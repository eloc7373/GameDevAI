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
class UMaterialInterface;
class UMaterialInstanceDynamic;

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
	void SpawnNPCs();

	// Helpers
	void AddTree(float AX, float AY, float Scale);
	void AddBuilding(float AX, float AY, float W, float D,
	                 const FColor& WallColor, const FColor& RoofColor);
	void AddFenceRun(const TArray<FVector2D>& Points);
	void AddStall(float AX, float AY);

	static float Prng(float Seed);
	static FVector GroundPos(float AX, float AY, float LiftCm = 0.f); // metres -> UE cm

	float TimeAccum = 0.f;
	bool bWorldBuilt = false;
};
