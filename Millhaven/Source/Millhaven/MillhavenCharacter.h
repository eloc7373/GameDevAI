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

	FString QuestName = TEXT("The Everloaf");
	FString QuestObjective = TEXT("Explore the village and speak to someone");

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

	/** Creates the Input Actions + mapping context. Safe to call more than once. */
	void BuildInputAssets();

	/** Pushes the mapping context onto the local player and clamps camera pitch. */
	void RegisterInputMapping();

	void BuildAvatar();

	/** Lifts the pawn back onto the terrain if it ever ends up under it. */
	void EnforceGroundSafety();

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
	void SelectOption(int32 Index);

	// Dialogue
	UPROPERTY() AMillhavenNPC* ActiveNPC = nullptr;
	FName CurrentNode = NAME_None;

	float WalkPhase = 0.f;
};
