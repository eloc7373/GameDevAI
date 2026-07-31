# Millhaven — Unreal Engine 5 open-world starter

Built for **Unreal Engine 5.8** (the last UE5 major release before UE6).
A low-poly, explorable village with four interactable NPCs, branching dialogue,
a quest tracker, a minimap and a golden-hour sky. **The entire world is generated
in C++ at runtime**, so the project runs from a completely empty level with **no
imported art assets required**. When you're ready, you drop your own Sketchfab
models in on top (guide near the bottom).

> Honest note: I wrote this without a copy of Unreal to compile against, so treat
> it as a strong starting point rather than guaranteed-clean-on-first-build. The
> code is idiomatic UE 5.3 C++; if the first compile flags something, it'll almost
> always be a one-line include or API-name tweak. Nothing here needs Blueprints.

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
   - **File → Save Current Level As** → make a folder `Maps`, name it `Millhaven`.
     (Final path: `Content/Maps/Millhaven`.) This matches the default-map setting
     already in `Config/DefaultEngine.ini`, so nothing else to configure.
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
| `ProcMeshLib.h` | Tiny geometry kit: `AddBox`, `AddCone`, `AddCylinder`, etc. (flat-shaded low-poly). |
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
     #include "UObject/ConstructorHelpers.h"

     void AMillhavenWorldGen::AddTree(float AX, float AY, float Scale)
     {
         static UStaticMesh* TreeMesh =
             LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Trees/SM_PineTree.SM_PineTree"));
         if (!TreeMesh) return;
         UStaticMeshComponent* SM = NewObject<UStaticMeshComponent>(this);
         SM->RegisterComponent();
         SM->SetStaticMesh(TreeMesh);
         SM->SetWorldLocation(GroundPos(AX, AY, 0.f));
         SM->SetWorldScale3D(FVector(Scale));
         SM->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepWorldTransform);
     }
     ```

   The same pattern replaces bushes, rocks, the well, buildings, the castle, etc.
   — keep the `GroundPos(...)` placement so props still sit on the terrain.

## 7. Known limitations / first-tweak checklist

- **Player falls through / floats:** if the terrain collision lags a frame, the
  spawn safety in `MillhavenCharacter::BeginPlay` lifts you to
  `TerrainHeight + 120cm`. If needed, raise that value.
- **Trees are walk-through** (decorative, no collision) so you never snag on
  foliage. Add collision later per the asset-swap section if you want solid trunks.
- **Sky/lighting** uses a real-time Sky Light + Sky Atmosphere. If the sky is
  black, add a **Sky Atmosphere** actor to the level manually, or lower reliance on
  real-time capture in `BuildEnvironmentLighting()`.
- **Limb animation** is a simple body-bob (Procedural Mesh can't rotate individual
  sections). For real walk cycles, swap the avatar for a Skeletal Mesh later.

Have fun in Millhaven.
