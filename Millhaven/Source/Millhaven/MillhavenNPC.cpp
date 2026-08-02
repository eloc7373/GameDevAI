#include "MillhavenNPC.h"
#include "ProcMeshLib.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

TArray<TWeakObjectPtr<AMillhavenNPC>> AMillhavenNPC::All;

static UMaterialInstanceDynamic* NPCMakeColorMID(UObject* Outer, UMaterialInterface* Base, const FColor& C)
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

static void NPCCommitSection(UProceduralMeshComponent* Mesh, int32 Section,
                             FMillhavenMeshBatch& Batch, const FColor& Tint,
                             UMaterialInstanceDynamic* MID)
{
	TArray<FLinearColor> Colors;
	Colors.Init(FLinearColor::FromSRGBColor(Tint), Batch.V.Num());
	Mesh->CreateMeshSection_LinearColor(Section, Batch.V, Batch.T, Batch.N, Batch.UV,
		Colors, TArray<FProcMeshTangent>(), false);
	if (MID)
	{
		Mesh->SetMaterial(Section, MID);
	}
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

void AMillhavenNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	All.Remove(this);
	// Drop any entries whose actor has already gone away.
	All.RemoveAll([](const TWeakObjectPtr<AMillhavenNPC>& W) { return !W.IsValid(); });
	Super::EndPlay(EndPlayReason);
}

void AMillhavenNPC::Init(const FString& InName, const FString& InRole,
                         const FColor& BodyColor, const FColor& SkinColor, const FColor& HairColor)
{
	NpcName = InName;
	NpcRole = InRole;

	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (!BaseMaterial)
	{
		BaseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	const FColor LegColor(
		(uint8)(BodyColor.R * 0.6f), (uint8)(BodyColor.G * 0.6f), (uint8)(BodyColor.B * 0.6f));

	// Local body frame: +X forward, +Y right, +Z up. Sizes in cm.
	// Legs (static)
	{
		FMillhavenMeshBatch B;
		B.AddBox(FVector(0.0,  14.0, 26.0), FVector(20.0, 19.0, 52.0));
		B.AddBox(FVector(0.0, -14.0, 26.0), FVector(20.0, 19.0, 52.0));
		UMaterialInstanceDynamic* MID = NPCMakeColorMID(this, BaseMaterial, LegColor);
		if (MID) { MIDs.Add(MID); }
		NPCCommitSection(LegsMesh, 0, B, LegColor, MID);
	}
	// Body (bobs)
	{
		FMillhavenMeshBatch B;
		B.AddBox(FVector::ZeroVector, FVector(27.0, 47.0, 68.0));
		UMaterialInstanceDynamic* MID = NPCMakeColorMID(this, BaseMaterial, BodyColor);
		if (MID) { MIDs.Add(MID); }
		NPCCommitSection(BodyMesh, 0, B, BodyColor, MID);
		BodyMesh->SetRelativeLocation(FVector(0.0, 0.0, 88.0));
	}
	// Head + hair (bobs)
	{
		FMillhavenMeshBatch Skin;
		Skin.AddBox(FVector::ZeroVector, FVector(41.0, 41.0, 41.0));
		UMaterialInstanceDynamic* SkinMID = NPCMakeColorMID(this, BaseMaterial, SkinColor);
		if (SkinMID) { MIDs.Add(SkinMID); }
		NPCCommitSection(HeadMesh, 0, Skin, SkinColor, SkinMID);

		FMillhavenMeshBatch Hair;
		Hair.AddBox(FVector(0.0, 0.0, 24.0), FVector(43.0, 43.0, 14.0));
		UMaterialInstanceDynamic* HairMID = NPCMakeColorMID(this, BaseMaterial, HairColor);
		if (HairMID) { MIDs.Add(HairMID); }
		NPCCommitSection(HeadMesh, 1, Hair, HairColor, HairMID);

		HeadMesh->SetRelativeLocation(FVector(0.0, 0.0, 147.0));
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
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Millhaven: AddOption on unknown dialogue node '%s' (NPC '%s') - option dropped."),
			*NodeKey.ToString(), *NpcName);
	}
}

void AMillhavenNPC::FacePoint(const FVector& WorldPoint)
{
	const FVector To = WorldPoint - GetActorLocation();
	const float Yaw = (float)FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
}

void AMillhavenNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const double Bob = FMath::Sin(T * 1.3f + BobPhase) * 4.0;
	BodyMesh->SetRelativeLocation(FVector(0.0, 0.0, 88.0 + Bob));
	HeadMesh->SetRelativeLocation(FVector(0.0, 0.0, 147.0 + Bob));
}
