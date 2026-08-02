#include "MillhavenHUD.h"
#include "MillhavenCharacter.h"
#include "MillhavenNPC.h"
#include "MillhavenWorldGen.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

static const FLinearColor PanelBg(0.05f, 0.03f, 0.01f, 0.82f);
static const FLinearColor Gold(0.96f, 0.75f, 0.19f, 1.f);
static const FLinearColor TextCol(0.9f, 0.86f, 0.78f, 1.f);
static const FLinearColor Dim(0.6f, 0.6f, 0.6f, 1.f);

// Village building footprints in design metres, kept in sync with the
// AddBuilding() calls in MillhavenWorldGen::BuildStructures().
static const FVector2D VillageBuildings[] = {
	{ -7.0,  -2.0 },
	{  6.0,  -3.5 },
	{ -6.0,   6.0 },
	{  7.5,   6.0 },
	{  0.0,  -9.0 },
	{ -12.0, -5.0 },
};

void AMillhavenHUD::Panel(float X, float Y, float W, float H, const FLinearColor& Col)
{
	DrawRect(Col, X, Y, W, H);
}

void AMillhavenHUD::Text(const FString& S, float X, float Y, const FLinearColor& Col, float Scale)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(S, Col, X, Y, Font, Scale);
}

TArray<FString> AMillhavenHUD::WrapText(const FString& S, int32 MaxChars)
{
	TArray<FString> Lines;
	TArray<FString> Words;
	S.ParseIntoArray(Words, TEXT(" "), true);
	FString Line;
	for (const FString& W : Words)
	{
		if (!Line.IsEmpty() && Line.Len() + W.Len() + 1 > MaxChars)
		{
			Lines.Add(Line);
			Line = W;
		}
		else
		{
			Line = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
		}
	}
	if (!Line.IsEmpty()) Lines.Add(Line);
	return Lines;
}

void AMillhavenHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas) return;
	AMillhavenCharacter* C = PlayerOwner ? Cast<AMillhavenCharacter>(PlayerOwner->GetPawn()) : nullptr;
	if (!C) return;

	DrawQuestPanel(C);
	DrawMinimap(C);
	DrawLocation(C);
	DrawControls();

	if (C->IsInDialogue())
	{
		DrawDialogue(C);
	}
	else if (C->GetNearbyNPC())
	{
		DrawPrompt(C);
	}
}

void AMillhavenHUD::DrawQuestPanel(AMillhavenCharacter* C)
{
	const float X = 24.f, Y = 24.f, W = 320.f, H = 96.f;
	Panel(X, Y, W, H, PanelBg);
	Panel(X, Y, W, 3.f, Gold);
	Text(TEXT("QUESTS"), X + 14.f, Y + 10.f, Gold, 1.1f);
	Text(FString(TEXT("- ")) + C->QuestName, X + 14.f, Y + 36.f, TextCol, 1.0f);
	Text(FString(TEXT("Objective: ")) + C->QuestObjective, X + 14.f, Y + 60.f, Dim, 0.85f);
}

void AMillhavenHUD::DrawMinimap(AMillhavenCharacter* C)
{
	const float S = 150.f;
	const float X = (float)Canvas->SizeX - S - 24.f, Y = 24.f;
	Panel(X, Y, S, S, FLinearColor(0.06f, 0.16f, 0.10f, 0.85f));
	Panel(X, Y, S, 3.f, Gold);

	const FVector P = C->GetActorLocation();
	const float cx = X + S * 0.5f, cy = Y + S * 0.5f;
	const float scale = 0.006f; // world cm -> minimap px

	auto Blip = [&](double WorldX, double WorldY, const FLinearColor& Col, float Size)
	{
		const float mx = cx + (float)((WorldX - P.X) * scale);
		const float my = cy + (float)((WorldY - P.Y) * scale);
		if (mx > X && mx < X + S && my > Y && my < Y + S)
		{
			Panel(mx - Size * 0.5f, my - Size * 0.5f, Size, Size, Col);
		}
	};

	// buildings
	for (const FVector2D& b : VillageBuildings)
	{
		Blip(b.X * 100.0, b.Y * 100.0, FLinearColor(0.78f, 0.44f, 0.25f, 1.f), 6.f);
	}
	// NPCs
	for (const TWeakObjectPtr<AMillhavenNPC>& Weak : AMillhavenNPC::All)
	{
		if (const AMillhavenNPC* N = Weak.Get())
		{
			const FVector L = N->GetActorLocation();
			Blip(L.X, L.Y, FLinearColor(1.f, 0.2f, 0.2f, 1.f), 6.f);
		}
	}
	// player
	Panel(cx - 4.f, cy - 4.f, 8.f, 8.f, FLinearColor(1.f, 0.9f, 0.25f, 1.f));
}

void AMillhavenHUD::DrawPrompt(AMillhavenCharacter* C)
{
	AMillhavenNPC* N = C->GetNearbyNPC();
	if (!N) return;
	const FString Msg = FString::Printf(TEXT("Press  E  -  Talk to %s"), *N->NpcName);
	const float W = 360.f, H = 40.f;
	const float X = ((float)Canvas->SizeX - W) * 0.5f, Y = (float)Canvas->SizeY - 150.f;
	Panel(X, Y, W, H, PanelBg);
	Text(Msg, X + 24.f, Y + 12.f, TextCol, 1.0f);
}

void AMillhavenHUD::DrawDialogue(AMillhavenCharacter* C)
{
	AMillhavenNPC* N = C->GetActiveNPC();
	if (!N) return;
	const FDlgNode* Node = N->GetNode(C->GetCurrentNode());
	if (!Node) return;

	const float W = 720.f, X = ((float)Canvas->SizeX - W) * 0.5f;
	const float H = 210.f, Y = (float)Canvas->SizeY - H - 40.f;
	Panel(X, Y, W, H, FLinearColor(0.04f, 0.024f, 0.008f, 0.94f));
	Panel(X, Y, W, 3.f, Gold);

	// portrait swatch
	Panel(X + 16.f, Y + 16.f, 48.f, 48.f, FLinearColor(0.3f, 0.2f, 0.4f, 1.f));
	Text(N->NpcName, X + 76.f, Y + 16.f, Gold, 1.15f);
	Text(N->NpcRole, X + 76.f, Y + 40.f, Dim, 0.8f);

	// body text (wrapped)
	TArray<FString> Lines = WrapText(Node->Text, 78);
	float ty = Y + 74.f;
	for (const FString& L : Lines)
	{
		Text(L, X + 20.f, ty, TextCol, 0.92f);
		ty += 20.f;
	}

	// options
	float oy = ty + 8.f;
	for (int32 i = 0; i < Node->Options.Num(); i++)
	{
		const FString Line = FString::Printf(TEXT("[%d]  %s"), i + 1, *Node->Options[i].Label);
		Text(Line, X + 24.f, oy, FLinearColor(0.85f, 0.85f, 0.85f, 1.f), 0.9f);
		oy += 20.f;
	}
}

void AMillhavenHUD::DrawLocation(AMillhavenCharacter* C)
{
	const FVector P = C->GetActorLocation();
	const FString Biome = AMillhavenWorldGen::BiomeAt((float)P.X, (float)P.Y);
	if (Biome != LastBiome)
	{
		LastBiome = Biome;
		BannerTimer = 2.4f;
	}
	if (BannerTimer > 0.f)
	{
		const UWorld* W = GetWorld();
		BannerTimer -= W ? W->GetDeltaSeconds() : 0.f;
		const float BW = 300.f, X = ((float)Canvas->SizeX - BW) * 0.5f, Y = 22.f;
		Panel(X, Y, BW, 34.f, PanelBg);
		Text(Biome, X + 20.f, Y + 9.f, FLinearColor(0.96f, 0.82f, 0.44f, 1.f), 1.1f);
	}
}

void AMillhavenHUD::DrawControls()
{
	const float X = (float)Canvas->SizeX - 190.f, Y = (float)Canvas->SizeY - 96.f;
	Text(TEXT("WASD  Move"), X, Y, Dim, 0.8f);
	Text(TEXT("Mouse  Look"), X, Y + 18.f, Dim, 0.8f);
	Text(TEXT("E  Interact"), X, Y + 36.f, Dim, 0.8f);
	Text(TEXT("1-4  Choose   Esc  Close"), X - 60.f, Y + 54.f, Dim, 0.8f);
}
