# CLAUDE.md — Millhaven

Persistent working notes for Claude Code sessions on this repo. Read this first.
Anything stated here as "verified" was actually checked; anything unverified says so.

## 1. What this is

**Millhaven** — a low-poly, explorable open-world village for **Unreal Engine 5.8**,
written entirely in C++. Started life as a Three.js prototype, ported to UE5.

The defining property of the project: **the whole world is generated in C++ at
runtime.** It runs from a completely empty level with no imported art assets. Terrain,
buildings, dock, cave, forest, water, clouds, NPC bodies and the player avatar are all
procedural geometry built from a small triangle-accumulator kit. There are **no
Blueprints and no `.uasset` content files** in the repo — `Content/` holds only
`Maps/.gitkeep`.

Sketchfab models are the *future* art path (licences already researched, see
`Millhaven/CREDITS.md`), not the current one. Procedural props are placeholders
designed to be swapped out.

### Repo layout

```
GameDevAI/                       <- git root
├── CLAUDE.md                    <- this file
├── journal/                     <- session log + backlog
├── TestFile                     <- vestigial, from the repo's first commit
└── Millhaven/                   <- the actual UE project root
    ├── Millhaven.uproject
    ├── README.md                <- excellent; read it, it is accurate
    ├── CREDITS.md               <- Sketchfab licence tracking
    ├── Config/                  <- DefaultEngine/Game/Input .ini
    ├── Content/Maps/.gitkeep    <- the map asset is NOT in the repo (see §3)
    ├── Source/
    │   ├── Millhaven.Target.cs
    │   ├── MillhavenEditor.Target.cs
    │   └── Millhaven/           <- the single runtime module
    └── Tools/                   <- python helper scripts
```

Note the nesting: the git root is `GameDevAI/`, the UE project root is
`GameDevAI/Millhaven/`. Paths in this file are relative to the git root.

### Current state (2026-08-13)

- `main` contains **only** `TestFile`. The entire project lives on unmerged branches.
  Millhaven is on `claude/new-session-tl0t6r`. Nothing has been merged, ever.
- **Compiles clean on UE 5.8** (per README + a prior session): `MillhavenEditor Win64
  Development`, MSVC 14.44, UHT + compile + link all succeed.
- **Runtime is not verified.** Compiling is not running. The world geometry, materials,
  input and dialogue have never been confirmed working in a live session.
- **Live open blocker:** terrain colour renders washed out in PIE. Diagnosis was
  interrupted waiting on console tests — see `journal/backlog.md` B1.

## 2. Module & dependencies

One runtime module, `Millhaven` (`Source/Millhaven/Millhaven.Build.cs`):

- Public: `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`,
  `ProceduralMeshComponent`
- Private: `PhysicsCore` (runtime collision cooking pulls it in)
- Plugins (`.uproject`): `ProceduralMeshComponent`, `EnhancedInput`
- Both targets: `BuildSettingsVersion.Latest`, `EngineIncludeOrderVersion.Latest`
  (IWYU — includes must be explicit, nothing is pulled in transitively)
- `PCHUsage = UseExplicitOrSharedPCHs`

**UE builds with warnings-as-errors.** C4458 (declaration hides class member) and C4244
(double→float narrowing, everywhere in LWC-land) are both *fatal*. This has already
bitten the project once; see §5.

## 3. Build & run

**Not verifiable from a Claude Code container** — there is no Unreal Engine and no
dotnet SDK installed here (checked 2026-08-13). Everything below is from the README and
prior sessions, not from a build I ran.

1. Requires UE 5.8 + VS 2022 ("Game development with C++") on Windows, or current Xcode
   on macOS. For 5.3/5.4, change `BuildSettingsVersion.Latest` → `V4` in both
   `*.Target.cs`.
2. Right-click `Millhaven/Millhaven.uproject` → *Generate Visual Studio project files*.
3. Open the `.uproject`; accept the rebuild prompt.
4. **Create the map**: File → New Level → Empty Level, Save As `Content/Maps/Millhaven`.
   This matches `Config/DefaultEngine.ini`. Until it exists the editor warns on startup
   and opens a blank map — harmless, the GameMode is project-wide so Play still builds
   the world. `Tools/create_millhaven_map.py` automates this (needs the Python Editor
   Script Plugin).
5. Press Play.

### What CAN be run here

```bash
python3 Millhaven/Tools/check_sources.py      # exit 0 = pass
```

Static checks needing no engine: engine type-name collisions, `.generated.h` include
ordering, declaration/definition cross-checks, brace balance, unresolvable local
includes, missing `GENERATED_BODY()`. **Verified passing 2026-08-13**: 13 files, 0
errors, 0 warnings. Run it before every commit — but treat it as a weak signal. It did
not and cannot catch the C4458 that a real compiler caught.

## 4. Coding conventions (observed in the existing C++)

- **Tabs** for indentation. Allman braces. Braces on single-statement `if`s in most
  places, though short guard clauses sometimes go on one line (`if (!W) return;`).
- UE prefixes throughout: `A` actors, `U` UObjects/components, `F` plain structs,
  `E` enums, `b` bools. Project types are `AMillhaven*` / `FMillhaven*`.
- **Every UObject pointer member is `UPROPERTY()`**, without exception. This is
  load-bearing, not stylistic — see §5 GC hazards. Non-UObject state (`TMap<FName,
  FDlgNode>`, `float TimeAccum`) is a bare member.
- Member init at the declaration (`float TimeAccum = 0.f;`), not in constructor bodies.
- **Design coordinates are metres; UE is centimetres.** Helpers take metres and
  multiply by 100 (`GroundPos(AX, AY, LiftCm)`). Sizes passed to `AddBox` are **full
  extents**, not half-extents. Keep this straight — it is the easiest thing to get wrong.
- LWC: `FVector`/`FVector2D` are double-based. Results from `Dist2D`, `Size2D`,
  `FVector2D::Distance`, `Atan2` need an **explicit `(float)` cast** at every
  assignment site. The codebase does this consistently; match it.
- **Comments explain *why*, not *what*.** Most comments in this codebase document a
  hazard or a past bug ("Seeds must not collapse when i or j is 0…", "Copy before
  OnClose(), which clears ActiveNPC…"). Do not add narration comments; do add a comment
  when you fix something subtle enough to be re-broken later.
- Section dividers are `// ---------------------------------------------------------------------------`.
- `UE_LOG(LogTemp, ...)` with messages prefixed `Millhaven:` for anything a user might
  need to see in the Output Log. Failure paths log rather than silently no-op.

### Architecture in one paragraph each

**World generation** (`MillhavenWorldGen.cpp`, 715 lines). An `AActor` spawned by the
GameMode in `InitGame`, which builds everything in `BeginPlay`. `TerrainHeight(WX, WY)`
is a **pure static function** — a sum of sines with a flat village disc (radius 16m), a
northern hill ramp, a south-west coastal basin and a rising outer rim. Because it's
pure and static, terrain mesh, object placement and player spawn all agree without a
single raycast; `AMillhavenCharacter` calls it directly. Geometry is accumulated into
per-colour `FMillhavenMeshBatch` batches (`B(FColor)`), then flushed by `CommitBatches`
into one `UProceduralMeshComponent` section per colour, each with its own
`UMaterialInstanceDynamic`. Four mesh components split by role: `Mesh` (solid, collision
cooked synchronously), `DecorMesh` (foliage, no collision), `WaterMesh` and `CloudMesh`
(both animated in `Tick`). `BiomeAt()` is the parallel pure function driving the HUD
location banner.

**NPCs** (`MillhavenNPC.cpp`). Each NPC is an actor with three procedural mesh
components (legs static, body and head bob out of phase). Dialogue is a
`TMap<FName, FDlgNode>` built imperatively at spawn time via `AddNode` / `AddOption`
calls in `WorldGen::SpawnNPCs()` — the four NPCs' entire trees are literals there.
`FDlgOption::Next` is a node key; the magic value `"x"` closes the conversation. Options
optionally carry a quest name + objective that overwrite the player's tracker. A
`static TArray<TWeakObjectPtr<AMillhavenNPC>> All` registry lets the player and HUD find
NPCs without iteration-order dependencies. **There is no behaviour tree, no AI
controller, no navmesh, and no movement** — NPCs are stationary conversation nodes.

**HUD** (`MillhavenHUD.cpp`). Immediate-mode `AHUD::DrawHUD` with Canvas primitives —
**no UMG, no widget assets**. Draws a quest panel, minimap, location banner, control
hints, and either the dialogue box or an interact prompt. Minimap blips come from a
hardcoded `VillageBuildings[]` table that must be kept in sync by hand with the
`AddBuilding()` calls in `BuildStructures()` (they drifted apart once already). Text
wrapping is by character count, not measured width.

**Player** (`MillhavenCharacter.cpp`). Third-person `ACharacter`, spring-arm camera,
procedural box avatar. All Enhanced Input assets — 11 `UInputAction`s and one
`UInputMappingContext` — are built **in C++ with `NewObject`** in `BuildInputAssets()`,
so there are no input `.uasset`s to author. Construction is idempotent and driven from
whichever of `PawnClientRestart` / `BeginPlay` / `SetupPlayerInputComponent` fires
first. `EnforceGroundSafety()` re-seats the pawn every tick if it falls >4m below the
analytic terrain height.

## 5. Hazards already paid for — do not regress these

Each of these was a real bug fixed during the UE 5.8 hardening pass. Re-introducing one
is the most likely way to break this project.

- **`FMeshBatch` is an engine type.** The batch struct is `FMillhavenMeshBatch`. Never
  declare a global type whose name might collide with an engine one — that is exactly
  what `check_sources.py` scans for.
- **Never name anything `Role`.** It hides `AActor::Role`; C4458 is fatal here. The NPC
  field is `NpcRole`.
- **No `static UObject*` caches.** A function-local `static UMaterialInterface*` is
  invisible to the GC and will be collected out from under you. Use a `UPROPERTY()`
  member. Same reason `AMillhavenNPC::All` holds `TWeakObjectPtr`.
- **`UInputAction`/`UInputMappingContext` are `UDataAsset`s** — build with `NewObject`,
  never `CreateDefaultSubobject`.
- **`UInputMappingContext::MapKey` returns a reference into an array that later calls
  reallocate.** Attach modifiers immediately, before the next `MapKey`.
- **Dialogue use-after-free.** `OnClose()` clears `ActiveNPC`, which invalidates any
  `FDlgNode*`/`FDlgOption&` pointing into it. Copy what you need *before* calling it.
- **Do not spawn the world generator from `GameMode::BeginPlay`** — it is not ordered
  against the pawn's. It spawns in `InitGame`, and refuses to double-build.
- **PRNG seeds must not collapse.** `Prng(i * j)` degenerates whenever either is 0. Mix
  with distinct primes.
- Include explicitly. IWYU is on; nothing arrives transitively.

## 6. Asset conventions

No third-party assets are bundled today. When Sketchfab models arrive:

- Import to `Content/<Category>/`, e.g. `Content/Trees/SM_PineTree`.
- Static meshes are `SM_` prefixed, materials `M_`, material instances `MI_`.
- **Cache every loaded asset in a `UPROPERTY()` member**, never a function-local static.
- **Licence tracking lives in `Millhaven/CREDITS.md`** and is the single source of
  truth. It already records per-model licence status. Several entries are CC-BY 4.0 and
  **require attribution wherever the game is published**; two are marked unverified and
  one has a mismatched ID. Any model added to the repo must get a `CREDITS.md` line at
  the same time. Do not mark a licence verified that you have not personally checked —
  the existing file is careful about this distinction and it must stay that way.

## 7. Don't touch without asking

Inferred from the project's state; confirm before working in any of these.

- **`main`** — never commit directly. Feature branch → PR → the owner merges. No
  force-push, no `git reset --hard`, no history rewriting on shared branches.
- **Build & packaging config** — `*.Target.cs`, `Millhaven.Build.cs`,
  `Millhaven.uproject`, `Config/*.ini`. Changing `BuildSettingsVersion`,
  `EngineIncludeOrderVersion`, the module dependency list or the plugin list can break a
  build the owner cannot easily re-verify. Flag and stop.
- **Save-game format** — none exists yet. That makes *introducing* one an architectural
  decision, not a chore: the quest tracker is currently two `FString`s on the pawn, and
  the first serialisation choice will be hard to undo. Do not add `USaveGame` on your
  own initiative.
- **The §5 hazard list** — those fixes look like arbitrary style choices. They are not.
- **`CREDITS.md` licence claims** — never upgrade an entry from unverified to verified
  without the owner having looked at the actual model page.
- **Anything shipping-related** — packaging settings, project version, company name.
- **`Tools/check_sources.py`** — weakening a check to make a commit pass is not a fix.

If it is unclear whether something is in scope: stop and ask rather than guess.

## 8. Working agreement

- One task at a time, on its own branch. PR when done, then **stop** — do not chain into
  the next task unprompted.
- Every PR explains what changed, why, and what you were unsure about.
- If a task turns out bigger or riskier than it looked, stop and say so rather than
  pushing through.
- Append a dated entry to `journal/` every session. It is shared memory between
  sessions and it is meant to be read by a person.
- **Be honest about verification.** This container cannot compile or run this project.
  "Compiles" means someone compiled it; "should work" means nobody has checked. Never
  blur the two — that distinction is the whole value of these notes.
