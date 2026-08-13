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

	/**
	 * Wraps to a pixel width using the font's real metrics. Wrapping by
	 * character count instead - as this used to - overflows the panel on wide
	 * strings and wastes half the line on narrow ones, because the HUD font is
	 * proportional.
	 */
	TArray<FString> WrapText(const FString& S, float MaxWidthPx, float Scale);

	FString LastBiome;
	float BannerTimer = 0.f;
};
