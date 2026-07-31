#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MillhavenNPC.generated.h"

class UProceduralMeshComponent;
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
	FString Role;

	/** Global registry so the player/HUD can find NPCs without iteration order issues. */
	static TArray<AMillhavenNPC*> All;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() UProceduralMeshComponent* LegsMesh = nullptr;
	UPROPERTY() UProceduralMeshComponent* BodyMesh = nullptr;
	UPROPERTY() UProceduralMeshComponent* HeadMesh = nullptr;

	UPROPERTY() TArray<UMaterialInstanceDynamic*> MIDs;

	TMap<FName, FDlgNode> Dialogue;
	float BobPhase = 0.f;
};
