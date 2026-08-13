#include "MillhavenWorldGen.h"
#include "MillhavenNPC.h"

#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

// ---------------------------------------------------------------------------

AMillhavenWorldGen::AMillhavenWorldGen()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WorldMesh"));
	Mesh->SetupAttachment(SceneRoot);
	// Collision is cooked synchronously so the player never lands on a mesh
	// whose collision has not finished building.
	Mesh->bUseAsyncCooking = false;

	DecorMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DecorMesh"));
	DecorMesh->SetupAttachment(SceneRoot);
	DecorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
	WaterMesh->SetupAttachment(SceneRoot);
	WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CloudMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CloudMesh"));
	CloudMesh->SetupAttachment(SceneRoot);
	CloudMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Lighting components
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(SceneRoot);
	Sun->SetMobility(EComponentMobility::Movable);

	Sky = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	Sky->SetupAttachment(SceneRoot);
	Sky->SetMobility(EComponentMobility::Movable);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	Atmosphere->SetupAttachment(SceneRoot);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	Fog->SetupAttachment(SceneRoot);
}

// ---------------------------------------------------------------------------
// Shared: create a colored material instance from the engine's basic material.
// Uses the "Color" parameter AND fills vertex colors, so the world looks right
// whether the section renders via the param or via a vertex-color material.
// ---------------------------------------------------------------------------
UMaterialInterface* AMillhavenWorldGen::ResolveBaseMaterial()
{
	// Every mesh section is coloured two ways at once: a "Color" vector
	// parameter set on the MID, and per-vertex colours baked into the section.
	// We cannot know which of the two a given base material actually reads, so
	// prefer one that reads vertex colour - that is the channel we always
	// write. Falling through this list is safe: a missing package just loads
	// as null and we try the next one.
	static const TCHAR* const Candidates[] =
	{
		// 1. The hand-authored material from README section 5, if it was made.
		TEXT("/Game/M_VertexColor.M_VertexColor"),
		// 2. An engine material that renders vertex colour straight to base colour.
		TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly.VertexColorViewMode_ColorOnly"),
		// 3. The basic shape material, tinted through its "Color" parameter.
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"),
	};

	for (const TCHAR* Path : Candidates)
	{
		// LOAD_Quiet | LOAD_NoWarn: a miss here is expected, not a problem.
		UMaterialInterface* Found = LoadObject<UMaterialInterface>(
			nullptr, Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
		if (Found)
		{
			UE_LOG(LogTemp, Log, TEXT("Millhaven: base material resolved to '%s'."), Path);
			return Found;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("Millhaven: no usable base material found - the world will render untinted. ")
		TEXT("See README section 5 for the M_VertexColor workaround."));
	return UMaterial::GetDefaultMaterial(MD_Surface);
}

UMaterialInterface* AMillhavenWorldGen::GetBaseMaterial()
{
	if (!BaseMaterial)
	{
		BaseMaterial = ResolveBaseMaterial();
	}
	return BaseMaterial;
}

UMaterialInstanceDynamic* AMillhavenWorldGen::MakeColorMID(const FColor& C)
{
	UMaterialInterface* Base = GetBaseMaterial();
	if (!Base)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
	if (MID)
	{
		MID->SetVectorParameterValue(FName("Color"), FLinearColor::FromSRGBColor(C));
	}
	return MID;
}

float AMillhavenWorldGen::Prng(float Seed)
{
	const float X = FMath::Sin(Seed * 127.1f + 311.7f) * 43758.5453f;
	return X - FMath::FloorToFloat(X);
}

// Pure ground-height function (cm in, cm out). Everything agrees with this.
float AMillhavenWorldGen::TerrainHeight(float WX, float WY)
{
	const float ax = WX / 100.f;
	const float ay = WY / 100.f;
	const float d = FMath::Sqrt(ax * ax + ay * ay);
	float h = 0.f;
	const float vr = 16.f, slope = 22.f;
	if (d > vr)
	{
		const float f = FMath::Min(1.f, (d - vr) / slope);
		h = (FMath::Sin(ax * 0.18f) * FMath::Cos(ay * 0.18f) * 2.5f
		   + FMath::Sin(ax * 0.37f + ay * 0.25f) * 1.1f) * f;

		if (ay < -25.f) // northern hills
		{
			const float hf = FMath::Min(1.f, (-ay - 25.f) / 25.f);
			h += hf * (FMath::Sin(ax * 0.12f) * 3.5f + FMath::Cos(ay * 0.10f) * 4.f);
		}
		if (ax < -18.f && ay > 18.f) // south-west coast basin
		{
			const float cf = FMath::Min(1.f,
				FMath::Sqrt(FMath::Square(ax + 18.f) + FMath::Square(ay - 18.f)) / 22.f);
			h = h * (1.f - cf * 0.85f) - cf * 1.2f;
		}
		if (d > 62.f) h += (d - 62.f) * 0.07f; // rising rim
	}
	return h * 100.f;
}

FString AMillhavenWorldGen::BiomeAt(float WX, float WY)
{
	const float ax = WX / 100.f, ay = WY / 100.f;
	const float d = FMath::Sqrt(ax * ax + ay * ay);
	if (d < 14.f) return TEXT("Millhaven Village");
	if (ax < -18.f && ay > 14.f) return TEXT("Shellwater Harbour");
	if (ay < -28.f) return TEXT("Northern Hills");
	if (ax > 18.f && ay < -10.f) return TEXT("Pinewood Forest");
	if (d > 45.f) return TEXT("Outer Wilderness");
	return TEXT("Millhaven Valley");
}

FVector AMillhavenWorldGen::GroundPos(float AX, float AY, float LiftCm)
{
	const float WX = AX * 100.f, WY = AY * 100.f;
	return FVector(WX, WY, TerrainHeight(WX, WY) + LiftCm);
}

FMillhavenMeshBatch& AMillhavenWorldGen::B(const FColor& Color)
{
	for (auto& Pair : Batches)
	{
		if (Pair.Key == Color) return Pair.Value;
	}
	Batches.Add(TPair<FColor, FMillhavenMeshBatch>(Color, FMillhavenMeshBatch()));
	return Batches.Last().Value;
}

void AMillhavenWorldGen::CommitBatches(UProceduralMeshComponent* Target, bool bCollision)
{
	if (!Target)
	{
		return;
	}

	int32 Section = Target->GetNumSections();
	for (auto& Pair : Batches)
	{
		FMillhavenMeshBatch& Bat = Pair.Value;
		if (Bat.V.Num() == 0) continue;

		TArray<FLinearColor> Colors;
		Colors.Init(FLinearColor::FromSRGBColor(Pair.Key), Bat.V.Num());

		Target->CreateMeshSection_LinearColor(Section, Bat.V, Bat.T, Bat.N, Bat.UV,
			Colors, TArray<FProcMeshTangent>(), bCollision);

		if (UMaterialInstanceDynamic* MID = MakeColorMID(Pair.Key))
		{
			MIDs.Add(MID);
			Target->SetMaterial(Section, MID);
		}
		Section++;
	}
	Batches.Empty();
}

void AMillhavenWorldGen::BeginPlay()
{
	Super::BeginPlay();
	BuildWorld();
}

void AMillhavenWorldGen::BuildWorld()
{
	if (bWorldBuilt)
	{
		return;
	}
	bWorldBuilt = true;

	BuildEnvironmentLighting();

	BuildTerrain();
	BuildStructures();
	CommitBatches(Mesh, true);   // solid world (player walks on it)

	BuildVegetation();
	BuildScenery();
	CommitBatches(DecorMesh, false);

	BuildWater();
	BuildClouds();

	SpawnNPCs();
}

// ---------------------------------------------------------------------------
// Lighting - golden hour
// ---------------------------------------------------------------------------
void AMillhavenWorldGen::BuildEnvironmentLighting()
{
	// Tuned against the "terrain colour looks washed out" report. Three things
	// were pushing the image towards flat pale blue at once: a dim sun against
	// a full-strength sky light (little directional contrast), and ground-level
	// fog that tinted terrain the player was standing on rather than only the
	// distance. The sun/sky ratio now favours the sun, and the fog starts 25m
	// out so near geometry reads its true colour.
	if (Sun)
	{
		Sun->SetWorldRotation(FRotator(-38.f, -55.f, 0.f)); // low, warm angle
		Sun->SetLightColor(FLinearColor(1.0f, 0.82f, 0.55f));
		Sun->SetIntensity(9.0f);         // was 6.0
		Sun->SetCastShadows(true);
	}
	if (Atmosphere)
	{
		Atmosphere->SetRayleighScattering(FLinearColor(0.18f, 0.34f, 0.85f));
	}
	if (Sky)
	{
		Sky->SetMobility(EComponentMobility::Movable);
		Sky->SetRealTimeCapture(true);   // works with SkyAtmosphere, no baking
		Sky->SetIntensity(0.65f);        // was 1.0
		Sky->RecaptureSky();
	}
	if (Fog)
	{
		Fog->SetFogDensity(0.004f);      // was 0.008
		Fog->SetStartDistance(2500.f);   // 25m - was 0, i.e. fog on the player's feet
		Fog->SetFogInscatteringColor(FLinearColor(0.55f, 0.64f, 0.76f));
	}
}

// ---------------------------------------------------------------------------
// Terrain (+ sandy village paths)
// ---------------------------------------------------------------------------
void AMillhavenWorldGen::BuildTerrain()
{
	const FColor Grass(70, 150, 46);
	const FColor GrassDark(56, 128, 40);
	const FColor Sand(212, 168, 88);
	const FColor Highland(72, 96, 48);

	const int32 N = 84;
	const float sizeM = 180.f;
	const float half = sizeM * 0.5f;
	const float stepM = sizeM / N;

	auto Corner = [&](int32 i, int32 j)
	{
		const float ax = -half + i * stepM;
		const float ay = -half + j * stepM;
		return FVector(ax * 100.f, ay * 100.f, TerrainHeight(ax * 100.f, ay * 100.f));
	};

	for (int32 i = 0; i < N; i++)
	{
		for (int32 j = 0; j < N; j++)
		{
			const FVector A = Corner(i, j);
			const FVector Bv = Corner(i + 1, j);
			const FVector C = Corner(i + 1, j + 1);
			const FVector D = Corner(i, j + 1);

			const float avgZ = (float)((A.Z + Bv.Z + C.Z + D.Z) * 0.25);
			const float ax = (float)((A.X + C.X) * 0.5 / 100.0);
			const float ay = (float)((A.Y + C.Y) * 0.5 / 100.0);

			FColor Col = ((i + j) & 1) ? Grass : GrassDark;
			if (avgZ > 380.f) Col = Highland;

			// Sand fringes the water, keyed off the same waterline the water
			// tiles use - so the beach always meets the shore instead of the
			// two being placed by independent guesses, which is how the old
			// hardcoded beach ended up nowhere near the old water quad.
			const float CoastD = FMath::Sqrt(FMath::Square(ax + 18.f) + FMath::Square(ay - 18.f));
			if (CoastD < 38.f && avgZ < WaterlineCm + 80.f)
			{
				Col = Sand;
			}

			B(Col).AddQuad(A, Bv, C, D);
		}
	}

	// Sandy paths through the village (thin slabs just above ground)
	const FColor Path(214, 170, 90);
	auto AddPath = [&](float ax, float ay, float w, float d)
	{
		const FVector c = GroundPos(ax, ay, 3.f);
		B(Path).AddHQuad(c, w * 100.f, d * 100.f);
	};
	AddPath(0, 0, 14, 22);
	AddPath(8, 3, 9, 5);
	AddPath(-8, 3, 7, 5);
	AddPath(0, -7, 8, 10);
}

// ---------------------------------------------------------------------------
// Structures: buildings, well, dock, cave, fences, rocks, stalls (solid)
// ---------------------------------------------------------------------------
void AMillhavenWorldGen::AddBuilding(float AX, float AY, float W, float D,
                                     const FColor& WallColor, const FColor& RoofColor)
{
	const FVector base = GroundPos(AX, AY, 0.f);
	const float wall = 280.f, wCm = W * 100.f, dCm = D * 100.f;

	// Walls
	B(WallColor).AddBox(base + FVector(0.0, 0.0, wall * 0.5), FVector(wCm, dCm, wall));
	// Roof (4-sided pyramid)
	B(RoofColor).AddCone(base + FVector(0.0, 0.0, wall), FMath::Max(wCm, dCm) * 0.78f, 170.f, 4, 45.f);
	// Door
	B(FColor(90, 46, 16)).AddBox(base + FVector(dCm * 0.5 + 4.0, 0.0, 65.0), FVector(9.0, 68.0, 130.0));
	// Windows
	const FColor Win(184, 218, 240);
	B(Win).AddBox(base + FVector(dCm * 0.5 + 4.0, -95.0, 145.0), FVector(9.0, 52.0, 52.0));
	B(Win).AddBox(base + FVector(dCm * 0.5 + 4.0,  95.0, 145.0), FVector(9.0, 52.0, 52.0));
	// Chimney
	B(FColor(136, 112, 96)).AddBox(base + FVector(-wCm * 0.28, wCm * 0.25, wall + 125.0),
		FVector(32.0, 32.0, 85.0));
}

void AMillhavenWorldGen::AddFenceRun(const TArray<FVector2D>& Points)
{
	const FColor Wood(154, 112, 80);
	for (int32 k = 0; k < Points.Num() - 1; k++)
	{
		const FVector2D P0 = Points[k], P1 = Points[k + 1];
		const float len = (float)FVector2D::Distance(P0, P1);
		if (len < KINDA_SMALL_NUMBER)
		{
			continue; // duplicated point - nothing to fence
		}
		const int32 steps = FMath::Max(1, FMath::CeilToInt(len / 1.5f));
		for (int32 s = 0; s <= steps; s++)
		{
			const float t = (float)s / (float)steps;
			const FVector2D p = FMath::Lerp(P0, P1, (double)t);
			B(Wood).AddBox(GroundPos((float)p.X, (float)p.Y, 52.f), FVector(12.0, 12.0, 105.0));
		}
		// top rail
		const FVector2D mid = (P0 + P1) * 0.5;
		const float yaw = (float)FMath::RadiansToDegrees(FMath::Atan2(P1.Y - P0.Y, P1.X - P0.X));
		B(Wood).AddBox(GroundPos((float)mid.X, (float)mid.Y, 72.f),
			FVector(len * 100.f, 9.0, 9.0), yaw);
	}
}

void AMillhavenWorldGen::AddStall(float AX, float AY)
{
	const FVector base = GroundPos(AX, AY, 0.f);
	B(FColor(208, 64, 32)).AddBox(base + FVector(0.0, 0.0, 190.0), FVector(280.0, 200.0, 12.0)); // canopy
	const FColor Post(138, 85, 48);
	for (int32 s = 0; s < 4; s++)
	{
		const double ox = (s < 2 ? -120.0 : 120.0);
		const double oy = ((s % 2) ? -90.0 : 90.0);
		B(Post).AddBox(base + FVector(ox, oy, 95.0), FVector(12.0, 12.0, 190.0));
	}
	const FColor Goods[3] = { FColor(204,48,48), FColor(232,112,32), FColor(240,208,32) };
	for (int32 g = 0; g < 3; g++)
	{
		B(Goods[g]).AddBox(base + FVector(-60.0 + g * 60.0, 0.0, 208.0), FVector(30.0, 30.0, 28.0));
	}
}

const TArray<FMillhavenBuildingDef>& AMillhavenWorldGen::VillageBuildings()
{
	// Function-local static of plain data - no UObjects here, so unlike the
	// material caches this one is invisible to the GC in the harmless way.
	static const TArray<FMillhavenBuildingDef> Defs = {
		{ FVector2D( -7.0, -2.0), 4.4f, 3.8f, FColor(212,144,96),  FColor(139,56,40) },
		{ FVector2D(  6.0, -3.5), 5.4f, 4.4f, FColor(200,168,96),  FColor(122,51,24) },
		{ FVector2D( -6.0,  6.0), 4.0f, 3.4f, FColor(184,144,80),  FColor(107,46,20) },
		{ FVector2D(  7.5,  6.0), 4.8f, 4.0f, FColor(208,168,104), FColor(144,60,24) },
		{ FVector2D(  0.0, -9.0), 6.0f, 4.5f, FColor(192,136,72),  FColor(160,56,32) },
		{ FVector2D(-12.0, -5.0), 3.8f, 3.2f, FColor(184,120,64),  FColor(106,40,16) },
	};
	return Defs;
}

void AMillhavenWorldGen::BuildStructures()
{
	// Village houses. The HUD minimap draws from this same table.
	for (const FMillhavenBuildingDef& Def : VillageBuildings())
	{
		AddBuilding((float)Def.At.X, (float)Def.At.Y, Def.W, Def.D, Def.Wall, Def.Roof);
	}

	// Market stalls
	AddStall(9.f, 2.f); AddStall(11.5f, 4.f); AddStall(11.f, 0.5f);

	// Well
	{
		const FVector w = GroundPos(0.f, -4.5f, 0.f);
		B(FColor(128,120,104)).AddCylinder(w, 90.f, 90.f, 52.f, 8);
		B(FColor(40,112,184)).AddCylinder(w + FVector(0.0, 0.0, 50.0), 72.f, 72.f, 6.f, 8);
		B(FColor(122,80,40)).AddBox(w + FVector(0.0, -52.0, 105.0), FVector(13.0, 13.0, 145.0));
		B(FColor(122,80,40)).AddBox(w + FVector(0.0,  52.0, 105.0), FVector(13.0, 13.0, 145.0));
		B(FColor(122,80,40)).AddBox(w + FVector(0.0,   0.0, 179.0), FVector(13.0, 126.0, 13.0));
	}

	// Dock / pier at Shellwater Harbour
	{
		const FColor Plank(160,120,74), Leg(122,85,48);
		const FVector2D DockAt = DockM();
		const FVector d = GroundPos((float)DockAt.X, (float)DockAt.Y, 0.f);
		const double DeckThick = 22.0;
		const double DeckUnder = (double)DockDeckTopCm - DeckThick;
		B(Plank).AddBox(FVector(d.X, d.Y, (double)DockDeckTopCm - DeckThick * 0.5),
			FVector(350.0, 1400.0, DeckThick));

		// Legs run from the bay floor up to the underside of the decking. They
		// used to be a fixed 2.5m block that both floated and poked through.
		for (int32 i = 0; i < 5; i++)
		{
			const double LegY = d.Y - 600.0 + i * 300.0;
			const float LegGround = TerrainHeight((float)d.X, (float)LegY);
			const double LegH = FMath::Max(20.0, DeckUnder - (double)LegGround);
			B(Leg).AddBox(FVector(d.X, LegY, DeckUnder - LegH * 0.5),
				FVector(26.0, 26.0, LegH));
		}

		// boat hull + mast, moored at the landmark the bay quest points to
		const FVector2D BoatAt = BayWatchM();
		const double BoatX = BoatAt.X * 100.0, BoatY = BoatAt.Y * 100.0;
		B(FColor(192,120,64)).AddBox(FVector(BoatX, BoatY, 40.0), FVector(300.0, 600.0, 70.0), 18.f);
		B(FColor(154,120,80)).AddCylinder(FVector(BoatX, BoatY, 60.0), 8.f, 8.f, 300.f, 4);

		// A boardwalk back to dry land. Once the water started following the
		// terrain the pier became an island - the old water plane covered the
		// whole area, so nothing revealed the gap. The landing point is
		// searched for rather than hardcoded, using the same "is this wet?"
		// test BuildWater uses, so the two can never disagree.
		{
			const FVector2D Village(0.0, 0.0);
			FVector2D Landfall = DockAt;
			const int32 Probes = 80;
			for (int32 s = 1; s <= Probes; s++)
			{
				const FVector2D P = FMath::Lerp(DockAt, Village, (double)s / (double)Probes);
				Landfall = P;
				if (!IsWaterAt((float)P.X, (float)P.Y))
				{
					break;
				}
			}

			const float SpanM = (float)FVector2D::Distance(DockAt, Landfall);
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(SpanM / 1.2f));

			// The walkway ramps from deck height down to just above the shore.
			// Held level it would meet the beach as a ~50cm ledge, which is
			// over UE's 45cm default step height - an invisible wall.
			const float ShoreGround =
				TerrainHeight((float)Landfall.X * 100.f, (float)Landfall.Y * 100.f);
			const double ShoreTop = (double)ShoreGround + 18.0;

			for (int32 s = 0; s <= Steps; s++)
			{
				const double T = (double)s / (double)Steps;
				const FVector2D P = FMath::Lerp(DockAt, Landfall, T);
				const double TopZ = FMath::Lerp((double)DockDeckTopCm, ShoreTop, T);

				B(Plank).AddBox(FVector(P.X * 100.0, P.Y * 100.0, TopZ - 10.0),
					FVector(200.0, 200.0, 20.0));

				// A post every other section, so the walkway is not floating.
				if ((s % 2) == 0)
				{
					const float G = TerrainHeight((float)P.X * 100.f, (float)P.Y * 100.f);
					const double PostTop = TopZ - 20.0;
					const double PostH = FMath::Max(10.0, PostTop - (double)G);
					B(Leg).AddBox(FVector(P.X * 100.0, P.Y * 100.0, PostTop - PostH * 0.5),
						FVector(16.0, 16.0, PostH));
				}
			}
		}
	}

	// Cave entrance (north)
	{
		const FColor Rock(96,104,88);
		const FVector2D CaveAt = CaveMouthM();
		const FVector c = GroundPos((float)CaveAt.X, (float)CaveAt.Y, 0.f);
		B(Rock).AddBox(c + FVector(0.0, -150.0, 190.0), FVector(150.0, 120.0, 380.0));
		B(Rock).AddBox(c + FVector(0.0,  150.0, 190.0), FVector(150.0, 120.0, 380.0));
		B(Rock).AddBox(c + FVector(0.0,    0.0, 375.0), FVector(150.0, 440.0, 150.0));
		B(FColor(6,4,10)).AddBox(c + FVector(78.0, 0.0, 130.0), FVector(6.0, 240.0, 260.0)); // dark mouth
		for (int32 i = 0; i < 8; i++)
		{
			const float rs = 35.f + Prng(i * 7.f) * 55.f;
			const double ox = (Prng(i * 13.f) - 0.5f) * 500.0;
			const double oy = (Prng(i * 19.f) - 0.5f) * 400.0;
			B(FColor(112,120,96)).AddBox(c + FVector(ox, oy, rs * 0.5), FVector(rs, rs, rs),
				Prng(i * 5.f) * 90.f);
		}
	}

	// Fences
	AddFenceRun({ {2.5, 1.0}, {2.5, 9.0}, {11.0, 9.0}, {11.0, 1.0} });
	AddFenceRun({ {-14.0, -1.0}, {-14.0, 9.0}, {-10.0, 9.0} });

	// Scattered rocks (solid)
	for (int32 i = 0; i < 24; i++)
	{
		const float a = Prng(i * 41.f) * 2.f * PI;
		const float dist = 14.f + Prng(i * 17.f) * 42.f;
		const float rx = FMath::Cos(a) * dist, ry = FMath::Sin(a) * dist;
		if (rx < -12.f && ry > 16.f) continue; // keep water clear
		const float sc = 25.f + Prng(i * 7.f) * 60.f;
		B(FColor(128,120,104)).AddBox(GroundPos(rx, ry, sc * 0.3f),
			FVector(sc, sc, sc), Prng(i * 5.f) * 90.f);
	}
}

// ---------------------------------------------------------------------------
// Vegetation (no collision)
// ---------------------------------------------------------------------------
void AMillhavenWorldGen::AddTree(float AX, float AY, float Scale)
{
	const FVector base = GroundPos(AX, AY, 0.f);
	const float s = Scale;
	// trunk
	B(FColor(122,85,48)).AddCylinder(base, 20.f * s, 13.f * s, 135.f * s, 5);
	// three cone layers
	const FColor Greens[3] = { FColor(36,96,24), FColor(42,112,32), FColor(26,78,20) };
	for (int32 i = 0; i < 3; i++)
	{
		const float r = (145.f - i * 26.f) * s;
		const float h = (175.f - i * 20.f) * s;
		const double z = (150.0 + i * 105.0) * s;
		B(Greens[i]).AddCone(base + FVector(0.0, 0.0, z), r, h, 6 + i, i * 30.f);
	}
}

void AMillhavenWorldGen::BuildVegetation()
{
	// General scattered trees, keeping village core and water clear
	TArray<FVector2D> placed;
	for (int32 i = 0; i < 120; i++)
	{
		const float a = Prng(i * 17.3f) * 2.f * PI;
		const float dist = 16.f + Prng(i * 9.7f) * 55.f;
		const FVector2D p(FMath::Cos(a) * dist, FMath::Sin(a) * dist);
		bool tooClose = false;
		for (const FVector2D& q : placed)
		{
			if (FVector2D::Distance(p, q) < 2.6) { tooClose = true; break; }
		}
		if (tooClose) continue;
		if (p.X < -12.0 && p.Y > 20.0) continue; // water
		placed.Add(p);
		AddTree((float)p.X, (float)p.Y, 0.7f + Prng(i * 3.f) * 0.6f);
	}
	// Dense Pinewood cluster (NE)
	for (int32 i = 0; i < 28; i++)
	{
		AddTree(18.f + Prng(i * 11.f) * 25.f, -20.f - Prng(i * 7.f) * 28.f, 0.8f + Prng(i * 3.f) * 0.5f);
	}

	// Bushes
	for (int32 i = 0; i < 40; i++)
	{
		const float a = Prng(i * 5.7f) * 2.f * PI;
		const float dist = 12.f + Prng(i * 11.f) * 35.f;
		const float bx = FMath::Cos(a) * dist, by = FMath::Sin(a) * dist;
		if (bx < -12.f && by > 18.f) continue;
		const float r = 30.f + Prng(i * 3.f) * 28.f;
		B(FColor(36,96,20)).AddBox(GroundPos(bx, by, r * 0.5f), FVector(r, r, r), Prng((float)i) * 45.f);
	}
	// Flowers
	const FColor Fl[4] = { FColor(240,48,96), FColor(255,204,0), FColor(255,96,32), FColor(224,32,224) };
	for (int32 i = 0; i < 60; i++)
	{
		const float a = Prng(i * 31.f) * 2.f * PI;
		const float dist = 10.f + Prng(i * 17.f) * 40.f;
		const float fx = FMath::Cos(a) * dist, fy = FMath::Sin(a) * dist;
		if (fx < -12.f && fy > 16.f) continue;
		B(Fl[i % 4]).AddCone(GroundPos(fx, fy, 0.f), 18.f, 24.f, 5);
	}
}

// ---------------------------------------------------------------------------
// Distant scenery: mountains with snow caps (no collision)
// ---------------------------------------------------------------------------
void AMillhavenWorldGen::BuildScenery()
{
	const FColor MtC[5] = { FColor(74,112,64), FColor(58,96,56), FColor(80,88,72),
	                        FColor(64,88,64), FColor(96,120,80) };
	for (int32 i = 0; i < 14; i++)
	{
		const float a = (i / 14.f) * 2.f * PI + 0.2f;
		const float dist = 80.f + Prng(i * 13.f) * 30.f;
		const float sz = 2000.f + Prng(i * 7.f) * 2800.f;
		const FVector at(FMath::Cos(a) * dist * 100.f, FMath::Sin(a) * dist * 100.f, -500.f);
		B(MtC[i % 5]).AddCone(at, sz, sz * 0.9f, 5, Prng(i * 3.f) * 180.f);
		if (Prng(i * 7.f) > 0.4f)
		{
			B(FColor(232,240,248)).AddCone(at + FVector(0.0, 0.0, sz * 0.9 - 200.0),
				sz * 0.32f, sz * 0.42f, 5);
		}
	}
}

// ---------------------------------------------------------------------------
// Water & clouds (animated in Tick)
// ---------------------------------------------------------------------------
void AMillhavenWorldGen::BuildWater()
{
	// A grid of tiles clipped against the terrain, not one big quad.
	//
	// The previous version was a hard-edged 48m square at a fixed height. Its
	// inland corner reached to about (-6m, 8m), which is inside the flat
	// village disc where TerrainHeight() returns exactly 0 - so a blue plane
	// sat 5cm above green grass roughly 10m from the village centre, with no
	// shoreline anywhere near it.
	FMillhavenMeshBatch W;

	// Bounds live on the class so IsWaterAt() answers for the same rectangle
	// this loop fills - without that it calls any low ground "water", the cave
	// hollow included.
	const float MinAX = WaterMinAX, MaxAX = WaterMaxAX;
	const float MinAY = WaterMinAY, MaxAY = WaterMaxAY;
	const float TileM = 2.f;

	const int32 NX = FMath::CeilToInt((MaxAX - MinAX) / TileM);
	const int32 NY = FMath::CeilToInt((MaxAY - MinAY) / TileM);

	auto GroundAtM = [](float AX, float AY)
	{
		return TerrainHeight(AX * 100.f, AY * 100.f);
	};

	for (int32 i = 0; i < NX; i++)
	{
		for (int32 j = 0; j < NY; j++)
		{
			const float ax0 = MinAX + i * TileM;
			const float ay0 = MinAY + j * TileM;
			const float ax1 = ax0 + TileM;
			const float ay1 = ay0 + TileM;

			const float Highest = FMath::Max(
				FMath::Max(GroundAtM(ax0, ay0), GroundAtM(ax1, ay0)),
				FMath::Max(GroundAtM(ax1, ay1), GroundAtM(ax0, ay1)));

			if (Highest > WaterlineCm - MinWaterDepthCm)
			{
				continue; // shore or dry land
			}

			const double Z = (double)WaterlineCm;
			W.AddQuad(
				FVector(ax0 * 100.0, ay0 * 100.0, Z),
				FVector(ax1 * 100.0, ay0 * 100.0, Z),
				FVector(ax1 * 100.0, ay1 * 100.0, Z),
				FVector(ax0 * 100.0, ay1 * 100.0, Z));
		}
	}

	if (W.V.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Millhaven: no water tiles cleared the waterline - the bay will be dry."));
		return;
	}

	TArray<FLinearColor> Cols;
	Cols.Init(FLinearColor(0.16f, 0.44f, 0.72f), W.V.Num());
	WaterMesh->CreateMeshSection_LinearColor(0, W.V, W.T, W.N, W.UV, Cols,
		TArray<FProcMeshTangent>(), false);
	if (UMaterialInstanceDynamic* MID = MakeColorMID(FColor(40, 112, 184)))
	{
		MIDs.Add(MID);
		WaterMesh->SetMaterial(0, MID);
	}
}

void AMillhavenWorldGen::BuildClouds()
{
	FMillhavenMeshBatch Cl;
	for (int32 i = 0; i < 14; i++)
	{
		const double cx = (Prng(i * 23.f) - 0.5f) * 16000.0;
		const double cy = (Prng(i * 31.f) - 0.5f) * 16000.0;
		const double cz = 1800.0 + Prng(i * 5.f) * 1400.0;
		const int32 blobs = 3 + (int32)(Prng(i * 7.f) * 4.f);
		for (int32 j = 0; j < blobs; j++)
		{
			// Seeds must not collapse when i or j is 0, so mix them with
			// distinct primes rather than multiplying them together.
			const float s0 = i * 37.f + j * 11.f;
			const float r = 250.f + Prng(s0 + 1.f) * 250.f;
			const FVector bp(cx + (Prng(s0 + 2.f) - 0.5f) * 900.0,
			                 cy + (Prng(s0 + 4.f) - 0.5f) * 600.0,
			                 cz + Prng(s0 + 3.f) * 200.0);
			Cl.AddBox(bp, FVector(r, r, r * 0.7f));
		}
	}
	TArray<FLinearColor> Cols;
	Cols.Init(FLinearColor(0.98f, 0.99f, 1.f), Cl.V.Num());
	CloudMesh->CreateMeshSection_LinearColor(0, Cl.V, Cl.T, Cl.N, Cl.UV, Cols,
		TArray<FProcMeshTangent>(), false);
	if (UMaterialInstanceDynamic* MID = MakeColorMID(FColor(250, 252, 255)))
	{
		MIDs.Add(MID);
		CloudMesh->SetMaterial(0, MID);
	}
}

// ---------------------------------------------------------------------------
// NPCs + dialogue trees
// ---------------------------------------------------------------------------
void AMillhavenWorldGen::SpawnNPCs()
{
	UWorld* W = GetWorld();
	if (!W) return;

	// Collected so every dialogue tree can be checked once they are all built.
	TArray<AMillhavenNPC*> Spawned;

	// NOTE: the role parameter must not be called "Role" - that shadows
	// AActor::Role (the replication role), which is a C4458 error here.
	auto Spawn = [&](float AX, float AY, const FString& InName, const FString& InRole,
	                 FColor Body, FColor Skin, FColor Hair, float LiftCm = 0.f) -> AMillhavenNPC*
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		P.Owner = this;
		const FVector Loc = GroundPos(AX, AY, LiftCm);
		AMillhavenNPC* N = W->SpawnActor<AMillhavenNPC>(AMillhavenNPC::StaticClass(), Loc, FRotator::ZeroRotator, P);
		if (N)
		{
			N->Init(InName, InRole, Body, Skin, Hair);
			Spawned.Add(N);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Millhaven: failed to spawn NPC '%s'."), *InName);
		}
		return N;
	};

	// --- Baker Maren ---
	if (AMillhavenNPC* N = Spawn(-5.5f, 5.2f, TEXT("Baker Maren"), TEXT("Village Baker"),
		FColor(204,104,64), FColor(244,208,160), FColor(128,64,24)))
	{
		N->AddNode("start", TEXT("Oh! A traveler in Millhaven - what a surprise! I'm Maren. The Everloaf Festival is nearly here. I need golden wheat from Farmer Aldric, east of the well. Would you fetch it for me?"));
		N->AddOption("start", TEXT("I'll help find the wheat!"), "accept");
		N->AddOption("start", TEXT("What is the Everloaf Festival?"), "festival");
		N->AddOption("start", TEXT("Nice to meet you, goodbye!"), "x");

		N->AddNode("festival", TEXT("Every harvest moon we bake a ceremonial loaf taller than a child - and share it with every soul in the valley! It keeps the winter frost from the heart."));
		N->AddOption("festival", TEXT("I'd love to help!"), "accept");
		N->AddOption("festival", TEXT("Sounds wonderful, goodbye!"), "x");

		N->AddNode("accept", TEXT("Wonderful! Aldric's east of the well - look for the green overalls. Bring me five bundles and I'll bake you something you won't soon forget!"));
		{
			FDlgOption Opt;
			Opt.Label = TEXT("I'll find him right away!");
			Opt.Next = "x";
			Opt.QuestId = "GetWheat";
			Opt.QuestName = TEXT("Get the Wheat");
			Opt.QuestObjective = TEXT("Find Farmer Aldric east of the well");
			// Cannot be taken twice, and disappears once Aldric hands it over.
			Opt.ForbidsQuest = "GetWheat";
			N->AddOption("accept", Opt);
		}
		// Unconditional, so the node still has something to pick once the
		// option above has been used up. Every gated node needs one of these.
		N->AddOption("accept", TEXT("Goodbye, Maren."), "x");

		// The return leg: only offered once Aldric has sent you back.
		{
			FDlgOption Opt;
			Opt.Label = TEXT("I have your golden wheat.");
			Opt.Next = "delivered";
			Opt.RequiresQuest = "DeliverWheat";
			N->AddOption("start", Opt);
		}
		N->AddNode("delivered", TEXT("Five bundles, and every stalk gold as a summer evening! Bless you, traveler. The Everloaf will rise higher than the chapel door this year - and the first slice is yours."));
		{
			FDlgOption Opt;
			Opt.Label = TEXT("Happy to help. Enjoy the festival!");
			Opt.Next = "x";
			Opt.CompletesQuest = "DeliverWheat";
			N->AddOption("delivered", Opt);
		}
	}

	// --- Farmer Aldric ---
	if (AMillhavenNPC* N = Spawn(5.2f, 3.8f, TEXT("Farmer Aldric"), TEXT("Local Farmer"),
		FColor(80,136,40), FColor(232,192,144), FColor(96,56,24)))
	{
		N->AddNode("start", TEXT("Morning! Fine weather for crops. Name's Aldric. I've been up since before the sun. What brings you to my corner of Millhaven?"));
		// Only comes up once Maren has actually asked for the wheat.
		{
			FDlgOption Opt;
			Opt.Label = TEXT("Maren sent me for wheat.");
			Opt.Next = "wheat";
			Opt.RequiresQuest = "GetWheat";
			N->AddOption("start", Opt);
		}
		N->AddOption("start", TEXT("Tell me about your farm."), "farm");
		N->AddOption("start", TEXT("Just exploring. Goodbye!"), "x");

		N->AddNode("wheat", TEXT("Ah - for the festival! My golden wheat is ready and waiting. Here - five bundles, cut this morning and still warm. Take them straight back to Maren before the dew lifts."));
		{
			// Hands the first quest off to the second, so the pair reads as one
			// errand across two NPCs.
			FDlgOption Opt;
			Opt.Label = TEXT("Thank you, Aldric!");
			Opt.Next = "x";
			Opt.CompletesQuest = "GetWheat";
			Opt.QuestId = "DeliverWheat";
			Opt.QuestName = TEXT("Deliver the Wheat");
			Opt.QuestObjective = TEXT("Bring the golden wheat back to Baker Maren");
			N->AddOption("wheat", Opt);
		}

		N->AddNode("farm", TEXT("Twenty-two years on this land. Wheat, barley, pumpkins - and the finest moonmelons in the valley grow near the old creek. Soil here's rich as midnight."));
		N->AddOption("farm", TEXT("What grows near the cave?"), "cave");
		N->AddOption("farm", TEXT("Impressive! Goodbye."), "x");

		N->AddNode("cave", TEXT("Silverleaf! Rare stuff, grows only in the cave's entrance light. Worth a fortune in the city. But mind the shadows beyond - I haven't gone past the first chamber in years."));
		N->AddOption("cave", TEXT("I'll be careful. Goodbye."), "x");
	}

	// --- Elder Sylva ---
	if (AMillhavenNPC* N = Spawn(-1.f, -6.f, TEXT("Elder Sylva"), TEXT("Village Elder"),
		FColor(112,64,170), FColor(236,208,176), FColor(192,192,208)))
	{
		N->AddNode("start", TEXT("Hmm. A wanderer arrives in Millhaven... I am Sylva, keeper of the old stories. Nine hundred years this valley has stood. Shall I tell you what I know, traveler?"));
		N->AddOption("start", TEXT("Tell me the valley's history."), "history");
		N->AddOption("start", TEXT("What lurks in the cave?"), "cave");
		N->AddOption("start", TEXT("Farewell, Elder."), "x");

		N->AddNode("history", TEXT("The first settlers followed a great river from the east - carrying seeds of the Everloaf tree. They planted it on the hillside. Every forest you see grew from its seeds."));
		N->AddOption("history", TEXT("What became of the settlers?"), "settlers");
		N->AddOption("history", TEXT("What lurks in the cave?"), "cave");
		N->AddOption("history", TEXT("Remarkable. Goodbye."), "x");

		N->AddNode("settlers", TEXT("They thrived for three centuries. Then the Stone Wolves came from the peaks - creatures of living granite. Half fled to the sea. We are the descendants of those who stayed and built."));
		N->AddOption("settlers", TEXT("Are the wolves still here?"), "cave");
		N->AddOption("settlers", TEXT("A powerful history. Goodbye."), "x");

		N->AddNode("cave", TEXT("The cave is older than the village. Phosphorescent moss marks the safe passage. Follow only the glowing stones. Whatever you do... do not go past the third chamber after dark."));
		{
			// Completed by arriving - see AMillhavenCharacter::UpdateLocationQuests.
			FDlgOption Opt;
			Opt.Label = TEXT("I'll be careful. Goodbye.");
			Opt.Next = "x";
			Opt.QuestId = "GlowingDeep";
			Opt.QuestName = TEXT("The Glowing Deep");
			Opt.QuestObjective = TEXT("Find the cave entrance north of the village");
			N->AddOption("cave", Opt);
		}
	}

	// --- Captain Wren ---
	// On his own pier, at the landward end. At the old spot he stood on the bay
	// floor a metre under the waterline; putting him at the *seaward* end would
	// be worse still, because "Trouble in the Bay" completes on arrival at the
	// moored boat and would finish the instant he handed it over.
	const float WrenAX = -25.f, WrenAY = 20.f;
	const float WrenLift = DockDeckTopCm - TerrainHeight(WrenAX * 100.f, WrenAY * 100.f);
	if (AMillhavenNPC* N = Spawn(WrenAX, WrenAY, TEXT("Captain Wren"), TEXT("Harbour Master"),
		FColor(32,80,128), FColor(232,200,160), FColor(96,56,32), WrenLift))
	{
		N->AddNode("start", TEXT("Ahoy! First time to our little harbour? The fishing's been poor this week - something spooks the catch. Where are you headed, stranger?"));
		N->AddOption("start", TEXT("Just exploring the valley."), "explore");
		N->AddOption("start", TEXT("What spooked the fish?"), "fish");
		N->AddOption("start", TEXT("Safe travels. Goodbye!"), "x");

		N->AddNode("explore", TEXT("Ha! A wanderer. Best advice: follow the coast north for the salt flats - beautiful views. South gets rocky fast. And for gods' sake avoid the cave at night."));
		N->AddOption("explore", TEXT("Why avoid the cave at night?"), "cave");
		N->AddOption("explore", TEXT("Good to know. Goodbye!"), "x");

		N->AddNode("fish", TEXT("Started three nights ago. Fish just vanish from the bay. I heard a low rumbling from under the water - my old boat's timbers shook. Something is down there."));
		{
			// Also completed by arriving, at the dock.
			FDlgOption Opt;
			Opt.Label = TEXT("That's alarming. Goodbye.");
			Opt.Next = "x";
			Opt.QuestId = "BayTrouble";
			Opt.QuestName = TEXT("Trouble in the Bay");
			Opt.QuestObjective = TEXT("Investigate what is scaring the fish at Shellwater Harbour");
			N->AddOption("fish", Opt);
		}

		N->AddNode("cave", TEXT("The cave mouth glows at midnight. Blue light, like starfire. Elder Sylva says it's safe if you follow the moss-lights. I say let sleeping wolves lie."));
		N->AddOption("cave", TEXT("Good advice. Goodbye."), "x");
	}

	// Every tree is complete now, so dangling targets and unreachable nodes
	// show up in the log on the first run rather than in play.
	for (const AMillhavenNPC* N : Spawned)
	{
		N->ValidateDialogue();
	}
}

// ---------------------------------------------------------------------------
void AMillhavenWorldGen::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TimeAccum += DeltaTime;

	if (WaterMesh)
	{
		WaterMesh->SetRelativeLocation(FVector(0.0, 0.0, FMath::Sin(TimeAccum * 0.8f) * 4.0));
	}

	if (CloudMesh)
	{
		FVector L = CloudMesh->GetRelativeLocation();
		L.X += DeltaTime * 55.0;
		if (L.X > 8000.0) L.X = -8000.0;
		CloudMesh->SetRelativeLocation(L);
	}
}
