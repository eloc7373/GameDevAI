#include "MillhavenGameMode.h"
#include "MillhavenCharacter.h"
#include "MillhavenHUD.h"
#include "MillhavenWorldGen.h"
#include "Engine/World.h"

AMillhavenGameMode::AMillhavenGameMode()
{
	DefaultPawnClass = AMillhavenCharacter::StaticClass();
	HUDClass = AMillhavenHUD::StaticClass();
}

void AMillhavenGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* W = GetWorld())
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		W->SpawnActor<AMillhavenWorldGen>(AMillhavenWorldGen::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, P);
	}
}
