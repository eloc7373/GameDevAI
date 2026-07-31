#include "MillhavenCharacter.h"
#include "MillhavenNPC.h"
#include "MillhavenWorldGen.h"
#include "ProcMeshLib.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

// Enhanced Input (UE 5.8)
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

static UMaterialInstanceDynamic* CharColorMID(UObject* Outer, const FColor& C)
{
	static UMaterialInterface* Base = nullptr;
	if (!Base)
		Base = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
	if (MID) MID->SetVectorParameterValue(FName("Color"), FLinearColor::FromSRGBColor(C));
	return MID;
}

static void CommitSec(UProceduralMeshComponent* M, int32 Sec, FMeshBatch& Bat,
                      UMaterialInstanceDynamic* MID)
{
	TArray<FLinearColor> Cols; Cols.Init(FLinearColor::White, Bat.V.Num());
	M->CreateMeshSection_LinearColor(Sec, Bat.V, Bat.T, Bat.N, Bat.UV, Cols,
		TArray<FProcMeshTangent>(), false);
	M->SetMaterial(Sec, MID);
}

AMillhavenCharacter::AMillhavenCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(38.f, 92.f);

	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = true;
		Move->RotationRate = FRotator(0.f, 640.f, 0.f);
		Move->MaxWalkSpeed = 560.f;
		Move->JumpZVelocity = 420.f;
		Move->AirControl = 0.2f;
	}

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 520.f;
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.f;
	SpringArm->SetRelativeLocation(FVector(0, 0, 90.f));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);

	Avatar = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Avatar"));
	Avatar->SetupAttachment(RootComponent);
	Avatar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Avatar->SetRelativeLocation(FVector(0, 0, -92.f)); // feet at capsule bottom

	// ---- Enhanced Input assets, built entirely in C++ ----
	auto MakeAxis = [&](const TCHAR* Name)
	{
		UInputAction* A = CreateDefaultSubobject<UInputAction>(Name);
		A->ValueType = EInputActionValueType::Axis1D;
		return A;
	};
	auto MakeButton = [&](const TCHAR* Name)
	{
		UInputAction* A = CreateDefaultSubobject<UInputAction>(Name);
		A->ValueType = EInputActionValueType::Boolean;
		return A;
	};

	MoveForwardAction = MakeAxis(TEXT("IA_MoveForward"));
	MoveRightAction   = MakeAxis(TEXT("IA_MoveRight"));
	TurnAction        = MakeAxis(TEXT("IA_Turn"));
	LookUpAction      = MakeAxis(TEXT("IA_LookUp"));
	InteractAction    = MakeButton(TEXT("IA_Interact"));
	CloseAction       = MakeButton(TEXT("IA_Close"));
	Dialogue1Action   = MakeButton(TEXT("IA_Dialogue1"));
	Dialogue2Action   = MakeButton(TEXT("IA_Dialogue2"));
	Dialogue3Action   = MakeButton(TEXT("IA_Dialogue3"));
	Dialogue4Action   = MakeButton(TEXT("IA_Dialogue4"));
	JumpAction        = MakeButton(TEXT("IA_Jump"));

	InputMapping = CreateDefaultSubobject<UInputMappingContext>(TEXT("IMC_Default"));

	int32 NegId = 0;
	auto MapNeg = [&](UInputAction* Action, const FKey& Key)
	{
		FEnhancedActionKeyMapping& M = InputMapping->MapKey(Action, Key);
		UInputModifierNegate* Neg = CreateDefaultSubobject<UInputModifierNegate>(
			*FString::Printf(TEXT("Neg_%d"), NegId++));
		M.Modifiers.Add(Neg);
	};

	// Movement (WASD + arrows). Negative directions get a Negate modifier.
	InputMapping->MapKey(MoveForwardAction, EKeys::W);
	InputMapping->MapKey(MoveForwardAction, EKeys::Up);
	MapNeg(MoveForwardAction, EKeys::S);
	MapNeg(MoveForwardAction, EKeys::Down);

	InputMapping->MapKey(MoveRightAction, EKeys::D);
	InputMapping->MapKey(MoveRightAction, EKeys::Right);
	MapNeg(MoveRightAction, EKeys::A);
	MapNeg(MoveRightAction, EKeys::Left);

	// Look
	InputMapping->MapKey(TurnAction, EKeys::MouseX);
	MapNeg(LookUpAction, EKeys::MouseY);

	// Buttons
	InputMapping->MapKey(InteractAction, EKeys::E);
	InputMapping->MapKey(CloseAction, EKeys::Escape);
	InputMapping->MapKey(Dialogue1Action, EKeys::One);
	InputMapping->MapKey(Dialogue2Action, EKeys::Two);
	InputMapping->MapKey(Dialogue3Action, EKeys::Three);
	InputMapping->MapKey(Dialogue4Action, EKeys::Four);
	InputMapping->MapKey(JumpAction, EKeys::SpaceBar);
}

void AMillhavenCharacter::BeginPlay()
{
	Super::BeginPlay();

	BuildAvatar();

	// Deterministic ground placement (works even before terrain mesh exists)
	FVector L = GetActorLocation();
	L.Z = AMillhavenWorldGen::TerrainHeight(L.X, L.Y) + 120.f;
	SetActorLocation(L);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->ViewPitchMin = -55.f;
			PC->PlayerCameraManager->ViewPitchMax = 18.f;
		}
		// Register the Enhanced Input mapping context
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
}

void AMillhavenCharacter::BuildAvatar()
{
	int32 s = 0;
	auto Part = [&](const FVector& c, const FVector& size, const FColor& col)
	{
		FMeshBatch B; B.AddBox(c, size);
		UMaterialInstanceDynamic* MID = CharColorMID(this, col); MIDs.Add(MID);
		CommitSec(Avatar, s++, B, MID);
	};
	Part(FVector(0, 0, 90.f),  FVector(28.f, 48.f, 70.f), FColor(58, 106, 170));   // body
	Part(FVector(0, 0, 148.f), FVector(42.f, 42.f, 42.f), FColor(240, 192, 128));  // head
	Part(FVector(0, 0, 168.f), FVector(44.f, 44.f, 18.f), FColor(176, 112, 48));   // hair
	Part(FVector(-34.f, 0, 88.f), FVector(18.f, 18.f, 55.f), FColor(42, 74, 128)); // L arm
	Part(FVector( 34.f, 0, 88.f), FVector(18.f, 18.f, 55.f), FColor(42, 74, 128)); // R arm
	Part(FVector(-14.f, 0, 26.f), FVector(20.f, 20.f, 52.f), FColor(30, 46, 74));  // L leg
	Part(FVector( 14.f, 0, 26.f), FVector(20.f, 20.f, 52.f), FColor(30, 46, 74));  // R leg
	Part(FVector(0, -24.f, 90.f), FVector(20.f, 34.f, 52.f), FColor(176, 96, 48)); // backpack
}

void AMillhavenCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

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
	}
}

void AMillhavenCharacter::MoveForward(const FInputActionValue& Value)
{
	const float V = Value.Get<float>();
	if (V != 0.f && !IsInDialogue() && Controller)
	{
		const FRotator Yaw(0, Controller->GetControlRotation().Yaw, 0);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V);
	}
}

void AMillhavenCharacter::MoveRight(const FInputActionValue& Value)
{
	const float V = Value.Get<float>();
	if (V != 0.f && !IsInDialogue() && Controller)
	{
		const FRotator Yaw(0, Controller->GetControlRotation().Yaw, 0);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V);
	}
}

void AMillhavenCharacter::Turn(const FInputActionValue& Value)
{
	if (!IsInDialogue()) AddControllerYawInput(Value.Get<float>() * 1.4f);
}

void AMillhavenCharacter::LookUp(const FInputActionValue& Value)
{
	if (!IsInDialogue()) AddControllerPitchInput(Value.Get<float>() * 1.2f);
}

void AMillhavenCharacter::OnJumpStart() { if (!IsInDialogue()) Jump(); }
void AMillhavenCharacter::OnJumpStop()  { StopJumping(); }

AMillhavenNPC* AMillhavenCharacter::GetNearbyNPC() const
{
	AMillhavenNPC* Best = nullptr;
	float BestD = 320.f;
	for (AMillhavenNPC* N : AMillhavenNPC::All)
	{
		if (!N) continue;
		const float D = FVector::Dist2D(N->GetActorLocation(), GetActorLocation());
		if (D < BestD) { BestD = D; Best = N; }
	}
	return Best;
}

void AMillhavenCharacter::OnInteract()
{
	if (IsInDialogue()) return;
	if (AMillhavenNPC* N = GetNearbyNPC())
	{
		ActiveNPC = N;
		CurrentNode = "start";
		N->FacePoint(GetActorLocation());
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

void AMillhavenCharacter::SelectOption(int32 Index)
{
	if (!ActiveNPC) return;
	const FDlgNode* Node = ActiveNPC->GetNode(CurrentNode);
	if (!Node || !Node->Options.IsValidIndex(Index)) return;

	const FDlgOption& Opt = Node->Options[Index];
	if (!Opt.QuestName.IsEmpty())
	{
		QuestName = Opt.QuestName;
		QuestObjective = Opt.QuestObjective;
	}
	if (Opt.Next == FName("x") || Opt.Next == NAME_None)
		OnClose();
	else
		CurrentNode = Opt.Next;
}

void AMillhavenCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float Speed = GetVelocity().Size2D();
	if (Speed > 20.f && !IsInDialogue())
	{
		WalkPhase += DeltaTime * 9.f;
		const float bob = FMath::Abs(FMath::Sin(WalkPhase)) * 4.f;
		Avatar->SetRelativeLocation(FVector(0, 0, -92.f + bob));
	}
	else
	{
		Avatar->SetRelativeLocation(FVector(0, 0, -92.f));
	}
}
