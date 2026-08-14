#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "MillhavenPickup.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Which little procedural shape a pickup is drawn as. */
UENUM()
enum class EMillhavenPickupShape : uint8
{
	Sheaf,      // bundled stalks - wheat
	Leaf,       // flat frond - silverleaf
	Crystal,    // double cone - gems
	Pearl,      // small sphere-ish - shellwater pearls
};

/**
 * A collectable lying in the world. Spins and bobs so it reads as an item
 * rather than scenery, and is picked up by walking near it - there is no
 * interact key, because stopping to press E on every stalk of wheat is not
 * fun.
 *
 * Like AMillhavenNPC this keeps a process-global registry of weak pointers so
 * the player can find pickups without an overlap component per item. Every
 * read of it MUST filter by GetWorld() - it spans PIE instances.
 */
UCLASS()
class AMillhavenPickup : public AActor
{
	GENERATED_BODY()

public:
	AMillhavenPickup();
	virtual void Tick(float DeltaTime) override;

	/** Builds the body and stores identity. Call right after spawning. */
	void Init(FName InItemId, const FString& InDisplayName,
	          EMillhavenPickupShape Shape, const FColor& Tint);

	/** Takes the item. Returns false if it was already collected. */
	bool Collect();
	bool IsCollected() const { return bCollected; }

	/** How close (cm, 2D) the player must be to sweep an item up. */
	static constexpr float PickupRangeCm = 170.f;

	FName ItemId;
	FString DisplayName;

	static TArray<TWeakObjectPtr<AMillhavenPickup>> All;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() UProceduralMeshComponent* Mesh = nullptr;
	UPROPERTY() UMaterialInterface* BaseMaterial = nullptr;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> MIDs;

	bool bCollected = false;
	float BobPhase = 0.f;
};
