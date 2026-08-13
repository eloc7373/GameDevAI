#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "MillhavenNPC.generated.h"

class UProceduralMeshComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

// Plain structs (no reflection needed - built and consumed in C++ only)
struct FDlgOption
{
	FString Label;
	FName Next;              // "x" closes the dialogue

	// --- Quest effects, applied when this option is chosen ---

	/** Non-empty starts (or retargets) a quest with this display name. */
	FString QuestName;
	FString QuestObjective;
	/** Stable id for the quest above. Falls back to QuestName if left unset. */
	FName QuestId;
	/** Marks this quest id complete. */
	FName CompletesQuest;

	// --- Requirements. An option is only offered when all of these pass. ---

	/** Only show while this quest is started and not yet complete. */
	FName RequiresQuest;
	/** Only show once this quest is complete. */
	FName RequiresQuestComplete;
	/** Hide once this quest exists at all - stops a quest being accepted twice. */
	FName ForbidsQuest;
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

	/**
	 * Full form, for options that carry requirements or complete a quest.
	 * Takes a prepared struct rather than returning a reference into the node's
	 * option array - a reference there would dangle as soon as the next
	 * AddOption grew it, which is the same trap UInputMappingContext::MapKey
	 * sets.
	 */
	void AddOption(FName NodeKey, const FDlgOption& Option);

	/**
	 * Logs dangling option targets, a missing "start" node, and unreachable
	 * nodes. Called once per NPC after its tree is built.
	 */
	void ValidateDialogue() const;

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
	/** Blocks the player so villagers are solid rather than walk-through. */
	UPROPERTY() UCapsuleComponent* Collision = nullptr;
	UPROPERTY() UProceduralMeshComponent* LegsMesh = nullptr;
	UPROPERTY() UProceduralMeshComponent* BodyMesh = nullptr;
	UPROPERTY() UProceduralMeshComponent* HeadMesh = nullptr;

	UPROPERTY() UMaterialInterface* BaseMaterial = nullptr;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> MIDs;

	TMap<FName, FDlgNode> Dialogue;
	float BobPhase = 0.f;
};
