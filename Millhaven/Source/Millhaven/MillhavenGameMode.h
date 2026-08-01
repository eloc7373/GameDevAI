#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MillhavenGameMode.generated.h"

class AMillhavenWorldGen;

/**
 * Wires up the default pawn, HUD and controller, then spawns the world
 * generator so the level can be completely empty.
 */
UCLASS()
class AMillhavenGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMillhavenGameMode();

	/**
	 * The generator is spawned here rather than in BeginPlay so that its own
	 * BeginPlay runs inside the world's normal actor sweep - i.e. before the
	 * first physics step, so the terrain's collision exists before the player
	 * pawn can fall through it.
	 */
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

protected:
	/** Spawned generator, or an existing one already placed in the level. */
	UPROPERTY() AMillhavenWorldGen* WorldGen = nullptr;
};
