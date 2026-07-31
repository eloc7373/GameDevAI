#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MillhavenHUD.generated.h"

class AMillhavenCharacter;

/**
 * Canvas-drawn HUD (no UMG assets): quest panel, minimap, interaction prompt,
 * dialogue box with numbered options, and a location banner.
 */
UCLASS()
class AMillhavenHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawQuestPanel(AMillhavenCharacter* C);
	void DrawMinimap(AMillhavenCharacter* C);
	void DrawPrompt(AMillhavenCharacter* C);
	void DrawDialogue(AMillhavenCharacter* C);
	void DrawLocation(AMillhavenCharacter* C);
	void DrawControls();

	void Panel(float X, float Y, float W, float H, const FLinearColor& Col);
	void Text(const FString& S, float X, float Y, const FLinearColor& Col, float Scale = 1.f);
	TArray<FString> WrapText(const FString& S, int32 MaxChars);

	FString LastBiome;
	float BannerTimer = 0.f;
};
