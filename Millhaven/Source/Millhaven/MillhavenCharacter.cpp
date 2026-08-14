#include "MillhavenCharacter.h"
#include "MillhavenNPC.h"
#include "MillhavenPickup.h"
#include "MillhavenWorldGen.h"
#include "ProcMeshLib.h"

#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

// Enhanced Input (UE 5.8)
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

static UMaterialInstanceDynamic* CharColorMID(UObject* Outer, UMaterialInterface* Base, const FColor& C)
{
	if (!Base)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
	if (MID)
	{
		MID->SetVectorParameterValue(FName("Color"), FLinearColor::FromSRGBColor(C));
	}
	return MID;
}

static void CharCommitSection(UProceduralMeshComponent* M, int32 Sec, FMillhavenMeshBatch& Bat,
                              const FColor& Tint, UMaterialInstanceDynamic* MID)
{
	TArray<FLinearColor> Cols;
	Cols.Init(FLinearColor::FromSRGBColor(Tint), Bat.V.Num());
	M->CreateMeshSection_LinearColor(Sec, Bat.V, Bat.T, Bat.N, Bat.UV, Cols,
		TArray<FProcMeshTangent>(), false);
	if (MID)
	{
		M->SetMaterial(Sec, MID);
	}
}

AMillhavenCharacter::AMillhavenCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(38.f, 92.f);

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = true;
		Move->RotationRate = FRotator(0.f, 640.f, 0.f);
		Move->MaxWalkSpeed = WalkSpeed;
		Move->JumpZVelocity = 420.f;
		Move->AirControl = 0.2f;
	}

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 520.f;
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.f;
	SpringArm->SetRelativeLocation(FVector(0.0, 0.0, 90.0));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->bUsePawnControlRotation = false;

	Avatar = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Avatar"));
	Avatar->SetupAttachment(RootComponent);
	Avatar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Avatar->SetRelativeLocation(FVector(0.0, 0.0, -92.0)); // feet at capsule bottom
}

// ---------------------------------------------------------------------------
// Enhanced Input assets, built entirely in C++ at runtime.
//
// UInputAction and UInputMappingContext derive from UDataAsset, not UActorComponent,
// so they are created with NewObject rather than CreateDefaultSubobject. The
// UPROPERTY() members above keep them alive against garbage collection.
// ---------------------------------------------------------------------------
void AMillhavenCharacter::BuildInputAssets()
{
	if (InputMapping)
	{
		return; // already built
	}

	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type) -> UInputAction*
	{
		UInputAction* A = NewObject<UInputAction>(this, FName(Name));
		A->ValueType = Type;
		return A;
	};

	MoveForwardAction = MakeAction(TEXT("IA_MoveForward"), EInputActionValueType::Axis1D);
	MoveRightAction   = MakeAction(TEXT("IA_MoveRight"),   EInputActionValueType::Axis1D);
	TurnAction        = MakeAction(TEXT("IA_Turn"),        EInputActionValueType::Axis1D);
	LookUpAction      = MakeAction(TEXT("IA_LookUp"),      EInputActionValueType::Axis1D);
	InteractAction    = MakeAction(TEXT("IA_Interact"),    EInputActionValueType::Boolean);
	CloseAction       = MakeAction(TEXT("IA_Close"),       EInputActionValueType::Boolean);
	Dialogue1Action   = MakeAction(TEXT("IA_Dialogue1"),   EInputActionValueType::Boolean);
	Dialogue2Action   = MakeAction(TEXT("IA_Dialogue2"),   EInputActionValueType::Boolean);
	Dialogue3Action   = MakeAction(TEXT("IA_Dialogue3"),   EInputActionValueType::Boolean);
	Dialogue4Action   = MakeAction(TEXT("IA_Dialogue4"),   EInputActionValueType::Boolean);
	JumpAction        = MakeAction(TEXT("IA_Jump"),        EInputActionValueType::Boolean);
	SprintAction      = MakeAction(TEXT("IA_Sprint"),      EInputActionValueType::Boolean);

	InputMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Millhaven"));

	// MapKey returns a reference into the context's Mappings array, so the
	// modifier must be attached before the next MapKey call can reallocate it.
	auto Map = [this](UInputAction* Action, const FKey& Key, bool bNegate)
	{
		FEnhancedActionKeyMapping& M = InputMapping->MapKey(Action, Key);
		if (bNegate)
		{
			M.Modifiers.Add(NewObject<UInputModifierNegate>(InputMapping));
		}
	};

	// Movement (WASD + arrows). Negative directions get a Negate modifier.
	Map(MoveForwardAction, EKeys::W,     false);
	Map(MoveForwardAction, EKeys::Up,    false);
	Map(MoveForwardAction, EKeys::S,     true);
	Map(MoveForwardAction, EKeys::Down,  true);

	Map(MoveRightAction,   EKeys::D,     false);
	Map(MoveRightAction,   EKeys::Right, false);
	Map(MoveRightAction,   EKeys::A,     true);
	Map(MoveRightAction,   EKeys::Left,  true);

	// Look. MouseY is negated so pushing the mouse forward looks up.
	Map(TurnAction,   EKeys::MouseX, false);
	Map(LookUpAction, EKeys::MouseY, true);

	// Buttons
	Map(InteractAction,  EKeys::E,        false);
	Map(CloseAction,     EKeys::Escape,   false);
	Map(Dialogue1Action, EKeys::One,      false);
	Map(Dialogue2Action, EKeys::Two,      false);
	Map(Dialogue3Action, EKeys::Three,    false);
	Map(Dialogue4Action, EKeys::Four,     false);
	Map(JumpAction,      EKeys::SpaceBar, false);
	Map(SprintAction,    EKeys::LeftShift, false);
}

void AMillhavenCharacter::RegisterInputMapping()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->ViewPitchMin = -55.f;
		PC->PlayerCameraManager->ViewPitchMax = 18.f;
	}

	if (ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			Subsys->ClearAllMappings();
			Subsys->AddMappingContext(InputMapping, 0);
		}
	}
}

// SetupPlayerInputComponent can run before BeginPlay, so the assets are built
// from whichever of the two happens first.
void AMillhavenCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	BuildInputAssets();
	RegisterInputMapping();
}

void AMillhavenCharacter::BeginPlay()
{
	Super::BeginPlay();

	BuildAvatar();

	// Deterministic ground placement (works even before the terrain mesh exists)
	FVector L = GetActorLocation();
	L.Z = AMillhavenWorldGen::TerrainHeight((float)L.X, (float)L.Y) + 120.0;
	SetActorLocation(L);

	// The opening objective, so the tracker is never empty on a fresh start.
	StartOrUpdateQuest(FName("Everloaf"), TEXT("The Everloaf"),
		TEXT("Explore the village and speak to someone"));

	BuildInputAssets();
	RegisterInputMapping();
}

void AMillhavenCharacter::BuildAvatar()
{
	if (!BaseMaterial)
	{
		// Shared resolution policy - see AMillhavenWorldGen::ResolveBaseMaterial.
		BaseMaterial = AMillhavenWorldGen::ResolveBaseMaterial();
	}

	int32 s = 0;
	auto Part = [&](const FVector& c, const FVector& size, const FColor& col)
	{
		FMillhavenMeshBatch B;
		B.AddBox(c, size);
		UMaterialInstanceDynamic* MID = CharColorMID(this, BaseMaterial, col);
		if (MID)
		{
			MIDs.Add(MID);
		}
		CharCommitSection(Avatar, s++, B, col, MID);
	};
	Part(FVector(0.0, 0.0, 90.0),    FVector(28.0, 48.0, 70.0), FColor(58, 106, 170));  // body
	Part(FVector(0.0, 0.0, 148.0),   FVector(42.0, 42.0, 42.0), FColor(240, 192, 128)); // head
	Part(FVector(0.0, 0.0, 168.0),   FVector(44.0, 44.0, 18.0), FColor(176, 112, 48));  // hair
	Part(FVector(-34.0, 0.0, 88.0),  FVector(18.0, 18.0, 55.0), FColor(42, 74, 128));   // L arm
	Part(FVector( 34.0, 0.0, 88.0),  FVector(18.0, 18.0, 55.0), FColor(42, 74, 128));   // R arm
	Part(FVector(-14.0, 0.0, 26.0),  FVector(20.0, 20.0, 52.0), FColor(30, 46, 74));    // L leg
	Part(FVector( 14.0, 0.0, 26.0),  FVector(20.0, 20.0, 52.0), FColor(30, 46, 74));    // R leg
	Part(FVector(0.0, -24.0, 90.0),  FVector(20.0, 34.0, 52.0), FColor(176, 96, 48));   // backpack
}

void AMillhavenCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	BuildInputAssets();

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &AMillhavenCharacter::MoveForward);
		EIC->BindAction(MoveRightAction,   ETriggerEvent::Triggered, this, &AMillhavenCharacter::MoveRight);
		EIC->BindAction(TurnAction,        ETriggerEvent::Triggered, this, &AMillhavenCharacter::Turn);
		EIC->BindAction(LookUpAction,      ETriggerEvent::Triggered, this, &AMillhavenCharacter::LookUp);

		EIC->BindAction(InteractAction,  ETriggerEvent::Started, this, &AMillhavenCharacter::OnInteract);
		EIC->BindAction(CloseAction,     ETriggerEvent::Started, this, &AMillhavenCharacter::OnClose);
		EIC->BindAction(Dialogue1Action, ETriggerEvent::Started, this, &AMillhavenCharacter::OnOption1);
		EIC->BindAction(Dialogue2Action, ETriggerEvent::Started, this, &AMillhavenCharacter::OnOption2);
		EIC->BindAction(Dialogue3Action, ETriggerEvent::Started, this, &AMillhavenCharacter::OnOption3);
		EIC->BindAction(Dialogue4Action, ETriggerEvent::Started, this, &AMillhavenCharacter::OnOption4);

		EIC->BindAction(JumpAction, ETriggerEvent::Started,   this, &AMillhavenCharacter::OnJumpStart);
		EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &AMillhavenCharacter::OnJumpStop);

		EIC->BindAction(SprintAction, ETriggerEvent::Started,   this, &AMillhavenCharacter::OnSprintStart);
		EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AMillhavenCharacter::OnSprintStop);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Millhaven: input component is not a UEnhancedInputComponent. ")
			TEXT("Check DefaultInputComponentClass in Config/DefaultInput.ini."));
	}
}

void AMillhavenCharacter::MoveForward(const FInputActionValue& Value)
{
	const float V = Value.Get<float>();
	if (V != 0.f && !IsInDialogue() && Controller)
	{
		const FRotator Yaw(0.f, (float)Controller->GetControlRotation().Yaw, 0.f);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V);
	}
}

void AMillhavenCharacter::MoveRight(const FInputActionValue& Value)
{
	const float V = Value.Get<float>();
	if (V != 0.f && !IsInDialogue() && Controller)
	{
		const FRotator Yaw(0.f, (float)Controller->GetControlRotation().Yaw, 0.f);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V);
	}
}

void AMillhavenCharacter::Turn(const FInputActionValue& Value)
{
	if (!IsInDialogue())
	{
		AddControllerYawInput(Value.Get<float>() * 1.4f);
	}
}

void AMillhavenCharacter::LookUp(const FInputActionValue& Value)
{
	if (!IsInDialogue())
	{
		AddControllerPitchInput(Value.Get<float>() * 1.2f);
	}
}

void AMillhavenCharacter::OnJumpStart() { if (!IsInDialogue()) Jump(); }
void AMillhavenCharacter::OnJumpStop()  { StopJumping(); }

void AMillhavenCharacter::OnSprintStart()
{
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = SprintSpeed;
	}
}

void AMillhavenCharacter::OnSprintStop()
{
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = WalkSpeed;
	}
}

// ---------------------------------------------------------------------------
// Inventory
// ---------------------------------------------------------------------------
int32 AMillhavenCharacter::GetItemCount(FName Item) const
{
	const int32* Found = Inventory.Find(Item);
	return Found ? *Found : 0;
}

FString AMillhavenCharacter::ItemDisplayName(FName Item) const
{
	if (const FString* Found = ItemNames.Find(Item))
	{
		return *Found;
	}
	return Item.ToString();
}

void AMillhavenCharacter::AddItem(FName Item, const FString& DisplayName, int32 Count)
{
	if (Item.IsNone() || Count <= 0)
	{
		return;
	}
	Inventory.FindOrAdd(Item) += Count;
	if (!DisplayName.IsEmpty())
	{
		ItemNames.Add(Item, DisplayName);
	}

	PickupToast = FString::Printf(TEXT("+%d  %s  (%d)"),
		Count, *ItemDisplayName(Item), GetItemCount(Item));
	PickupToastTimer = 2.2f;
}

bool AMillhavenCharacter::ConsumeItem(FName Item, int32 Count)
{
	if (Item.IsNone() || Count <= 0)
	{
		return true;
	}
	int32* Have = Inventory.Find(Item);
	if (!Have || *Have < Count)
	{
		return false;   // short: change nothing
	}
	*Have -= Count;
	if (*Have <= 0)
	{
		Inventory.Remove(Item);
	}
	return true;
}

void AMillhavenCharacter::SweepPickups()
{
	const FVector Here = GetActorLocation();
	for (const TWeakObjectPtr<AMillhavenPickup>& Weak : AMillhavenPickup::All)
	{
		AMillhavenPickup* P = Weak.Get();
		// Process-global registry - see the note on AMillhavenPickup::All.
		if (!P || P->IsCollected() || P->GetWorld() != GetWorld())
		{
			continue;
		}
		if ((float)FVector::Dist2D(P->GetActorLocation(), Here) > AMillhavenPickup::PickupRangeCm)
		{
			continue;
		}
		if (P->Collect())
		{
			AddItem(P->ItemId, P->DisplayName, 1);
		}
	}
}

void AMillhavenCharacter::UpdateItemQuests()
{
	for (FMillhavenQuest& Q : Quests)
	{
		if (Q.bComplete || !Q.bAutoCompleteOnItems || Q.RequiredItem.IsNone())
		{
			continue;
		}
		if (GetItemCount(Q.RequiredItem) >= Q.RequiredCount)
		{
			CompleteQuest(Q.Id);
		}
	}
}

AMillhavenNPC* AMillhavenCharacter::GetNearbyNPC() const
{
	AMillhavenNPC* Best = nullptr;
	float BestD = InteractRangeCm;
	const FVector Here = GetActorLocation();
	for (const TWeakObjectPtr<AMillhavenNPC>& Weak : AMillhavenNPC::All)
	{
		AMillhavenNPC* N = Weak.Get();
		// The registry is process-global. Without the world check an NPC in a
		// second PIE instance (or an editor preview world) is interactable
		// from this one.
		if (!N || N->GetWorld() != GetWorld())
		{
			continue;
		}
		const float D = (float)FVector::Dist2D(N->GetActorLocation(), Here);
		if (D < BestD)
		{
			BestD = D;
			Best = N;
		}
	}
	return Best;
}

void AMillhavenCharacter::OnInteract()
{
	if (IsInDialogue())
	{
		return;
	}
	if (AMillhavenNPC* N = GetNearbyNPC())
	{
		ActiveNPC = N;
		CurrentNode = "start";
		N->FacePoint(GetActorLocation());

		// The opening objective is "explore the village and speak to someone",
		// so this is the moment it is met. Without it the tracker carries an
		// unfinishable quest at the top of the list for the whole game.
		CompleteQuest(FName("Everloaf"));
	}
}

void AMillhavenCharacter::OnClose()
{
	ActiveNPC = nullptr;
	CurrentNode = NAME_None;
}

void AMillhavenCharacter::OnOption1() { SelectOption(0); }
void AMillhavenCharacter::OnOption2() { SelectOption(1); }
void AMillhavenCharacter::OnOption3() { SelectOption(2); }
void AMillhavenCharacter::OnOption4() { SelectOption(3); }

// ---------------------------------------------------------------------------
// Quest tracker
// ---------------------------------------------------------------------------
FMillhavenQuest* AMillhavenCharacter::FindQuest(FName Id)
{
	return Quests.FindByPredicate([Id](const FMillhavenQuest& Q) { return Q.Id == Id; });
}

const FMillhavenQuest* AMillhavenCharacter::FindQuest(FName Id) const
{
	return Quests.FindByPredicate([Id](const FMillhavenQuest& Q) { return Q.Id == Id; });
}

bool AMillhavenCharacter::IsQuestActive(FName Id) const
{
	const FMillhavenQuest* Q = FindQuest(Id);
	return Q && !Q->bComplete;
}

bool AMillhavenCharacter::IsQuestComplete(FName Id) const
{
	const FMillhavenQuest* Q = FindQuest(Id);
	return Q && Q->bComplete;
}

void AMillhavenCharacter::StartOrUpdateQuest(FName Id, const FString& Name, const FString& Objective)
{
	if (Id.IsNone())
	{
		return;
	}
	if (FMillhavenQuest* Existing = FindQuest(Id))
	{
		// Retarget a running quest; a finished one stays finished.
		if (!Existing->bComplete)
		{
			Existing->Name = Name;
			Existing->Objective = Objective;
		}
		return;
	}

	FMillhavenQuest Q;
	Q.Id = Id;
	Q.Name = Name;
	Q.Objective = Objective;
	Quests.Add(Q);

	UE_LOG(LogTemp, Log, TEXT("Millhaven: quest started - %s (%s)"), *Name, *Id.ToString());
}

void AMillhavenCharacter::CompleteQuest(FName Id)
{
	FMillhavenQuest* Q = FindQuest(Id);
	if (!Q || Q->bComplete)
	{
		return;
	}
	Q->bComplete = true;
	UE_LOG(LogTemp, Log, TEXT("Millhaven: quest complete - %s"), *Q->Name);
}

void AMillhavenCharacter::SetQuestItemGoal(FName Id, FName Item, int32 Count, bool bAutoComplete)
{
	if (Item.IsNone() || Count <= 0)
	{
		return;
	}
	if (FMillhavenQuest* Q = FindQuest(Id))
	{
		Q->RequiredItem = Item;
		Q->RequiredCount = Count;
		Q->bAutoCompleteOnItems = bAutoComplete;
	}
}

bool AMillhavenCharacter::IsOptionAvailable(const FDlgOption& Opt) const
{
	if (!Opt.RequiresItem.IsNone()
		&& GetItemCount(Opt.RequiresItem) < Opt.RequiresItemCount)
	{
		return false;
	}
	if (!Opt.RequiresQuest.IsNone() && !IsQuestActive(Opt.RequiresQuest))
	{
		return false;
	}
	if (!Opt.RequiresQuestComplete.IsNone() && !IsQuestComplete(Opt.RequiresQuestComplete))
	{
		return false;
	}
	if (!Opt.ForbidsQuest.IsNone() && FindQuest(Opt.ForbidsQuest) != nullptr)
	{
		return false;
	}
	return true;
}

TArray<const FDlgOption*> AMillhavenCharacter::GetAvailableOptions() const
{
	TArray<const FDlgOption*> Out;
	if (!ActiveNPC)
	{
		return Out;
	}
	const FDlgNode* Node = ActiveNPC->GetNode(CurrentNode);
	if (!Node)
	{
		return Out;
	}
	for (const FDlgOption& Opt : Node->Options)
	{
		if (IsOptionAvailable(Opt))
		{
			Out.Add(&Opt);
		}
	}
	return Out;
}

void AMillhavenCharacter::SelectOption(int32 Index)
{
	if (!ActiveNPC)
	{
		return;
	}
	const TArray<const FDlgOption*> Available = GetAvailableOptions();
	if (!Available.IsValidIndex(Index))
	{
		return;
	}

	// Copy the whole option before anything else: OnClose() clears ActiveNPC,
	// which invalidates the node these pointers point into.
	const FDlgOption Opt = *Available[Index];

	// Take payment first: if the player is somehow short, nothing else happens.
	// The option should not have been offered at all in that case, but a quest
	// that completes while the goods stay in the bag is the worse failure.
	if (!Opt.ConsumesItem.IsNone() && Opt.ConsumesItemCount > 0)
	{
		if (!ConsumeItem(Opt.ConsumesItem, Opt.ConsumesItemCount))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Millhaven: option '%s' wanted %d x %s but the player has %d."),
				*Opt.Label, Opt.ConsumesItemCount, *Opt.ConsumesItem.ToString(),
				GetItemCount(Opt.ConsumesItem));
			return;
		}
	}

	if (!Opt.QuestName.IsEmpty())
	{
		// An explicit id lets two NPCs advance the same quest; without one the
		// display name doubles as the id.
		const FName Id = Opt.QuestId.IsNone() ? FName(*Opt.QuestName) : Opt.QuestId;
		StartOrUpdateQuest(Id, Opt.QuestName, Opt.QuestObjective);
		SetQuestItemGoal(Id, Opt.QuestItem, Opt.QuestItemCount, Opt.bQuestItemAutoCompletes);
	}
	if (!Opt.CompletesQuest.IsNone())
	{
		CompleteQuest(Opt.CompletesQuest);
	}

	if (Opt.Next == FName("x") || Opt.Next == NAME_None)
	{
		OnClose();
	}
	else
	{
		CurrentNode = Opt.Next;
	}
}

// Exploration quests finish by arriving. Landmark positions come from the
// world generator so they cannot drift away from the geometry.
void AMillhavenCharacter::UpdateLocationQuests()
{
	struct FArrival
	{
		FName Id;
		FVector2D AtM;
		float RadiusM;
	};

	const FArrival Arrivals[] =
	{
		{ FName("GlowingDeep"), AMillhavenWorldGen::CaveMouthM(), 9.f },
		// The moored boat, not the dock: Captain Wren stands on the dock, so a
		// dock-centred radius would complete the quest as he gave it out.
		{ FName("BayTrouble"),  AMillhavenWorldGen::BayWatchM(),  7.f },
	};

	const FVector L = GetActorLocation();
	const FVector2D HereM(L.X / 100.0, L.Y / 100.0);

	for (const FArrival& A : Arrivals)
	{
		if (!IsQuestActive(A.Id))
		{
			continue;
		}
		if ((float)FVector2D::Distance(HereM, A.AtM) <= A.RadiusM)
		{
			CompleteQuest(A.Id);
		}
	}
}

// If the world mesh's collision has not cooked yet - or the player somehow
// clips through it - drop them back onto the analytic terrain height rather
// than letting them fall out of the world.
void AMillhavenCharacter::EnforceGroundSafety()
{
	const FVector L = GetActorLocation();
	const double Ground = AMillhavenWorldGen::TerrainHeight((float)L.X, (float)L.Y);
	if (L.Z < Ground - 400.0)
	{
		SetActorLocation(FVector(L.X, L.Y, Ground + 120.0), false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->Velocity = FVector::ZeroVector;
		}
	}
}

void AMillhavenCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	EnforceGroundSafety();
	UpdateLocationQuests();
	SweepPickups();
	UpdateItemQuests();

	if (PickupToastTimer > 0.f)
	{
		PickupToastTimer -= DeltaTime;
		if (PickupToastTimer <= 0.f)
		{
			PickupToast.Reset();
		}
	}

	const float Speed = (float)GetVelocity().Size2D();
	if (Speed > 20.f && !IsInDialogue())
	{
		WalkPhase += DeltaTime * 9.f;
		const double Bob = FMath::Abs(FMath::Sin(WalkPhase)) * 4.0;
		Avatar->SetRelativeLocation(FVector(0.0, 0.0, -92.0 + Bob));
	}
	else
	{
		Avatar->SetRelativeLocation(FVector(0.0, 0.0, -92.0));
	}
}
