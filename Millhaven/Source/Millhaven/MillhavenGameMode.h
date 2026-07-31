#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MillhavenGameMode.generated.h"

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

protected:
	virtual void BeginPlay() override;
};
