#!/usr/bin/env python3
"""
Parses the dialogue trees out of SpawnNPCs() and proves the quest chain works.

The trees are built imperatively in C++ and cannot be inspected without running
the game, but the questions worth asking are all reachability questions:

  * Can every quest actually be started?
  * Can every quest actually be *finished*?
  * Is there any reachable quest state where a node offers the player nothing?
  * Does every option target a node that exists?
  * Is any node unreachable?

AMillhavenNPC::ValidateDialogue() answers the last two at runtime, per NPC.
This answers all five, across all NPCs at once, before the game is ever built -
which matters because a quest chain spans NPCs and no single NPC can see it.

    python3 Tools/check_dialogue.py

Exit code is non-zero on any error-level finding.
"""

from __future__ import annotations

import os
import re
import sys
from dataclasses import dataclass, field
from itertools import product

WORLDGEN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "Source", "Millhaven", "MillhavenWorldGen.cpp")
CHARACTER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "Source", "Millhaven", "MillhavenCharacter.cpp")

CLOSE = "x"


@dataclass
class Option:
    label: str
    next: str
    quest_id: str = ""
    quest_name: str = ""
    completes: str = ""
    requires: str = ""
    requires_complete: str = ""
    forbids: str = ""

    @property
    def starts(self) -> str:
        """The quest id this option starts, mirroring SelectOption()'s rule."""
        if not self.quest_name:
            return ""
        return self.quest_id or self.quest_name

    @property
    def gated(self) -> bool:
        return bool(self.requires or self.requires_complete or self.forbids)


@dataclass
class Npc:
    name: str
    nodes: dict = field(default_factory=dict)   # key -> list[Option]


def strip_block_comments(text: str) -> str:
    return re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)


def parse_npcs(src: str) -> list[Npc]:
    """Pull each `Spawn(...)` block and the AddNode/AddOption calls under it."""
    body = src[src.index("void AMillhavenWorldGen::SpawnNPCs()"):]
    body = strip_block_comments(body)
    # Drop line comments so commented-out calls are not parsed as real ones.
    body = re.sub(r'//[^\n]*', '', body)

    spawn_re = re.compile(r'Spawn\(\s*[^,]+,\s*[^,]+,\s*TEXT\("([^"]+)"\)')
    marks = [(m.start(), m.group(1)) for m in spawn_re.finditer(body)]
    if not marks:
        return []

    npcs: list[Npc] = []
    for idx, (pos, name) in enumerate(marks):
        end = marks[idx + 1][0] if idx + 1 < len(marks) else len(body)
        npcs.append(parse_one(name, body[pos:end]))
    return npcs


FIELD_MAP = {
    "Label": "label",
    "Next": "next",
    "QuestId": "quest_id",
    "QuestName": "quest_name",
    "QuestObjective": None,
    "CompletesQuest": "completes",
    "RequiresQuest": "requires",
    "RequiresQuestComplete": "requires_complete",
    "ForbidsQuest": "forbids",
}


def parse_one(name: str, chunk: str) -> Npc:
    npc = Npc(name=name)

    for m in re.finditer(r'AddNode\(\s*"([^"]+)"', chunk):
        npc.nodes.setdefault(m.group(1), [])

    # Short form: AddOption("node", TEXT("label"), "next" [, TEXT(q), TEXT(o)])
    short = re.compile(
        r'AddOption\(\s*"([^"]+)"\s*,\s*TEXT\("([^"]*)"\)\s*,\s*"([^"]*)"'
        r'(?:\s*,\s*TEXT\("([^"]*)"\)\s*,\s*TEXT\("([^"]*)"\))?\s*\)')
    for m in short.finditer(chunk):
        node, label, nxt, qname, _obj = m.groups()
        npc.nodes.setdefault(node, []).append(
            Option(label=label, next=nxt, quest_name=qname or ""))

    # Struct form: a block of `Opt.Field = ...;` ending in AddOption(node, Opt)
    struct = re.compile(
        r'FDlgOption\s+Opt\s*;(?P<assigns>.*?)AddOption\(\s*"(?P<node>[^"]+)"\s*,\s*Opt\s*\)',
        re.DOTALL)
    # Values come two ways: TEXT("...") for FString fields and a plain quoted
    # literal for FName fields. Matching only the first silently drops every
    # Next and QuestId, which makes the graph look like a pile of dead ends.
    assign = re.compile(
        r'Opt\.(\w+)\s*=\s*(?:TEXT\(\s*"([^"]*)"\s*\)|"([^"]*)")\s*;')

    for m in struct.finditer(chunk):
        opt = Option(label="", next="")
        for a in assign.finditer(m.group("assigns")):
            key = a.group(1)
            raw = a.group(2) if a.group(2) is not None else a.group(3)
            attr = FIELD_MAP.get(key, "missing")
            if attr == "missing":
                raise SystemExit(f"check_dialogue.py: unknown FDlgOption field "
                                 f"'{key}' - teach the parser about it")
            if attr:
                setattr(opt, attr, raw)

        # An option that closes the conversation spells it "x". A blank Next
        # here means the parser failed, not that the author meant it.
        if not opt.next:
            raise SystemExit(
                f"check_dialogue.py: parsed an option on {name} node "
                f"'{m.group('node')}' with no Next - the parser is out of date "
                f"with the C++")

        npc.nodes.setdefault(m.group("node"), []).append(opt)

    return npc


def parse_location_quests(src: str) -> set[str]:
    """Quest ids UpdateLocationQuests() completes by arrival."""
    m = re.search(r'const FArrival Arrivals\[\]\s*=\s*\{(.*?)\};', src, re.DOTALL)
    if not m:
        return set()
    return set(re.findall(r'FName\("([^"]+)"\)', m.group(1)))


def parse_seed_quests(src: str) -> set[str]:
    """Quests the pawn starts itself, e.g. the opening objective in BeginPlay."""
    return set(re.findall(r'StartOrUpdateQuest\(FName\("([^"]+)"\)', src))


def parse_code_completed_quests(src: str) -> set[str]:
    """Quests some gameplay code completes directly, not a dialogue option.

    Every CompleteQuest(FName("...")) call site in the pawn counts: arriving at
    a landmark, opening a conversation, and so on. These are treated as always
    available to the player, which is why the search can reach them from any
    state.
    """
    return set(re.findall(r'CompleteQuest\(FName\("([^"]+)"\)', src))


# --- state-space search -----------------------------------------------------
# A world state is a mapping quest -> 0 absent / 1 active / 2 complete.

ABSENT, ACTIVE, DONE = 0, 1, 2


def available(opts: list[Option], state: dict) -> list[Option]:
    out = []
    for o in opts:
        if o.requires and state.get(o.requires, ABSENT) != ACTIVE:
            continue
        if o.requires_complete and state.get(o.requires_complete, ABSENT) != DONE:
            continue
        if o.forbids and state.get(o.forbids, ABSENT) != ABSENT:
            continue
        out.append(o)
    return out


def apply(opt: Option, state: dict) -> dict:
    nxt = dict(state)
    if opt.starts and nxt.get(opt.starts, ABSENT) == ABSENT:
        nxt[opt.starts] = ACTIVE
    if opt.completes and nxt.get(opt.completes, ABSENT) == ACTIVE:
        nxt[opt.completes] = DONE
    return nxt


def explore(npcs: list[Npc], quests: list[str], seed: dict,
            auto_quests: set[str]):
    """BFS over global quest states, talking to every NPC in every state."""
    start = tuple(sorted(seed.items()))
    seen = {start}
    frontier = [start]
    empty_nodes: set[tuple[str, str]] = set()

    while frontier:
        cur = frontier.pop()
        state = dict(cur)

        # Arriving somewhere, or striking up a conversation, is always
        # available - so any code-completed quest can finish at any point.
        for q in auto_quests:
            if state.get(q, ABSENT) == ACTIVE:
                nxt = dict(state)
                nxt[q] = DONE
                key = tuple(sorted(nxt.items()))
                if key not in seen:
                    seen.add(key)
                    frontier.append(key)

        for npc in npcs:
            # Every conversation starts at "start".
            stack = ["start"]
            walked = set()
            while stack:
                node = stack.pop()
                if node in walked or node not in npc.nodes:
                    continue
                walked.add(node)

                opts = available(npc.nodes[node], state)
                if not opts:
                    empty_nodes.add((npc.name, node))
                    continue

                for o in opts:
                    nxt = apply(o, state)
                    if nxt != state:
                        key = tuple(sorted(nxt.items()))
                        if key not in seen:
                            seen.add(key)
                            frontier.append(key)
                    if o.next != CLOSE and o.next:
                        stack.append(o.next)

    return seen, empty_nodes


def main() -> int:
    with open(WORLDGEN, "r", encoding="utf-8") as fh:
        worldgen = fh.read()
    with open(CHARACTER, "r", encoding="utf-8") as fh:
        character = fh.read()

    npcs = parse_npcs(worldgen)
    if not npcs:
        print("ERROR could not find any Spawn() calls in SpawnNPCs()")
        return 2

    location_quests = parse_location_quests(character)
    seed_quests = parse_seed_quests(character)
    auto_quests = parse_code_completed_quests(character)

    errors: list[str] = []
    warnings: list[str] = []

    # --- per-NPC structural checks (mirrors ValidateDialogue) ---------------
    all_quests: set[str] = set(seed_quests) | set(auto_quests) | set(location_quests)
    for npc in npcs:
        if "start" not in npc.nodes:
            errors.append(f"{npc.name} has no 'start' node")
        reachable, stack = {"start"}, ["start"]
        while stack:
            node = stack.pop()
            for o in npc.nodes.get(node, []):
                if o.starts:
                    all_quests.add(o.starts)
                if o.completes:
                    all_quests.add(o.completes)
                for gate in (o.requires, o.requires_complete, o.forbids):
                    if gate:
                        all_quests.add(gate)
                if o.next in (CLOSE, ""):
                    continue
                if o.next not in npc.nodes:
                    errors.append(f"{npc.name} node '{node}' option '{o.label}' "
                                  f"targets unknown node '{o.next}'")
                elif o.next not in reachable:
                    reachable.add(o.next)
                    stack.append(o.next)
        for node in npc.nodes:
            if node not in reachable:
                warnings.append(f"{npc.name} node '{node}' is unreachable from 'start'")

    quests = sorted(all_quests)

    # --- reachability over the whole cast ----------------------------------
    seed = {q: (ACTIVE if q in seed_quests else ABSENT) for q in quests}
    # UpdateLocationQuests() completes through a loop variable rather than a
    # literal, so the arrival set has to be unioned in explicitly.
    states, empty_nodes = explore(npcs, quests, seed, auto_quests | location_quests)

    startable = {q for q in quests if any(dict(s).get(q, ABSENT) != ABSENT for s in states)}
    finishable = {q for q in quests if any(dict(s).get(q) == DONE for s in states)}

    for q in quests:
        if q not in startable:
            errors.append(f"quest '{q}' can never be started")
        elif q not in finishable:
            warnings.append(f"quest '{q}' can be started but never completed")

    for npc_name, node in sorted(empty_nodes):
        errors.append(f"{npc_name} node '{node}' offers nothing in some reachable "
                      f"quest state - the player is stranded there")

    # --- report -------------------------------------------------------------
    for npc in npcs:
        opts = sum(len(v) for v in npc.nodes.values())
        print(f"  {npc.name}: {len(npc.nodes)} nodes, {opts} options")
    print()
    print(f"  quests found: {', '.join(quests) if quests else '(none)'}")
    print(f"  completable:  {', '.join(sorted(finishable)) or '(none)'}")
    print(f"  by arrival:   {', '.join(sorted(location_quests)) or '(none)'}")
    other_auto = sorted(auto_quests - location_quests)
    print(f"  by gameplay:  {', '.join(other_auto) or '(none)'}")
    print(f"  reachable quest states explored: {len(states)}")
    print()

    for e in errors:
        print("ERROR   %s" % e)
    for w in warnings:
        print("WARNING %s" % w)

    print("\n%d NPC(s), %d quest(s), %d error(s), %d warning(s)."
          % (len(npcs), len(quests), len(errors), len(warnings)))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
