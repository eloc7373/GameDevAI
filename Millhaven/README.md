# Millhaven — Unreal Engine 5 open-world starter

Built for **Unreal Engine 5.8** (the last UE5 major release before UE6).
A low-poly, explorable village with four interactable NPCs, branching dialogue,
a quest tracker, a minimap and a golden-hour sky. **The entire world is generated
in C++ at runtime**, so the project runs from a completely empty level with **no
imported art assets required**. When you're ready, you drop your own Sketchfab
models in on top (guide near the bottom).

> **Honest note on build status.** This code has never been compiled — it was
> written, and later hardened, without a copy of Unreal available. Treat it as a
> strong starting point, not as guaranteed-clean-on-first-build. Nothing here
> needs Blueprints.
>
> A hardening pass has since fixed the defects that were findable by inspection
> (see [§8](#8-ue-58-hardening-pass)), including one guaranteed compile error and
> several missing includes. `python3 Tools/check_sources.py` runs the static
> checks that *are* possible without an engine and currently passes clean. That
> is a much lower bar than a real compile — expect to still fix a thing or two.

---

## 1. Requirements

- **Unreal Engine 5.8** (installed via the Epic Games Launcher).
  Using a different 5.x? Open `Millhaven.uproject` in a text editor and change
  `"EngineAssociation": "5.8"` to your version. On 5.5-5.7 this project still
  builds; on 5.3/5.4 change `BuildSettingsVersion.Latest` in the two
  `*.Target.cs` files to `BuildSettingsVersion.V4`.
- A C++ toolchain:
  - **Windows:** Visual Studio 2022 (latest update) with the *"Game development
    with C++"* workload. UE 5.8 needs a recent 17.x.
  - **macOS:** Xcode (current version supported by UE 5.8).
- The **Procedural Mesh Component** and **Enhanced Input** plugins — both already
  enabled in the `.uproject` (Enhanced Input ships enabled by default in UE5).

## 2. First-time setup (about 5 minutes)

1. **Generate project files.** Right-click `Millhaven.uproject` →
   *Generate Visual Studio project files* (Windows). On Mac, just open the
   `.uproject`.
2. **Open & build.** Double-click `Millhaven.uproject`. Unreal will say the module
   is out of date and offer to rebuild — click **Yes**. (Or build the `Millhaven`
   target from your IDE first, then open.)
3. **Create the level.** The project points at a map it expects you to make:
   - In the editor: **File → New Level → Empty Level**.
   - **File → Save Current Level As** → the `Maps` folder already exists, name it
     `Millhaven`. (Final path: `Content/Maps/Millhaven`.) This matches the
     default-map setting already in `Config/DefaultEngine.ini`, so nothing else
     to configure.
   - *Or*, to skip the clicking: enable the **Python Editor Script Plugin** and
     run `Tools/create_millhaven_map.py` (instructions are in the file header).

   Until this map exists the editor will warn on startup that it can't find
   `/Game/Maps/Millhaven` and open a blank map instead. That's harmless — the
   Game Mode is set project-wide, so **Play** still builds the whole world.
4. **Confirm the Game Mode.** Edit → Project Settings → **Maps & Modes**. The
   *Default GameMode* should already be `MillhavenGameMode`. If it's blank, set it.
5. **Press Play.** The village, harbour, forest, cave, NPCs and sky all build the
   instant play starts.

## 3. Controls

| Input | Action |
|---|---|
| **W A S D** / arrows | Move |
| **Mouse** | Look / rotate camera |
| **E** | Talk to a nearby NPC |
| **1–4** | Choose a dialogue option |
| **Esc** | Close dialogue |
| **Space** | Jump |

Walk up to Baker Maren, Farmer Aldric, Elder Sylva or Captain Wren (red dots on
the minimap) and press **E**.

### Input is 100% code (no assets to make)

UE 5.8 uses **Enhanced Input**, and the legacy `BindAxis`/`BindAction` +
`DefaultInput.ini` mappings from older tutorials are deprecated. This project
creates every Input Action and the Input Mapping Context **in C++**
(`MillhavenCharacter.cpp` constructor), so there are **no Input Action `.uasset`
files to author in the editor** — it just works on first run. To rebind a key,
edit the `MapKey(...)` calls; to change what a key does, edit the matching
`BindAction(...)` call in `SetupPlayerInputComponent`.

## 4. How the world is built (so you can edit it)

Everything lives in `Source/Millhaven/`:

| File | Role |
|---|---|
| `ProcMeshLib.h` | Tiny geometry kit: `AddBox`, `AddCone`, `AddCylinder`, etc. (flat-shaded low-poly). The accumulator type is `FMillhavenMeshBatch` — **not** `FMeshBatch`, which is an engine type. |
| `MillhavenWorldGen.cpp` | Builds terrain, buildings, dock, cave, trees, water, clouds; spawns NPCs. **Start here to change the map.** |
| `MillhavenNPC.cpp` | NPC body + dialogue-tree storage. |
| `MillhavenCharacter.cpp` | Third-person player, movement, camera, interaction, quest state. |
| `MillhavenHUD.cpp` | Quest panel, minimap, dialogue box, banner (Canvas, no UMG). |
| `MillhavenGameMode.cpp` | Spawns the world generator; sets pawn + HUD. |

- **Move/resize a building:** edit the `AddBuilding(ax, ay, w, d, wall, roof)`
  calls in `BuildStructures()`. `ax, ay` are metres from the village centre.
- **Edit dialogue:** in `SpawnNPCs()`, each NPC is a set of `AddNode` +
  `AddOption` calls. `"x"` closes the conversation; the two trailing strings on an
  option set the quest name/objective.
- **Terrain shape:** `TerrainHeight()` is one pure function — tweak the sine terms
  to reshape hills/coast. Object placement follows it automatically.

## 5. Colors look wrong? (materials)

The world colours meshes two ways at once for safety: it tints the engine's
`BasicShapeMaterial` **and** writes vertex colours. If everything renders a flat
grey/white on your build, make a one-time material:

1. Content Browser → **Add → Material**, name it `M_VertexColor`.
2. Open it, add a **Vertex Color** node, plug its **RGB** into **Base Color**, save.
3. In `MakeColorMID` / `CharColorMID` / the NPC helper, change the loaded path from
   `/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial` to
   `/Game/M_VertexColor.M_VertexColor`. Rebuild.

## 6. Dropping in your Sketchfab models

Your procedural props are placeholders. To swap in real art:

1. **Download** each model from your Sketchfab account (FBX or glTF) — respecting
   its licence (see `CREDITS.md`).
2. **Import** into Unreal: drag the `.fbx` into the Content Browser → it becomes a
   **Static Mesh** (e.g. `SM_PineTree`). Assign its textures if they don't
   auto-hook.
3. **Two ways to use them:**
   - *No code (quickest):* just drag the static mesh into the level wherever you
     like. Great for hero pieces like the castle.
   - *Replace the procedural spawns:* swap a placeholder for your mesh. Example —
     to replace the code trees, open `MillhavenWorldGen.cpp`, and instead of
     `AddTree(...)` spawn a mesh:

     ```cpp
     // near the top of the .cpp
     #include "Components/StaticMeshComponent.h"
     #include "Engine/StaticMesh.h"

     // ...and in MillhavenWorldGen.h, so the mesh stays GC-rooted:
     //     UPROPERTY() UStaticMesh* TreeMesh = nullptr;

     void AMillhavenWorldGen::AddTree(float AX, float AY, float Scale)
     {
         if (!TreeMesh)
         {
             TreeMesh = LoadObject<UStaticMesh>(
                 nullptr, TEXT("/Game/Trees/SM_PineTree.SM_PineTree"));
         }
         if (!TreeMesh) return;
         UStaticMeshComponent* SM = NewObject<UStaticMeshComponent>(this);
         SM->RegisterComponent();
         SM->SetStaticMesh(TreeMesh);
         SM->SetWorldLocation(GroundPos(AX, AY, 0.f));
         SM->SetWorldScale3D(FVector(Scale));
         SM->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepWorldTransform);
     }
     ```

     > Cache the loaded asset in a `UPROPERTY()` member, **not** a function-local
     > `static UStaticMesh*`. A bare static is invisible to the garbage collector,
     > so the mesh can be collected out from under you between levels.

   The same pattern replaces bushes, rocks, the well, buildings, the castle, etc.
   — keep the `GroundPos(...)` placement so props still sit on the terrain.

## 7. Known limitations / first-tweak checklist

- **Player falls through / floats:** the pawn spawns at `TerrainHeight + 120cm`,
  and `MillhavenCharacter::EnforceGroundSafety()` re-seats you every tick if you
  ever end up more than 4m below the analytic terrain height. If you still fall,
  the world mesh's collision is not cooking — check the Output Log.
- **First-frame hitch.** The terrain is ~14k triangles and its collision is cooked
  synchronously at `BeginPlay` so the player never lands on an uncooked mesh. That
  costs a visible hitch on load. To trade safety for smoothness, set
  `Mesh->bUseAsyncCooking = true` in the `AMillhavenWorldGen` constructor.
- **Trees are walk-through** (decorative, no collision) so you never snag on
  foliage. Add collision later per the asset-swap section if you want solid trunks.
- **Sky/lighting** uses a real-time Sky Light + Sky Atmosphere. If the sky is
  black, add a **Sky Atmosphere** actor to the level manually, or lower reliance on
  real-time capture in `BuildEnvironmentLighting()`.
- **Limb animation** is a simple body-bob (Procedural Mesh can't rotate individual
  sections). For real walk cycles, swap the avatar for a Skeletal Mesh later.

---

## 8. UE 5.8 hardening pass

Changes made to get the project closer to a clean first build. Nothing here was
verified by a compiler — see the note at the top.

**Would not have compiled**

- `ProcMeshLib.h` declared a global `struct FMeshBatch`. The engine already
  declares `FMeshBatch` (`SceneManagement.h`), which the Engine shared PCH pulls
  in — a guaranteed redefinition error. Renamed to `FMillhavenMeshBatch`.
- Added the includes that `EngineIncludeOrderVersion.Latest` (IWYU) requires but
  that nothing pulled in transitively: `GameFramework/Controller.h`,
  `GameFramework/PlayerController.h`, `Camera/PlayerCameraManager.h`,
  `Engine/LocalPlayer.h`, `Engine/World.h`, `Engine/Font.h`, `EngineUtils.h`,
  `Materials/Material.h`.

**Would have compiled, but misbehaved at runtime**

- **Input assets.** `UInputAction` and `UInputMappingContext` are `UDataAsset`s,
  and were being built with `CreateDefaultSubobject` in the character
  constructor. They are now built with `NewObject` and held in `UPROPERTY()`
  members. Because `SetupPlayerInputComponent` can run *before* `BeginPlay`,
  construction is idempotent and driven from `PawnClientRestart`.
- **GC hazards.** Two `static UMaterialInterface*` caches held UObject pointers
  invisible to the garbage collector; they are now `UPROPERTY()` members with a
  `GetDefaultMaterial` fallback. The `AMillhavenNPC::All` registry now holds
  `TWeakObjectPtr`, so a destroyed NPC can't leave a dangling entry.
- **LWC narrowing.** `FVector`/`FVector2D` are double-based in UE5. Results from
  `Dist2D`, `Size2D`, `FVector2D::Distance` and `Atan2` were being assigned
  straight to `float`; all such sites now cast explicitly (C4244).
- **Dialogue use-after-free.** `SelectOption` read `Opt.Next` after calling
  `OnClose()`, which clears `ActiveNPC` and invalidates the node it points into.
- **World build ordering.** The generator was spawned from `GameMode::BeginPlay`,
  which is not ordered against the pawn's. It now spawns in `InitGame`, so its
  `BeginPlay` runs in the world's normal actor sweep, and it refuses to
  double-build if one was already placed in the level by hand.
- **Cloud placement.** `BuildClouds` seeded its PRNG with `i * j + k`, which
  collapses to the same value whenever `i` or `j` is 0 — so several cloud
  clusters were stacked in one spot. Seeds now mix `i` and `j` with distinct
  primes.
- `AddFenceRun` divided by a step count that is 0 for a zero-length segment.
- The HUD minimap's hardcoded building list had drifted from `BuildStructures()`
  and is now a single named table with a comment tying the two together.
- `AddOption` against an unknown node key silently dropped the option; it now
  warns.

**Tooling**

- `Tools/check_sources.py` — static checks that don't need an engine: engine
  type-name collisions, `.generated.h` include ordering, declaration/definition
  cross-checks, brace balance, unresolvable local includes, missing
  `GENERATED_BODY()`. Run it before you build; exit code is non-zero on error.
- `Tools/create_millhaven_map.py` — creates `Content/Maps/Millhaven` so you can
  skip the manual level-creation step.

Have fun in Millhaven.
