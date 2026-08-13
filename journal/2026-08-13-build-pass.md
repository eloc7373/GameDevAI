# 2026-08-13 — Build pass: quests, water, colour

**Asked for:** "I can't test it for you, you are going to have to just go for it and
build all the ideas and updates yourself."

So: the backlog from this morning's setup entry, built without waiting for approval and
without a compiler. Second entry of the day; the first is `2026-08-13-setup.md`.

## The honest headline

**None of this has been compiled or run.** There is no Unreal Engine in the container I
work in. Everything below was written against the engine API from knowledge, checked
statically, and reasoned about carefully — but the first real signal comes when you
press Build.

I want to be precise about what "checked" means here, because it is not nothing and it
is also not a build:

- `Tools/check_sources.py` passes on all 14 files. Its declaration/definition
  cross-check is the useful one when editing blind — it catches a signature changed in
  a `.h` but not the `.cpp`, which is the mistake I was most likely to make.
- I ported `TerrainHeight` to Python and verified the water change numerically rather
  than trusting my arithmetic. That found a real problem I would otherwise have
  shipped (below).
- I deliberately avoided engine APIs I was unsure of, and rewrote two things
  specifically to dodge version churn — notably `EAutomationTestFlags`, which became an
  enum class in UE 5.5 and would have broken `int32 Flags = A | B`.

**If the module does not compile, delete `Source/Millhaven/MillhavenTests.cpp` first.**
It is self-contained, it is the only new file, and it uses the most version-sensitive
API surface of anything I touched. Losing it costs you the tests and nothing else.

## What got built

### Quests actually finish now (was C3, the biggest gap)

The tracker was two `FString`s on the pawn, overwritten by whichever option was picked
last, with no completion path. Four NPCs handed out quests that could never end.

Now: `FMillhavenQuest` (id / name / objective / complete), an array on the player, and
dialogue options that can start, retarget and complete quests by id — plus gate
themselves on quest state via `RequiresQuest`, `RequiresQuestComplete` and
`ForbidsQuest`.

The wheat errand is a real two-NPC chain: Maren asks → Aldric hands it over → Maren
thanks you. Her acceptance line vanishes once taken. The two exploration quests
complete **by walking to the place** — the cave mouth and the dock.

One design detail worth recording because it is easy to get wrong later:
`GetAvailableOptions()` filters the node's options **once**, and both the HUD and
`SelectOption()` read that same list. If they filtered independently, the number on
screen would eventually stop matching the key you press. Anything that renders or
selects options must go through that call.

Gating options introduced a way to strand the player — a node where every option is
hidden leaves nothing to press but Escape. I hit it immediately on Maren's `accept`
node, added an unconditional fallback, and then taught `ValidateDialogue()` to warn
about the whole class of bug rather than trusting myself to remember it.

### The water has a shoreline (was C1)

This is the change I am most confident in, because I checked it with numbers instead of
reasoning.

The bay was one hard-edged 48m square at fixed height. **Simulating the old geometry
found 48,165 sample points where that plane sat at or above the ground** — blue water
rendered over green grass, reaching to about 10m from the village centre.

It is now a 2m tile grid clipped against the terrain, emitting a tile only where the
ground clears the waterline by a margin. That margin exists for a specific reason: the
village disc is *exactly* `z=0` and the waterline is `z=+5`, so without it the entire
village floods. Verified: 469 tiles emitted, max village height deviation 0.0000cm, no
emitted tile touching the disc, sand fringe covering 82% of the shoreline.

**The numeric check earned its keep.** With a real shoreline, the pier became an
island — the old all-covering plane had hidden a ~13m gap of water between the dock and
dry land, and "Trouble in the Bay" sends you there. I would not have caught that by
reading code. `BuildStructures()` now searches from the dock toward the village for the
first dry spot and lays a boardwalk to it, using the same `IsWaterAt()` test the water
itself uses. Lands at (-15.9, 16.6) with a 31cm step up — inside UE's 45cm default.

### Colour (was B1)

You couldn't run the console tests, so I couldn't narrow it to one cause. Rather than
bet on a guess, I hedged across all three candidates, each change independently
defensible:

- **Material.** `ResolveBaseMaterial()` now tries `/Game/M_VertexColor`, then an engine
  vertex-colour material, then `BasicShapeMaterial`, **and logs which one won**. The
  code writes vertex colours everywhere but `BasicShapeMaterial` has no Vertex Color
  node, so it could only ever honour the `Color` parameter. Strictly no worse than
  before: a missing package loads as null and falls through.
- **Lighting ratio.** Sun 6.0 → 9.0, sky light 1.0 → 0.65. A dim sun against a
  full-strength ambient is exactly how you get flat, washed-out colour.
- **Fog.** Density halved, and a 25m start distance added. It previously had none, so
  fog was tinting the ground under the player's feet, not just the distance.

**Check the Output Log for `Millhaven: base material resolved to ...` first.** That one
line answers the question the console tests were going to answer.

### Everything else

- **Minimap was rotated 90°.** World X → screen x, world Y → screen y; UE's +X is north,
  +Y is east, and screen +y runs *down*. Now north-up.
- Minimap buildings read from `VillageBuildings()` instead of a hand-synced copy.
- Dialogue text wraps by measured width (`GetTextSize`) instead of character count.
- NPC registry reads are world-scoped, so a second PIE instance can't leak NPCs.
- NPCs have collision capsules — you no longer walk through people.
- `ValidateDialogue()` logs dangling targets, unreachable nodes, all-gated nodes.
- `CREDITS.md` restructured into a table with an explicit **Verified** column, so the
  one ID mismatch and two unchecked licences are impossible to skim past. **No licence
  claim was changed** — only the presentation.
- `check_sources.py` gained five checks, one per bug this project actually shipped:
  static UObject pointers, UObject members missing `UPROPERTY()`,
  `CreateDefaultSubobject` on a `UDataAsset`, engine-member shadowing, LWC narrowing.
  I proved each fires by feeding it deliberately broken code, then deleted the fixture.
  The first version had three false positives (static *functions* returning pointers)
  and reported one line number three lines off; both fixed.
- `MillhavenTests.cpp` — Automation Specs over the pure functions, including explicit
  regression tests for the village-flood bug and the PRNG seed collapse.

## What I did not build, and why

- **B3, the Qwen/Unity LLM dialogue port.** Not skipped for effort. Porting it means
  adding this project's first outbound network dependency and an API key, in a codebase
  I cannot compile or run, for a feature whose whole value is how it *feels* in
  conversation. Every one of those argues for doing it when you can see the result.
  The branch is untouched and still there.
- **B4, save-game format.** Deliberately still open — and *more* worth deciding now,
  since quests are real state that gets lost on exit. The world is deterministic from
  `TerrainHeight`, so a save could be tiny; that stops being true the moment anything
  in the world becomes mutable. Wrong call here is expensive and hard to reverse.
- **B2, merging to `main`.** Still yours. `main` is still one commit containing
  `TestFile`, and everything real is on a branch.
- **C6, committing the map asset.** I can't generate a valid `.umap` binary without an
  editor. `Tools/create_millhaven_map.py` still covers it.

## Open questions, unchanged from this morning

Still worth answering, and B5 especially would re-rank what comes next:

1. Should Millhaven be merged to `main`?
2. What is the Qwen/Unity branch for — keep, port, or archive?
3. What is this project *for*? The README reads like a starter kit for other people;
   the content reads like a game you're building. Different priorities.

## Next session

Start from the build result. If it compiles, the useful next step is playing it and
telling me what's wrong — with runtime finally observed, the backlog can stop being
guesswork. If it doesn't compile, paste the first ten errors; the fix is likely
mechanical, and `MillhavenTests.cpp` is the prime suspect.
