#include "MillhavenPickup.h"
#include "MillhavenWorldGen.h"
#include "ProcMeshLib.h"

#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

TArray<TWeakObjectPtr<AMillhavenPickup>> AMillhavenPickup::All;

// ---------------------------------------------------------------------------

AMillhavenPickup::AMillhavenPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	// Items are swept up by proximity, not by bumping into them - collision
	// here would just snag the player on scenery.
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMillhavenPickup::BeginPlay()
{
	Super::BeginPlay();
	All.AddUnique(this);
	// Desynchronised bob, so a field of wheat does not pulse in lockstep.
	BobPhase = FMath::FRandRange(0.f, 2.f * PI);
}

void AMillhavenPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	All.Remove(this);
	All.RemoveAll([](const TWeakObjectPtr<AMillhavenPickup>& W) { return !W.IsValid(); });
	Super::EndPlay(EndPlayReason);
}

void AMillhavenPickup::Init(FName InItemId, const FString& InDisplayName,
                            EMillhavenPickupShape Shape, const FColor& Tint)
{
	ItemId = InItemId;
	DisplayName = InDisplayName;

	if (!BaseMaterial)
	{
		BaseMaterial = AMillhavenWorldGen::ResolveBaseMaterial();
	}

	FMillhavenMeshBatch B;
	switch (Shape)
	{
	case EMillhavenPickupShape::Sheaf:
		// A tied bundle: stalks fanning up out of a band.
		for (int32 i = 0; i < 5; i++)
		{
			const float A = (2.f * PI * i) / 5.f;
			const double LeanX = FMath::Cos(A) * 7.0;
			const double LeanY = FMath::Sin(A) * 7.0;
			B.AddBox(FVector(LeanX, LeanY, 26.0), FVector(4.0, 4.0, 52.0));
			B.AddCone(FVector(LeanX, LeanY, 52.0), 7.f, 20.f, 4, A * 30.f);
		}
		B.AddBox(FVector(0.0, 0.0, 20.0), FVector(22.0, 22.0, 7.0));
		break;

	case EMillhavenPickupShape::Leaf:
		// A frond on a short stem.
		B.AddBox(FVector(0.0, 0.0, 10.0), FVector(3.0, 3.0, 20.0));
		B.AddBox(FVector(0.0, 0.0, 26.0), FVector(9.0, 30.0, 3.0), 20.f);
		B.AddBox(FVector(0.0, 0.0, 33.0), FVector(7.0, 22.0, 3.0), -25.f);
		break;

	case EMillhavenPickupShape::Crystal:
		B.AddCone(FVector(0.0, 0.0, 14.0), 11.f, 30.f, 5);
		B.AddCone(FVector(0.0, 0.0, 14.0), 11.f, -14.f, 5);
		break;

	case EMillhavenPickupShape::Pearl:
	default:
		// Faceted blob - three stacked rings read as a pearl at low poly.
		B.AddCylinder(FVector(0.0, 0.0, 4.0), 6.f, 10.f, 6.f, 6);
		B.AddCylinder(FVector(0.0, 0.0, 10.0), 10.f, 10.f, 7.f, 6);
		B.AddCylinder(FVector(0.0, 0.0, 17.0), 10.f, 5.f, 6.f, 6);
		break;
	}

	TArray<FLinearColor> Colors;
	Colors.Init(FLinearColor::FromSRGBColor(Tint), B.V.Num());
	Mesh->CreateMeshSection_LinearColor(0, B.V, B.T, B.N, B.UV, Colors,
		TArray<FProcMeshTangent>(), false);

	if (BaseMaterial)
	{
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this))
		{
			MID->SetVectorParameterValue(FName("Color"), FLinearColor::FromSRGBColor(Tint));
			MIDs.Add(MID);
			Mesh->SetMaterial(0, MID);
		}
	}
}

bool AMillhavenPickup::Collect()
{
	if (bCollected)
	{
		return false;
	}
	bCollected = true;
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);
	return true;
}

void AMillhavenPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bCollected)
	{
		return;
	}

	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const double Bob = FMath::Sin(T * 1.8f + BobPhase) * 5.0;
	Mesh->SetRelativeLocation(FVector(0.0, 0.0, Bob));
	Mesh->SetRelativeRotation(FRotator(0.f, T * 45.f, 0.f));
}
