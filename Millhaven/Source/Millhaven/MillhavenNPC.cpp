#include "MillhavenNPC.h"
#include "MillhavenWorldGen.h"
#include "ProcMeshLib.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
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

	// The art is authored with the NPC's feet at the actor origin, so the
	// capsule is pushed up by its own half-height to stand on the ground
	// rather than being buried to the waist.
	Collision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Collision"));
	Collision->SetupAttachment(Root);
	Collision->InitCapsuleSize(42.f, 90.f);
	Collision->SetRelativeLocation(FVector(0.0, 0.0, 90.0));
	Collision->SetCollisionProfileName(TEXT("Pawn"));

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
		// One shared resolution policy for the whole project - see
		// AMillhavenWorldGen::ResolveBaseMaterial.
		BaseMaterial = AMillhavenWorldGen::ResolveBaseMaterial();
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
	FDlgOption Opt;
	Opt.Label = Label;
	Opt.Next = Next;
	Opt.QuestName = QuestName;
	Opt.QuestObjective = QuestObjective;
	AddOption(NodeKey, Opt);
}

void AMillhavenNPC::AddOption(FName NodeKey, const FDlgOption& Option)
{
	if (FDlgNode* Node = Dialogue.Find(NodeKey))
	{
		Node->Options.Add(Option);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Millhaven: AddOption on unknown dialogue node '%s' (NPC '%s') - option dropped."),
			*NodeKey.ToString(), *NpcName);
	}
}

void AMillhavenNPC::ValidateDialogue() const
{
#if !UE_BUILD_SHIPPING
	static const FName StartKey("start");
	static const FName CloseKey("x");

	if (!Dialogue.Contains(StartKey))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Millhaven: NPC '%s' has no 'start' node - it cannot be talked to."), *NpcName);
		return;
	}

	// Dangling targets: an option pointing at a node that was never added
	// used to just dead-end the conversation with no diagnostic.
	for (const TPair<FName, FDlgNode>& Pair : Dialogue)
	{
		for (const FDlgOption& Opt : Pair.Value.Options)
		{
			if (Opt.Next == CloseKey || Opt.Next.IsNone())
			{
				continue;
			}
			if (!Dialogue.Contains(Opt.Next))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Millhaven: NPC '%s' node '%s' option '%s' targets unknown node '%s'."),
					*NpcName, *Pair.Key.ToString(), *Opt.Label, *Opt.Next.ToString());
			}
		}
	}

	// A node whose every option is requirement-gated can strand the player
	// with nothing to press but Escape, and only in the quest state that
	// happens to hide them all - so it is easy to miss by hand.
	for (const TPair<FName, FDlgNode>& Pair : Dialogue)
	{
		if (Pair.Value.Options.Num() == 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Millhaven: NPC '%s' node '%s' has no options - it is a dead end."),
				*NpcName, *Pair.Key.ToString());
			continue;
		}

		bool bHasUngated = false;
		for (const FDlgOption& Opt : Pair.Value.Options)
		{
			if (Opt.RequiresQuest.IsNone()
				&& Opt.RequiresQuestComplete.IsNone()
				&& Opt.ForbidsQuest.IsNone())
			{
				bHasUngated = true;
				break;
			}
		}
		if (!bHasUngated)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Millhaven: NPC '%s' node '%s' has only gated options - the player ")
				TEXT("can reach a state where nothing is offered."),
				*NpcName, *Pair.Key.ToString());
		}
	}

	// Unreachable nodes: authored but impossible to arrive at.
	TSet<FName> Reachable;
	TArray<FName> Pending;
	Reachable.Add(StartKey);
	Pending.Add(StartKey);
	while (Pending.Num() > 0)
	{
		const FName Key = Pending.Pop();
		if (const FDlgNode* Node = Dialogue.Find(Key))
		{
			for (const FDlgOption& Opt : Node->Options)
			{
				if (Opt.Next == CloseKey || Opt.Next.IsNone())
				{
					continue;
				}
				if (Dialogue.Contains(Opt.Next) && !Reachable.Contains(Opt.Next))
				{
					Reachable.Add(Opt.Next);
					Pending.Add(Opt.Next);
				}
			}
		}
	}
	for (const TPair<FName, FDlgNode>& Pair : Dialogue)
	{
		if (!Reachable.Contains(Pair.Key))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Millhaven: NPC '%s' node '%s' is unreachable from 'start'."),
				*NpcName, *Pair.Key.ToString());
		}
	}
#endif
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
