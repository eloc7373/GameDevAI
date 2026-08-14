#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "MillhavenCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UInputAction;
class UInputMappingContext;
class AMillhavenNPC;
struct FDlgOption;

/**
 * One tracked quest. Identified by a stable FName id so dialogue in two
 * different NPCs can refer to the same quest; the display strings are free
 * text the dialogue supplies.
 */
struct FMillhavenQuest
{
	FName Id;
	FString Name;
	FString Objective;
	bool bComplete = false;

	/**
	 * Optional gathering goal. When set, the quest tracks inventory and shows
	 * progress; a quest with bAutoCompleteOnItems also finishes by itself once
	 * the count is met, rather than waiting to be handed in.
	 */
	FName RequiredItem;
	int32 RequiredCount = 0;
	bool bAutoCompleteOnItems = false;
};

/**
 * Third-person explorer for UE 5.8. Uses Enhanced Input, with all Input Actions
 * and the Input Mapping Context built in C++ - so the project needs NO input
 * assets created in the editor. Procedural low-poly avatar, spring-arm camera,
 * proximity NPC interaction, and quest/dialogue state the HUD reads.
 */
UCLASS()
class AMillhavenCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AMillhavenCharacter();
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

	// HUD-facing state
	bool IsInDialogue() const { return ActiveNPC != nullptr; }
	AMillhavenNPC* GetActiveNPC() const { return ActiveNPC; }
	FName GetCurrentNode() const { return CurrentNode; }
	AMillhavenNPC* GetNearbyNPC() const;

	/** How close (cm, 2D) the player must be for the interact prompt to appear. */
	static constexpr float InteractRangeCm = 320.f;

	// --- Quest tracker ---

	/** Every quest started this session, in the order they were taken. */
	const TArray<FMillhavenQuest>& GetQuests() const { return Quests; }

	bool IsQuestActive(FName Id) const;
	bool IsQuestComplete(FName Id) const;

	/** Starts the quest, or retargets its objective if it is already running. */
	void StartOrUpdateQuest(FName Id, const FString& Name, const FString& Objective);
	void CompleteQuest(FName Id);

	/** Attaches a gathering goal to a quest that is already running. */
	void SetQuestItemGoal(FName Id, FName Item, int32 Count, bool bAutoComplete);

	// --- Inventory ---

	const TMap<FName, int32>& GetInventory() const { return Inventory; }
	int32 GetItemCount(FName Item) const;
	void AddItem(FName Item, const FString& DisplayName, int32 Count = 1);
	/** Removes Count of Item. Returns false and changes nothing if short. */
	bool ConsumeItem(FName Item, int32 Count);
	/** Display name for an item the player has seen, else the raw id. */
	FString ItemDisplayName(FName Item) const;

	/** Transient "+1 Golden Wheat" line for the HUD; empty when nothing recent. */
	FString PickupToast;
	float PickupToastTimer = 0.f;

	/** True when an option's requirements pass and it should be offered. */
	bool IsOptionAvailable(const FDlgOption& Opt) const;

	/**
	 * Options on the current node that pass their requirements, in the order
	 * they are displayed. The HUD renders this list and SelectOption() indexes
	 * into it, so the numbers on screen always match the keys 1-4.
	 */
	TArray<const FDlgOption*> GetAvailableOptions() const;

protected:
	virtual void BeginPlay() override;

	// Components
	UPROPERTY() USpringArmComponent* SpringArm = nullptr;
	UPROPERTY() UCameraComponent* Camera = nullptr;
	UPROPERTY() UProceduralMeshComponent* Avatar = nullptr;
	UPROPERTY() UMaterialInterface* BaseMaterial = nullptr;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> MIDs;

	// Enhanced Input - created at runtime in C++, no .uasset files required.
	// These are UDataAsset types, so they are built with NewObject in BeginPlay
	// rather than CreateDefaultSubobject (which is for CDO component subobjects).
	UPROPERTY() UInputMappingContext* InputMapping = nullptr;
	UPROPERTY() UInputAction* MoveForwardAction = nullptr;
	UPROPERTY() UInputAction* MoveRightAction = nullptr;
	UPROPERTY() UInputAction* TurnAction = nullptr;
	UPROPERTY() UInputAction* LookUpAction = nullptr;
	UPROPERTY() UInputAction* InteractAction = nullptr;
	UPROPERTY() UInputAction* CloseAction = nullptr;
	UPROPERTY() UInputAction* Dialogue1Action = nullptr;
	UPROPERTY() UInputAction* Dialogue2Action = nullptr;
	UPROPERTY() UInputAction* Dialogue3Action = nullptr;
	UPROPERTY() UInputAction* Dialogue4Action = nullptr;
	UPROPERTY() UInputAction* JumpAction = nullptr;
	UPROPERTY() UInputAction* SprintAction = nullptr;

	/** Creates the Input Actions + mapping context. Safe to call more than once. */
	void BuildInputAssets();

	/** Pushes the mapping context onto the local player and clamps camera pitch. */
	void RegisterInputMapping();

	void BuildAvatar();

	/** Lifts the pawn back onto the terrain if it ever ends up under it. */
	void EnforceGroundSafety();

	/** Completes any active quest whose landmark the player has reached. */
	void UpdateLocationQuests();

	/** Sweeps up nearby pickups and files them into the inventory. */
	void SweepPickups();

	/** Finishes any auto-complete gathering quest whose count is now met. */
	void UpdateItemQuests();

	FMillhavenQuest* FindQuest(FName Id);
	const FMillhavenQuest* FindQuest(FName Id) const;

	// Input handlers (Enhanced Input signatures)
	void MoveForward(const FInputActionValue& Value);
	void MoveRight(const FInputActionValue& Value);
	void Turn(const FInputActionValue& Value);
	void LookUp(const FInputActionValue& Value);
	void OnInteract();
	void OnClose();
	void OnOption1();
	void OnOption2();
	void OnOption3();
	void OnOption4();
	void OnJumpStart();
	void OnJumpStop();
	void OnSprintStart();
	void OnSprintStop();
	void SelectOption(int32 Index);

	static constexpr float WalkSpeed = 560.f;
	static constexpr float SprintSpeed = 900.f;

	// Dialogue
	UPROPERTY() AMillhavenNPC* ActiveNPC = nullptr;
	FName CurrentNode = NAME_None;

	/** Plain data, not a UObject - no reflection or GC involvement needed. */
	TArray<FMillhavenQuest> Quests;

	TMap<FName, int32> Inventory;
	/** Pretty names, learned from the pickups themselves as they are collected. */
	TMap<FName, FString> ItemNames;

	float WalkPhase = 0.f;
};
