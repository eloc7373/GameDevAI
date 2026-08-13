# 2026-08-13 — Tooling pass: verifying what I can't run

**Asked for:** "Keep going with work! More ideas."

Third entry today, after `2026-08-13-setup.md` and `2026-08-13-build-pass.md`.

## The reasoning behind what I picked

There was already a large uncompiled change set sitting on this branch. Adding more
uncompiled C++ on top of it makes the eventual build failure bigger and harder to
diagnose, and the marginal feature is worth less than the risk. So this pass went the
other way: **build things that cannot break the build, and use them to check the work
already done.**

That turned out to be the right call. The tools found six real bugs, five of them in
code I had written that morning and read back twice.

## What got built

Three engine-free tools, all committed under `Tools/`:

- **`verify_worldgen.py`** — re-implements `TerrainHeight` and asserts the invariants
  the game depends on. Also re-reads the C++ constants and fails if the Python mirror
  has drifted, which is the usual way a check like this rots into a reassuring lie.
- **`check_dialogue.py`** — parses the dialogue trees out of `SpawnNPCs()` and searches
  the entire quest state space (72 states). It answers the question no single NPC can
  see, and that `ValidateDialogue()` structurally cannot: **can every quest actually be
  finished?**
- **`render_map.py`** — draws the world to an SVG from the same terrain function,
  colour rules and water test the game uses. Generated, so gitignored.

## What they found

**1. `IsWaterAt()` lied outside the bay.** I had written it as "ground below the
waterline", forgetting `BuildWater()` only tiles a rectangle. The cave hollow sits 3.2m
down in the northern hills, nowhere near the sea, and the helper called it water. It is
billed in the header as "the one definition of wet", so anything trusting it would have
inherited the bug. Now bounds-checked.

**2. Captain Wren, Harbour Master, spawned 1.04m under the waterline.** Standing on the
bay floor. This predates today — with the old all-covering water plane nothing revealed
it. He now stands on his own pier, lifted to the decking.

**3. "Trouble in the Bay" completed the instant it was handed out.** The arrival radius
was centred on the dock, and Wren stands on the dock. Eleven metres, radius eleven
metres. The quest would have ticked to "complete" in the same frame the player accepted
it. Fixed by pointing it at the moored boat instead — and the tool now enforces the
general rule: *no quest-giver may stand inside the arrival radius of the quest they
give.*

**4. The opening quest could never be completed.** "Everloaf" is seeded in `BeginPlay`
and no dialogue option or arrival check ever finished it, so the player would carry a
permanently unfinishable entry at the top of the tracker. Its objective text is
literally "explore the village and speak to someone", so `OnInteract()` now completes it
on the first conversation.

**5. The boardwalk was a 51cm ledge.** My own verification had passed it at "31cm"
because I measured to the underside of the planks instead of the walking surface. UE's
default max step is 45cm, so the walkway I had just added to fix the marooned pier was
itself unclimbable. It now ramps from deck height down to the shore, on posts.

**6. The minimap "fix" from this morning was wrong, and I introduced it.**

This one deserves the space.

I reported the minimap as rotated 90°, on the reasoning that UE's +X is north, so
mapping world X→screen x and Y→screen y must be wrong. I changed it. **Millhaven does
not use UE's axis convention.** It kept the screen-space axes of its Three.js
prototype — **+X east, +Y south, north is −Y** — and every piece of world content
agrees: `BiomeAt` puts "Northern Hills" at `ay < -28`, the cave commented "(north)" is
at y=−36, the cluster commented "(NE)" is +X/−Y, the basin commented "south-west" is
−X/+Y. Under that convention the original code was already a correct north-up map, and
what I shipped rotated it 90°.

Reverted. The code now carries a comment saying explicitly why it looks wrong and must
not be "corrected", and README §10 spells out the convention with the evidence table.
`CLAUDE.md` §4 has it too, because this is precisely the sort of thing a fresh session
gets wrong in the first hour.

What is worth noticing: I read that code carefully, twice, and reasoned confidently to
the wrong answer both times. It surfaced the moment `render_map.py` drew the world and
the harbour came out in the wrong corner. **Drawing the thing beat reading the code.**

## Discipline notes

Same rule as the `check_sources.py` extensions: a check that never fires is worse than
no check, because it is trusted. Every rule here was proven against deliberately broken
input — an option pointing at a missing node, a node with its unconditional fallback
removed, a broken quest chain — and each produced the right error before being kept.

`check_dialogue.py` needed that discipline itself. Its first version silently parsed
every `Opt.Next = "x";` as empty, because the regex only handled the `TEXT("...")` form
and not plain quoted FNames. It reported a tidy, entirely fictitious graph. It now
hard-errors if it parses an option with no `Next`, on the grounds that a blank there
means the parser is out of date rather than that the author meant it.

## State of the branch

Still **not compiled** — no engine here, unchanged. `check_sources.py` (14 files),
`verify_worldgen.py` (7 groups) and `check_dialogue.py` (5 quests, 72 states) all pass.

If the build fails, `MillhavenTests.cpp` remains the first thing to delete.

## Still open, still yours

1. Build it. Everything else is inference.
2. `main` is *still* one commit containing `TestFile`.
3. What is this project for? (backlog B5) — re-ranks everything.
4. Save format (B4) — quests are real state now and are lost on exit.
5. The Qwen/Unity branch (B3).

New in the backlog: **C9**, landmarks sitting in hollows rather than on features — the
cave mouth is 3.2m *below* zero in the middle of the "Northern Hills", which reads as a
sinkhole. Cheap to reshape now that the terrain maths runs standalone.
