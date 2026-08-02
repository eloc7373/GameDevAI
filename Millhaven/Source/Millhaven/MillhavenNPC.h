#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "MillhavenNPC.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

// Plain structs (no reflection needed - built and consumed in C++ only)
struct FDlgOption
{
	FString Label;
	FName Next;              // "x" closes the dialogue
	FString QuestName;       // optional: updates quest tracker when chosen
	FString QuestObjective;
};

struct FDlgNode
{
	FString Text;
	TArray<FDlgOption> Options;
};

UCLASS()
class AMillhavenNPC : public AActor
{
	GENERATED_BODY()

public:
	AMillhavenNPC();
	virtual void Tick(float DeltaTime) override;

	/** Builds the low-poly body and stores identity. Call right after spawning. */
	void Init(const FString& InName, const FString& InRole,
	          const FColor& BodyColor, const FColor& SkinColor, const FColor& HairColor);

	void AddNode(FName Key, const FString& Text);
	void AddOption(FName NodeKey, const FString& Label, FName Next,
	               const FString& QuestName = TEXT(""), const FString& QuestObjective = TEXT(""));

	const FDlgNode* GetNode(FName Key) const { return Dialogue.Find(Key); }
	void FacePoint(const FVector& WorldPoint);

	FString NpcName;
	/** Named NpcRole, not Role: a plain "Role" would hide AActor::Role. */
	FString NpcRole;

	/**
	 * Global registry so the player/HUD can find NPCs without iteration order
	 * issues. Weak pointers, so a destroyed or GC'd NPC never leaves a dangling
	 * entry even if EndPlay is skipped (level teardown, PIE stop).
	 */
	static TArray<TWeakObjectPtr<AMillhavenNPC>> All;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() UProceduralMeshComponent* LegsMesh = nullptr;
	UPROPERTY() UProceduralMeshComponent* BodyMesh = nullptr;
	UPROPERTY() UProceduralMeshComponent* HeadMesh = nullptr;

	UPROPERTY() UMaterialInterface* BaseMaterial = nullptr;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> MIDs;

	TMap<FName, FDlgNode> Dialogue;
	float BobPhase = 0.f;
};
