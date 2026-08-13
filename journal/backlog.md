# Millhaven — candidate backlog

Proposed 2026-08-13 from the reconnaissance pass. **Nothing here is started.** Ranked
within each bucket by value against risk.

File references are to `Millhaven/Source/Millhaven/…` unless stated otherwise, on branch
`claude/new-session-tl0t6r`.

### The constraint that shapes this list

This container has **no Unreal Engine and no dotnet SDK**. I can read, reason, patch,
and run `Tools/check_sources.py`. I cannot compile, cannot run PIE, and cannot see the
game. So "safe to run unattended" here means *"the reasoning is verifiable without an
engine and the blast radius is small"* — it does **not** mean "verified working". Every
item in bucket A still needs you to build it. I've marked what each one costs you to
check.

---

## A. Safe to run unattended

**A1. Automation Spec tests for the pure functions** — *high value, low risk*
`TerrainHeight`, `BiomeAt`, `GroundPos`, `Prng` (`MillhavenWorldGen.cpp:95-147`) and the
`FMillhavenMeshBatch` primitives (`ProcMeshLib.h`) are pure, deterministic, and
engine-state-free — the ideal test surface, and the reason this is ranked first. Specs
would pin: the village disc really is flat inside 16m; `BiomeAt` boundaries agree with
`TerrainHeight`'s features; `AddBox` treats `Size` as a full extent; cone/cylinder
winding produces outward normals; vertex and index counts match `3 × triangles`;
`Prng` doesn't collapse for degenerate seeds (the bug fixed in the hardening pass).
No new module dependency — `IMPLEMENT_SIMPLE_AUTOMATION_TEST` lives in `Core`. Guard
with `#if WITH_AUTOMATION_TESTS`.
*Caveat: I cannot compile these, so first-build syntax errors are likely. Cost to you:
one build + `Automation RunTests Millhaven` in the console.*

**A2. Extend `check_sources.py` to catch the hazards it currently misses** — *good
value, very low risk*
It already catches engine type-name collisions. It does **not** catch the other
recurring hazards in `CLAUDE.md` §5: a member or parameter named `Role`; a
function-local `static U*` pointer; a UObject pointer member without `UPROPERTY()`;
`CreateDefaultSubobject<UInputAction>`. All four are grep-shaped. Pure tooling, runs
here, cannot affect the game.
*Cost to you: nothing — I can verify this one fully.*

**A3. Sync the minimap building table to its source** — *low value, low risk*
`MillhavenHUD.cpp:18-25` duplicates the six `AddBuilding` positions from
`MillhavenWorldGen.cpp:375-380` by hand. They drifted apart once already; the current
comment is the only thing holding them together. Options: expose a shared static table
from the worldgen, or add a `check_sources.py` rule comparing the two lists.
*Cost to you: one build.*

**A4. Dialogue-tree validator** — *moderate value, low risk*
Nothing verifies that every `AddOption`'s `Next` resolves to a real node, that `"start"`
exists on each NPC, or that no node is unreachable. `AddOption` warns on an unknown
*source* node (`MillhavenNPC.cpp:152`) but never checks the *destination*. A
`WITH_AUTOMATION_TESTS` spec or a `#if !UE_BUILD_SHIPPING` post-spawn pass would catch
a typo'd key that currently just dead-ends a conversation silently.
*Cost to you: one build.*

**A5. Second pass over `CREDITS.md` formatting** — *low value, no risk*
Restructure into a table with a `Verified?` column so the two unverified entries and the
mismatched-ID entry (`CREDITS.md:33,37-39`) are impossible to miss. **Formatting only —
I will not change any licence claim.**
*Cost to you: a glance.*

---

## B. Needs your input first

**B1. Washed-out terrain colour** — *blocking, and blocked on you*
Carried over from the previous session, which ended waiting for console results. This
gates every visual task, so it should go first.

I read the lighting and material path and think the three suggested tests aren't equally
informative — **`showflag.Lighting 0` is the decisive one**, and it's worth running
first rather than third:

- If terrain is **still grey/white unlit** → base colour is wrong, so it's the
  *material*, not the lighting. `MakeColorMID` (`MillhavenWorldGen.cpp:80-93`) sets a
  vector parameter named `Color` on `/Engine/BasicShapes/BasicShapeMaterial`. If that
  parameter name doesn't drive base colour on your engine build, every mesh renders the
  material's default and the vertex colours written alongside it are ignored, because
  `BasicShapeMaterial` has no Vertex Color node. **README §5 already documents the fix
  (`M_VertexColor`)** — which would make this a five-minute job rather than a lighting
  investigation.
- If terrain **goes green unlit** → base colour is fine and it *is* lighting. Then
  `r.Fog 0` and `r.SkyAtmosphere 0` separate the height fog
  (`MillhavenWorldGen.cpp:241-245`, inscattering `0.65,0.78,0.9` — bright and blue) from
  SkyAtmosphere aerial perspective (`:230-233`, Rayleigh `0.18,0.34,0.85` — strongly
  blue). Sun intensity is `6.0` lux (`:227`), below UE's default 10, which would make
  any haze read as proportionally stronger.

Two hypotheses, one test to separate them. I need the result before touching anything.

**B2. Merge Millhaven to `main`** — *high value, needs your call*
`main` is still one commit containing `TestFile`. The entire project has only ever
existed on unmerged branches. Anyone cloning this repo — including a future Claude
session, as happened today — sees an empty repo. This is a real risk of loss, and the
fix is a PR you merge. I won't touch `main` without you saying so.

**B3. What to do about the Qwen/Unity branch** — *high value, architectural*
`codex/create-npc-communication-file-with-qwen2.5-7b-instruct` contains
`Assets/Scripts/QwenNpcDialogue.cs`, a Unity `MonoBehaviour` calling an
OpenAI-compatible Qwen2.5-7B-Instruct endpoint for live NPC dialogue, plus a .NET 8
console tester. It cannot run in Millhaven: different engine, different runtime,
different NPC cast, and a fundamentally different dialogue architecture (networked LLM
inference vs. hand-authored `TMap<FName, FDlgNode>` trees).

The idea is a strong direction for Millhaven and the character writing is good. But
porting it means UE5 C++ HTTP, async response handling, a fallback when the endpoint is
unreachable, latency masking in the dialogue UI, API-key handling, and a decision about
whether quests can still be driven by a model that improvises. **That is a design
conversation, not a task.** Options: keep the branch parked, schedule a UE5 port as a
proper design task, or archive it. Also note it would be this project's first outbound
network dependency — flagged per the guardrails.

**B4. Save-game format** — *needs sign-off before any work*
None exists. Quest state is two `FString`s on the pawn (`MillhavenCharacter.h:43-44`),
lost on exit. Adding persistence means choosing what's canonical: the world is
deterministic from `TerrainHeight`, so a save could be tiny (player transform + quest
state) — but only while nothing mutates the world. The moment anything is destructible
or placeable, that assumption breaks and the format has to change. Worth deciding
deliberately, early, and with you.

**B5. What is this project actually for?** — *changes how everything above is ranked*
The README reads like a polished starter kit for other people. The world content —
four NPCs, branching dialogue, a quest tracker, regional lore — reads like a game you're
building. Those point at different priorities: the first wants docs, tests and a clean
first-run experience; the second wants quest completion, more content, and NPC
behaviour. `Config/DefaultGame.ini` says `ProjectVersion=0.1.0` and nothing else. I'd
rather ask than guess.

---

## C. Bugs and gaps found while exploring

**C1. The water plane covers dry land next to the village** — *confirmed by arithmetic,
worth fixing*
`MillhavenWorldGen.cpp:539-540`. The water is a single hard-edged square: centre
`(-30m, 32m)`, 48m × 48m, so it spans x ∈ [-54, -6], y ∈ [8, 56], at a fixed z = 5cm.
But the coastal basin that lowers the terrain only applies where `ax < -18 && ay > 18`
(`:120-125`). In the wedge around x ∈ [-16, -6], y ∈ [8, 18] the terrain is at or near
z = 0 — inside or just outside the flat village disc — so **a blue water quad sits ~5cm
above green grass roughly 10m from the village centre**, with no beach transition (the
sand test at `:285` requires `ax < -16 && ay > 16`, so it doesn't cover this wedge
either). At the opposite corner the water is only ~14cm deep. The shoreline doesn't
follow the basin at all. Fix: shrink and reshape the quad to the basin, or clip water
tiles to `TerrainHeight < waterline`.

**C2. The minimap is rotated 90°** — *confirmed by inspection, small fix*
`MillhavenHUD.cpp:110-111` maps world X → screen x and world Y → screen y. In UE, +X is
north and +Y is east; on screen, +y is **down**. So north draws to the right and east
draws downward — the whole map is rotated 90° clockwise relative to the world. Walking
north moves your blip's surroundings sideways. Correct mapping for a north-up map:
`mx = cx + (WorldY - P.Y) * scale; my = cy - (WorldX - P.X) * scale;`
(Note: the previous session already fixed the minimap *zoom* at `:105`. This is a
separate axis bug in the same function.)

**C3. Quests can be started but never completed** — *design gap, not a defect*
`MillhavenCharacter.cpp:374-378` overwrites `QuestName`/`QuestObjective` when a dialogue
option carries them. There is no completion condition, no item, no state machine, no
"done". Four NPCs hand out quests that can never be finished. The HUD panel
(`MillhavenHUD.cpp:82-90`) always renders one. This is the largest functional gap in the
game and probably the most valuable feature work — but it needs B5 answered first, and a
real design, so it isn't an unattended task.

**C4. NPC registry isn't world-scoped** — *latent, low priority*
`AMillhavenNPC::All` (`MillhavenNPC.cpp:10`) is a process-global static. `GetNearbyNPC`
(`MillhavenCharacter.cpp:319`) iterates it without comparing `GetWorld()`. Fine in
single-player PIE. With multiple PIE instances, or an editor preview world alongside a
running game, NPCs from one world become interactable from another. One-line fix; add it
to whatever branch next touches that file.

**C5. HUD text wraps by character count, not measured width** — *cosmetic*
`MillhavenHUD.cpp:38-58` wraps at 78 characters; `:165` draws into a 720px panel with a
proportional font at 0.92 scale. Wide lines will overflow and narrow ones waste space.
`UFont::GetStringSize` would do it properly. Only matters once dialogue text varies more
than it does now.

**C6. The map asset doesn't exist in the repo** — *first-run friction*
`Content/Maps/.gitkeep` is a placeholder; `Config/DefaultEngine.ini` points
`GameDefaultMap` and `EditorStartupMap` at `/Game/Maps/Millhaven`, which isn't there. The
editor warns on every fresh clone. README §2.3 and `Tools/create_millhaven_map.py` both
handle it, and the warning is harmless — but every new clone hits it. Worth deciding
whether to commit the (tiny, empty) map asset.

**C7. NPCs have no collision** — *possibly intentional, worth confirming*
All three NPC mesh components are `NoCollision` (`MillhavenNPC.cpp:49,53,57`), so the
player walks straight through them. Reasonable while they're conversation nodes; wrong
if they ever become physical characters.

**C8. No TODO/FIXME markers anywhere** — *observation, not a task*
I grepped. The codebase has none. Unfinished work is recorded in README §7 ("Known
limitations") instead, which is a better habit than scattered markers — worth keeping.

---

## Suggested order

1. **B1** — unblocks all visual work, and may turn out to be the documented five-minute
   material fix rather than a lighting hunt.
2. **B2** — the project currently exists only on unmerged branches.
3. **B5** — cheap to answer, and it re-ranks everything below it.
4. **A1 + A2** — the safest real work available, and A1 builds the safety net that makes
   later unattended work trustworthy.
5. **C2, C1** — small, well-understood, visible improvements.
6. **B3, B4, C3** — the architectural conversations, once the ground is stable.

Awaiting your approval before starting anything.
