#include "MillhavenGameMode.h"
#include "MillhavenCharacter.h"
#include "MillhavenHUD.h"
#include "MillhavenWorldGen.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AMillhavenGameMode::AMillhavenGameMode()
{
	DefaultPawnClass = AMillhavenCharacter::StaticClass();
	HUDClass = AMillhavenHUD::StaticClass();
}

void AMillhavenGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}

	// If someone dragged a generator into the level by hand, use that one
	// instead of spawning a second copy of the entire world.
	for (TActorIterator<AMillhavenWorldGen> It(W); It; ++It)
	{
		WorldGen = *It;
		break;
	}

	if (!WorldGen)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		P.ObjectFlags |= RF_Transient; // generated content, never saved into the map
		WorldGen = W->SpawnActor<AMillhavenWorldGen>(AMillhavenWorldGen::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, P);
	}

	if (!WorldGen)
	{
		UE_LOG(LogTemp, Error, TEXT("Millhaven: could not spawn AMillhavenWorldGen - the world will be empty."));
	}
}
