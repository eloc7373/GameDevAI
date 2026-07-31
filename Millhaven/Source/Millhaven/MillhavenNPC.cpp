#include "MillhavenNPC.h"
#include "ProcMeshLib.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

TArray<AMillhavenNPC*> AMillhavenNPC::All;

static UMaterialInstanceDynamic* NPCMakeColorMID(UObject* Outer, const FColor& C)
{
	static UMaterialInterface* Base = nullptr;
	if (!Base)
	{
		Base = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
	if (MID)
	{
		MID->SetVectorParameterValue(FName("Color"), FLinearColor::FromSRGBColor(C));
	}
	return MID;
}

static void NPCCommitSection(UProceduralMeshComponent* Mesh, int32 Section,
                             FMeshBatch& Batch, UMaterialInstanceDynamic* MID)
{
	TArray<FLinearColor> Colors;
	Colors.Init(FLinearColor::White, Batch.V.Num());
	Mesh->CreateMeshSection_LinearColor(Section, Batch.V, Batch.T, Batch.N, Batch.UV,
		Colors, TArray<FProcMeshTangent>(), false);
	Mesh->SetMaterial(Section, MID);
}

AMillhavenNPC::AMillhavenNPC()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	LegsMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Legs"));
	LegsMesh->SetupAttachment(Root);
	LegsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	BodyMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Body"));
	BodyMesh->SetupAttachment(Root);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HeadMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Head"));
	HeadMesh->SetupAttachment(Root);
	HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMillhavenNPC::BeginPlay()
{
	Super::BeginPlay();
	All.AddUnique(this);
	BobPhase = FMath::FRandRange(0.f, 2.f * PI);
}

void AMillhavenNPC::EndPlay(const EEndPlayReason::Type Reason)
{
	All.Remove(this);
	Super::EndPlay(Reason);
}

void AMillhavenNPC::Init(const FString& InName, const FString& InRole,
                         const FColor& BodyColor, const FColor& SkinColor, const FColor& HairColor)
{
	NpcName = InName;
	Role = InRole;

	const FColor LegColor(
		(uint8)(BodyColor.R * 0.6f), (uint8)(BodyColor.G * 0.6f), (uint8)(BodyColor.B * 0.6f));

	// Local body frame: +X forward, +Y right, +Z up. Sizes in cm.
	// Legs (static)
	{
		FMeshBatch B;
		B.AddBox(FVector(0,  14, 26), FVector(20, 19, 52));
		B.AddBox(FVector(0, -14, 26), FVector(20, 19, 52));
		UMaterialInstanceDynamic* MID = NPCMakeColorMID(this, LegColor);
		MIDs.Add(MID);
		NPCCommitSection(LegsMesh, 0, B, MID);
	}
	// Body (bobs)
	{
		FMeshBatch B;
		B.AddBox(FVector::ZeroVector, FVector(27, 47, 68));
		UMaterialInstanceDynamic* MID = NPCMakeColorMID(this, BodyColor);
		MIDs.Add(MID);
		NPCCommitSection(BodyMesh, 0, B, MID);
		BodyMesh->SetRelativeLocation(FVector(0, 0, 88));
	}
	// Head + hair (bobs)
	{
		FMeshBatch Skin;
		Skin.AddBox(FVector::ZeroVector, FVector(41, 41, 41));
		UMaterialInstanceDynamic* SkinMID = NPCMakeColorMID(this, SkinColor);
		MIDs.Add(SkinMID);
		NPCCommitSection(HeadMesh, 0, Skin, SkinMID);

		FMeshBatch Hair;
		Hair.AddBox(FVector(0, 0, 24), FVector(43, 43, 14));
		UMaterialInstanceDynamic* HairMID = NPCMakeColorMID(this, HairColor);
		MIDs.Add(HairMID);
		NPCCommitSection(HeadMesh, 1, Hair, HairMID);

		HeadMesh->SetRelativeLocation(FVector(0, 0, 147));
	}
}

void AMillhavenNPC::AddNode(FName Key, const FString& Text)
{
	FDlgNode Node;
	Node.Text = Text;
	Dialogue.Add(Key, Node);
}

void AMillhavenNPC::AddOption(FName NodeKey, const FString& Label, FName Next,
                              const FString& QuestName, const FString& QuestObjective)
{
	if (FDlgNode* Node = Dialogue.Find(NodeKey))
	{
		FDlgOption Opt;
		Opt.Label = Label;
		Opt.Next = Next;
		Opt.QuestName = QuestName;
		Opt.QuestObjective = QuestObjective;
		Node->Options.Add(Opt);
	}
}

void AMillhavenNPC::FacePoint(const FVector& WorldPoint)
{
	const FVector To = WorldPoint - GetActorLocation();
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
	SetActorRotation(FRotator(0, Yaw, 0));
}

void AMillhavenNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Bob = FMath::Sin(T * 1.3f + BobPhase) * 4.f;
	BodyMesh->SetRelativeLocation(FVector(0, 0, 88.f + Bob));
	HeadMesh->SetRelativeLocation(FVector(0, 0, 147.f + Bob));
}
