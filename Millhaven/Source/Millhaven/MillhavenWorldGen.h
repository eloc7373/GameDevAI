#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProcMeshLib.h"
#include "MillhavenWorldGen.generated.h"

class UProceduralMeshComponent;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UPointLightComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * One village house. Declared here so BuildStructures() and the HUD minimap
 * read from the same table instead of two hand-synced copies (they drifted
 * apart once already).
 */
struct FMillhavenBuildingDef
{
	FVector2D At;        // design metres from the village centre
	float W = 4.f;       // footprint width, metres
	float D = 4.f;       // footprint depth, metres
	FColor Wall = FColor::White;
	FColor Roof = FColor::White;
};

/**
 * Spawned by the GameMode into an (otherwise empty) level. Builds the whole
 * world at BeginPlay: lighting, terrain, village, harbour, cave, forest and
 * NPCs - all procedural geometry, so NO content assets are required to run.
 *
 * Horizontal design coords are in "metres"; everything is x100 into UE cm.
 * TerrainHeight is a pure function so object placement and the player spawn
 * agree with the mesh without any raycasts.
 */
UCLASS()
class AMillhavenWorldGen : public AActor
{
	GENERATED_BODY()

public:
	AMillhavenWorldGen();
	virtual void Tick(float DeltaTime) override;

	/** Ground height (cm) at a world XY (cm). Deterministic. */
	static float TerrainHeight(float WX, float WY);

	/** Region name for the HUD location banner. */
	static FString BiomeAt(float WX, float WY);

	/** True once the world geometry has been generated. */
	bool IsWorldBuilt() const { return bWorldBuilt; }

	/**
	 * Sea level, in UE cm. Water tiles are emitted at this height and only
	 * where the ground is below it, so the shoreline follows the terrain
	 * instead of being a hard-edged square. BuildTerrain() colours the sand
	 * fringe against the same number.
	 */
	static constexpr float WaterlineCm = 5.f;

	/**
	 * How far under the waterline the ground must sit before a water tile is
	 * emitted. The margin is what keeps ground at exactly z=0 - the whole flat
	 * village disc - from flooding. Anything asking "is this spot water?" must
	 * use this same threshold, or the shoreline it computes will not be the
	 * one the player can see.
	 */
	static constexpr float MinWaterDepthCm = 20.f;

	// The rectangle BuildWater() tiles, in design metres. Low ground outside it
	// is simply low ground: the cave hollow in the northern hills sits 3.2m
	// below the waterline and is bone dry, because no tile is ever laid there.
	static constexpr float WaterMinAX = -60.f;
	static constexpr float WaterMaxAX =  -4.f;
	static constexpr float WaterMinAY =   4.f;
	static constexpr float WaterMaxAY =  60.f;

	/** True where BuildWater() lays a tile. The one definition of "wet". */
	static bool IsWaterAt(float AX, float AY)
	{
		if (AX < WaterMinAX || AX > WaterMaxAX || AY < WaterMinAY || AY > WaterMaxAY)
		{
			return false;
		}
		return TerrainHeight(AX * 100.f, AY * 100.f) <= WaterlineCm - MinWaterDepthCm;
	}

	/** Top surface of the pier decking, UE cm. Things standing on it lift here. */
	static constexpr float DockDeckTopCm = 39.f;

	/** The moored boat out in the bay - where "Trouble in the Bay" sends you. */
	static FVector2D BayWatchM() { return FVector2D(-20.0, 30.0); }

	// --- The cave chamber, in design metres --------------------------------
	// Walls and a roof over the natural terrain, not a flat room: the ground
	// here falls about 3m across the footprint, which makes a descending cave
	// for free and would fight any floor slab.
	static constexpr float CaveMinAX = -17.0f;
	static constexpr float CaveMaxAX =  -4.5f;
	static constexpr float CaveMinAY = -45.5f;
	static constexpr float CaveMaxAY = -34.5f;
	/** The doorway gap in the east wall, lining up with the carved mouth. */
	static constexpr float CaveDoorMinAY = -37.5f;
	static constexpr float CaveDoorMaxAY = -34.5f;
	/** Underside of the roof slab, UE cm. Clears the highest floor by ~3m. */
	static constexpr float CaveRoofCm = 40.f;

	// --- Time of day --------------------------------------------------------

	/** Real seconds for one in-game day. */
	static constexpr float DayLengthSeconds = 720.f;

	/** Current hour, 0..24. */
	float GetTimeOfDay() const { return TimeOfDay; }

	/** True while the sun is down - drives the cave glow and the night palette. */
	bool IsNight() const { return TimeOfDay < 6.f || TimeOfDay >= 19.f; }

	/** "07:20" for the HUD clock. */
	static FString ClockString(float Hours);

	/** "Dawn", "Morning", ... for the HUD. */
	static FString PhaseName(float Hours);

	/** The village houses. Single source of truth; see FMillhavenBuildingDef. */
	static const TArray<FMillhavenBuildingDef>& VillageBuildings();

	// Landmark positions in design metres. Shared with the character's quest
	// arrival checks so the two cannot drift apart.
	static FVector2D CaveMouthM() { return FVector2D(-5.0, -36.0); }
	static FVector2D DockM()      { return FVector2D(-25.0, 26.0); }

	/**
	 * Resolves the base material every procedural mesh is tinted from. Static
	 * so the NPC and the player avatar share one resolution policy.
	 */
	static UMaterialInterface* ResolveBaseMaterial();

	// Pure helpers. Public because they are part of the contract the rest of
	// the game and the automation tests are written against, not just an
	// implementation detail of the builders below.

	/** Deterministic hash-noise in [0,1). Seeds must be mixed, never multiplied. */
	static float Prng(float Seed);

	/** Design metres -> UE cm, dropped onto the terrain. */
	static FVector GroundPos(float AX, float AY, float LiftCm = 0.f);

protected:
	virtual void BeginPlay() override;

	// Components
	UPROPERTY() USceneComponent* SceneRoot = nullptr;
	UPROPERTY() UProceduralMeshComponent* Mesh = nullptr;      // solid: terrain + structures (collision)
	UPROPERTY() UProceduralMeshComponent* DecorMesh = nullptr; // foliage/scenery (no collision)
	UPROPERTY() UProceduralMeshComponent* WaterMesh = nullptr; // bobs
	UPROPERTY() UProceduralMeshComponent* CloudMesh = nullptr; // drifts

	UPROPERTY() UDirectionalLightComponent* Sun = nullptr;
	UPROPERTY() USkyLightComponent* Sky = nullptr;
	UPROPERTY() USkyAtmosphereComponent* Atmosphere = nullptr;
	UPROPERTY() UExponentialHeightFogComponent* Fog = nullptr;
	/** Lit only after dark, so Captain Wren's "the cave mouth glows at
	 *  midnight" is literally true rather than just flavour text. */
	UPROPERTY() UPointLightComponent* CaveGlow = nullptr;

	/** Cached so the engine material is not re-resolved (and stays GC-rooted). */
	UPROPERTY() UMaterialInterface* BaseMaterial = nullptr;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> MIDs;

	// Color-batched geometry
	TArray<TPair<FColor, FMillhavenMeshBatch>> Batches;
	FMillhavenMeshBatch& B(const FColor& Color);
	void CommitBatches(UProceduralMeshComponent* Target, bool bCollision);

	UMaterialInterface* GetBaseMaterial();
	UMaterialInstanceDynamic* MakeColorMID(const FColor& C);

	// Builders
	void BuildWorld();
	void BuildEnvironmentLighting();
	void BuildTerrain();
	void BuildStructures();
	void BuildVegetation();
	void BuildScenery();
	void BuildWater();
	void BuildClouds();
	void BuildCaveChamber();
	void SpawnNPCs();
	void SpawnPickups();

	/** Advances the clock and re-tints sun, sky and fog to match. */
	void UpdateDayNight(float DeltaTime);

	// Helpers
	void AddTree(float AX, float AY, float Scale);
	void AddBuilding(float AX, float AY, float W, float D,
	                 const FColor& WallColor, const FColor& RoofColor);
	void AddFenceRun(const TArray<FVector2D>& Points);
	void AddStall(float AX, float AY);

	float TimeAccum = 0.f;
	/** Starts mid-morning so the first thing a player sees is daylight. */
	float TimeOfDay = 8.f;
	bool bWorldBuilt = false;
};
