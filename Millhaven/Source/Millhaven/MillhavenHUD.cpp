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
static const FLinearColor DoneCol(0.55f, 0.82f, 0.45f, 1.f);

void AMillhavenHUD::Panel(float X, float Y, float W, float H, const FLinearColor& Col)
{
	DrawRect(Col, X, Y, W, H);
}

void AMillhavenHUD::Text(const FString& S, float X, float Y, const FLinearColor& Col, float Scale)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(S, Col, X, Y, Font, Scale);
}

TArray<FString> AMillhavenHUD::WrapText(const FString& S, float MaxWidthPx, float Scale)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;

	TArray<FString> Lines;
	TArray<FString> Words;
	S.ParseIntoArray(Words, TEXT(" "), true);

	FString Line;
	for (const FString& W : Words)
	{
		const FString Candidate = Line.IsEmpty() ? W : Line + TEXT(" ") + W;

		float TextW = 0.f, TextH = 0.f;
		GetTextSize(Candidate, TextW, TextH, Font, Scale);

		// A single word wider than the panel still has to go somewhere, so only
		// break when there is already something on the line to keep.
		if (!Line.IsEmpty() && TextW > MaxWidthPx)
		{
			Lines.Add(Line);
			Line = W;
		}
		else
		{
			Line = Candidate;
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
	const TArray<FMillhavenQuest>& Quests = C->GetQuests();

	const float X = 24.f, Y = 24.f, W = 340.f;
	const float RowH = 42.f;
	const float H = 34.f + RowH * (float)FMath::Max(1, Quests.Num());

	Panel(X, Y, W, H, PanelBg);
	Panel(X, Y, W, 3.f, Gold);
	Text(TEXT("QUESTS"), X + 14.f, Y + 10.f, Gold, 1.1f);

	float ty = Y + 36.f;
	for (const FMillhavenQuest& Q : Quests)
	{
		const FString Head = FString::Printf(TEXT("%s %s"),
			Q.bComplete ? TEXT("[x]") : TEXT("[ ]"), *Q.Name);
		Text(Head, X + 14.f, ty, Q.bComplete ? DoneCol : TextCol, 1.0f);

		const FString Sub = Q.bComplete ? FString(TEXT("Complete")) : Q.Objective;
		Text(Sub, X + 32.f, ty + 19.f, Dim, 0.8f);
		ty += RowH;
	}
}

void AMillhavenHUD::DrawMinimap(AMillhavenCharacter* C)
{
	const float S = 150.f;
	const float X = (float)Canvas->SizeX - S - 24.f, Y = 24.f;
	Panel(X, Y, S, S, FLinearColor(0.06f, 0.16f, 0.10f, 0.85f));
	Panel(X, Y, S, 3.f, Gold);

	const FVector P = C->GetActorLocation();
	const float cx = X + S * 0.5f, cy = Y + S * 0.5f;

	// World cm -> minimap px, derived from how much world the map should show.
	// At 0.006 the half-extent covered 125m, so the village (~15m across) and
	// every NPC collapsed into a dozen pixels at the centre.
	const float MinimapRadiusM = 45.f;
	const float scale = (S * 0.5f) / (MinimapRadiusM * 100.f);

	// North-up projection.
	//
	// DO NOT "fix" this to the UE default of +X=north. Millhaven inherits the
	// screen-space convention of its Three.js prototype: **+X is east and +Y is
	// south**, so -Y is north. Every piece of world content agrees - BiomeAt()
	// puts "Northern Hills" at ay < -28, the cave marked "(north)" is at
	// y = -36, the cluster commented "(NE)" is +X/-Y, and the basin commented
	// "south-west" is -X/+Y. Under that convention X->x and Y->y already *is*
	// north-up, and rotating it is what breaks the map.
	auto Blip = [&](double WorldX, double WorldY, const FLinearColor& Col, float Size)
	{
		const float mx = cx + (float)((WorldX - P.X) * scale);
		const float my = cy + (float)((WorldY - P.Y) * scale);
		if (mx > X && mx < X + S && my > Y && my < Y + S)
		{
			Panel(mx - Size * 0.5f, my - Size * 0.5f, Size, Size, Col);
		}
	};

	// buildings - read from the generator's table, not a second hand-kept copy
	for (const FMillhavenBuildingDef& B : AMillhavenWorldGen::VillageBuildings())
	{
		Blip(B.At.X * 100.0, B.At.Y * 100.0, FLinearColor(0.78f, 0.44f, 0.25f, 1.f), 6.f);
	}
	// NPCs
	for (const TWeakObjectPtr<AMillhavenNPC>& Weak : AMillhavenNPC::All)
	{
		const AMillhavenNPC* N = Weak.Get();
		// The registry is process-global, so an NPC from another PIE world can
		// appear in it. Only draw the ones in this HUD's world.
		if (N && N->GetWorld() == GetWorld())
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

	// body text (wrapped to the panel's inner width)
	TArray<FString> Lines = WrapText(Node->Text, W - 40.f, 0.92f);
	float ty = Y + 74.f;
	for (const FString& L : Lines)
	{
		Text(L, X + 20.f, ty, TextCol, 0.92f);
		ty += 20.f;
	}

	// Options come from the character so the numbers shown here are exactly
	// the ones SelectOption() indexes - requirement-gated options are filtered
	// out of both by the same call.
	const TArray<const FDlgOption*> Options = C->GetAvailableOptions();
	float oy = ty + 8.f;
	for (int32 i = 0; i < Options.Num(); i++)
	{
		const FString Line = FString::Printf(TEXT("[%d]  %s"), i + 1, *Options[i]->Label);
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
