#include "MillhavenWorldGen.h"
#include "MillhavenNPC.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------
// Shared: create a colored material instance from the engine's basic material.
// Uses the "Color" parameter AND fills vertex colors, so the world looks right
// whether the section renders via the param or via a vertex-color material.
// ---------------------------------------------------------------------------
static UMaterialInterface* GetBaseColorMaterial()
{
	static UMaterialInterface* Base = nullptr;
	if (!Base)
	{
		Base = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	return Base;
}

static UMaterialInstanceDynamic* MakeColorMID(UObject* Outer, const FColor& C)
{
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(GetBaseColorMaterial(), Outer);
	if (MID)
	{
		MID->SetVectorParameterValue(FName("Color"), FLinearColor::FromSRGBColor(C));
	}
	return MID;
}

// ---------------------------------------------------------------------------

AMillhavenWorldGen::AMillhavenWorldGen()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WorldMesh"));
	Mesh->SetupAttachment(SceneRoot);

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

FMeshBatch& AMillhavenWorldGen::B(const FColor& Color)
{
	for (auto& Pair : Batches)
	{
		if (Pair.Key == Color) return Pair.Value;
	}
	Batches.Add(TPair<FColor, FMeshBatch>(Color, FMeshBatch()));
	return Batches.Last().Value;
}

void AMillhavenWorldGen::CommitBatches(UProceduralMeshComponent* Target, bool bCollision)
{
	int32 Section = Target->GetNumSections();
	for (auto& Pair : Batches)
	{
		FMeshBatch& Bat = Pair.Value;
		if (Bat.V.Num() == 0) continue;

		TArray<FLinearColor> Colors;
		Colors.Init(FLinearColor::FromSRGBColor(Pair.Key), Bat.V.Num());

		Target->CreateMeshSection_LinearColor(Section, Bat.V, Bat.T, Bat.N, Bat.UV,
			Colors, TArray<FProcMeshTangent>(), bCollision);

		UMaterialInstanceDynamic* MID = MakeColorMID(this, Pair.Key);
		MIDs.Add(MID);
		Target->SetMaterial(Section, MID);
		Section++;
	}
	Batches.Empty();
}

void AMillhavenWorldGen::BeginPlay()
{
	Super::BeginPlay();

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
	if (Sun)
	{
		Sun->SetWorldRotation(FRotator(-38.f, -55.f, 0.f)); // low, warm angle
		Sun->SetLightColor(FLinearColor(1.0f, 0.82f, 0.55f));
		Sun->SetIntensity(6.0f);
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
		Sky->SetIntensity(1.0f);
		Sky->RecaptureSky();
	}
	if (Fog)
	{
		Fog->SetFogDensity(0.008f);
		Fog->SetFogInscatteringColor(FLinearColor(0.65f, 0.78f, 0.9f));
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

			const float avgZ = (A.Z + Bv.Z + C.Z + D.Z) * 0.25f;
			const float ax = (A.X + C.X) * 0.5f / 100.f;
			const float ay = (A.Y + C.Y) * 0.5f / 100.f;

			FColor Col = ((i + j) & 1) ? Grass : GrassDark;
			if (avgZ > 380.f) Col = Highland;
			if (avgZ < 12.f && ax < -16.f && ay > 16.f) Col = Sand; // beach

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
	B(WallColor).AddBox(base + FVector(0, 0, wall * 0.5f), FVector(wCm, dCm, wall));
	// Roof (4-sided pyramid)
	B(RoofColor).AddCone(base + FVector(0, 0, wall), FMath::Max(wCm, dCm) * 0.78f, 170.f, 4, 45.f);
	// Door
	B(FColor(90, 46, 16)).AddBox(base + FVector(dCm * 0.5f + 4.f, 0, 65.f), FVector(9.f, 68.f, 130.f));
	// Windows
	const FColor Win(184, 218, 240);
	B(Win).AddBox(base + FVector(dCm * 0.5f + 4.f, -95.f, 145.f), FVector(9.f, 52.f, 52.f));
	B(Win).AddBox(base + FVector(dCm * 0.5f + 4.f,  95.f, 145.f), FVector(9.f, 52.f, 52.f));
	// Chimney
	B(FColor(136, 112, 96)).AddBox(base + FVector(-wCm * 0.28f, wCm * 0.25f, wall + 125.f),
		FVector(32.f, 32.f, 85.f));
}

void AMillhavenWorldGen::AddFenceRun(const TArray<FVector2D>& Points)
{
	const FColor Wood(154, 112, 80);
	for (int32 k = 0; k < Points.Num() - 1; k++)
	{
		const FVector2D P0 = Points[k], P1 = Points[k + 1];
		const float len = FVector2D::Distance(P0, P1);
		const int32 steps = FMath::CeilToInt(len / 1.5f);
		for (int32 s = 0; s <= steps; s++)
		{
			const float t = (float)s / steps;
			const FVector2D p = FMath::Lerp(P0, P1, t);
			B(Wood).AddBox(GroundPos(p.X, p.Y, 52.f), FVector(12.f, 12.f, 105.f));
		}
		// top rail
		const FVector2D mid = (P0 + P1) * 0.5f;
		const float yaw = FMath::RadiansToDegrees(FMath::Atan2(P1.Y - P0.Y, P1.X - P0.X));
		B(Wood).AddBox(GroundPos(mid.X, mid.Y, 72.f), FVector(len * 100.f, 9.f, 9.f), yaw);
	}
}

void AMillhavenWorldGen::AddStall(float AX, float AY)
{
	const FVector base = GroundPos(AX, AY, 0.f);
	B(FColor(208, 64, 32)).AddBox(base + FVector(0, 0, 190.f), FVector(280.f, 200.f, 12.f)); // canopy
	const FColor Post(138, 85, 48);
	for (int32 s = 0; s < 4; s++)
	{
		const float ox = (s < 2 ? -120.f : 120.f);
		const float oy = ((s % 2) ? -90.f : 90.f);
		B(Post).AddBox(base + FVector(ox, oy, 95.f), FVector(12.f, 12.f, 190.f));
	}
	const FColor Goods[3] = { FColor(204,48,48), FColor(232,112,32), FColor(240,208,32) };
	for (int32 g = 0; g < 3; g++)
		B(Goods[g]).AddBox(base + FVector(-60.f + g * 60.f, 0, 208.f), FVector(30.f, 30.f, 28.f));
}

void AMillhavenWorldGen::BuildStructures()
{
	// Village houses
	AddBuilding(-7,   -2,  4.4f, 3.8f, FColor(212,144,96),  FColor(139,56,40));
	AddBuilding( 6,   -3.5,5.4f, 4.4f, FColor(200,168,96),  FColor(122,51,24));
	AddBuilding(-6,    6,  4.0f, 3.4f, FColor(184,144,80),  FColor(107,46,20));
	AddBuilding( 7.5,  6,  4.8f, 4.0f, FColor(208,168,104), FColor(144,60,24));
	AddBuilding( 0,   -9,  6.0f, 4.5f, FColor(192,136,72),  FColor(160,56,32));
	AddBuilding(-12,  -5,  3.8f, 3.2f, FColor(184,120,64),  FColor(106,40,16));

	// Market stalls
	AddStall(9, 2); AddStall(11.5f, 4); AddStall(11, 0.5f);

	// Well
	{
		const FVector w = GroundPos(0, -4.5f, 0.f);
		B(FColor(128,120,104)).AddCylinder(w, 90.f, 90.f, 52.f, 8);
		B(FColor(40,112,184)).AddCylinder(w + FVector(0,0,50.f), 72.f, 72.f, 6.f, 8);
		B(FColor(122,80,40)).AddBox(w + FVector(0,-52.f,105.f), FVector(13.f,13.f,145.f));
		B(FColor(122,80,40)).AddBox(w + FVector(0, 52.f,105.f), FVector(13.f,13.f,145.f));
		B(FColor(122,80,40)).AddBox(w + FVector(0,0,179.f), FVector(13.f,126.f,13.f));
	}

	// Dock / pier at Shellwater Harbour
	{
		const FColor Plank(160,120,74), Leg(122,85,48);
		const FVector d = GroundPos(-25, 26, 0.f);
		B(Plank).AddBox(FVector(d.X, d.Y, 28.f), FVector(350.f, 1400.f, 22.f));
		for (int32 i = 0; i < 5; i++)
			B(Leg).AddBox(FVector(d.X, d.Y - 600.f + i * 300.f, -90.f), FVector(26.f, 26.f, 250.f));
		// boat hull + mast
		B(FColor(192,120,64)).AddBox(FVector(d.X + 500.f, d.Y + 400.f, 40.f), FVector(300.f, 600.f, 70.f), 18.f);
		B(FColor(154,120,80)).AddCylinder(FVector(d.X + 500.f, d.Y + 400.f, 60.f), 8.f, 8.f, 300.f, 4);
	}

	// Cave entrance (north)
	{
		const FColor Rock(96,104,88);
		const FVector c = GroundPos(-5, -36, 0.f);
		B(Rock).AddBox(c + FVector(0, -150.f, 190.f), FVector(150.f, 120.f, 380.f));
		B(Rock).AddBox(c + FVector(0,  150.f, 190.f), FVector(150.f, 120.f, 380.f));
		B(Rock).AddBox(c + FVector(0, 0, 375.f), FVector(150.f, 440.f, 150.f));
		B(FColor(6,4,10)).AddBox(c + FVector(78.f, 0, 130.f), FVector(6.f, 240.f, 260.f)); // dark mouth
		for (int32 i = 0; i < 8; i++)
		{
			const float rs = 35.f + Prng(i * 7.f) * 55.f;
			const float ox = (Prng(i * 13.f) - 0.5f) * 500.f;
			const float oy = (Prng(i * 7.f) - 0.5f) * 400.f;
			B(FColor(112,120,96)).AddBox(c + FVector(ox, oy, rs * 0.5f), FVector(rs, rs, rs),
				Prng(i * 5.f) * 90.f);
		}
	}

	// Fences
	AddFenceRun({ {2.5f,1}, {2.5f,9}, {11,9}, {11,1} });
	AddFenceRun({ {-14,-1}, {-14,9}, {-10,9} });

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
		const float z = (150.f + i * 105.f) * s;
		B(Greens[i]).AddCone(base + FVector(0, 0, z), r, h, 6 + i, i * 30.f);
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
			if (FVector2D::Distance(p, q) < 2.6f) { tooClose = true; break; }
		if (tooClose) continue;
		if (p.X < -12.f && p.Y > 20.f) continue; // water
		placed.Add(p);
		AddTree(p.X, p.Y, 0.7f + Prng(i * 3.f) * 0.6f);
	}
	// Dense Pinewood cluster (NE)
	for (int32 i = 0; i < 28; i++)
		AddTree(18.f + Prng(i * 11.f) * 25.f, -20.f - Prng(i * 7.f) * 28.f, 0.8f + Prng(i * 3.f) * 0.5f);

	// Bushes
	for (int32 i = 0; i < 40; i++)
	{
		const float a = Prng(i * 5.7f) * 2.f * PI;
		const float dist = 12.f + Prng(i * 11.f) * 35.f;
		const float bx = FMath::Cos(a) * dist, by = FMath::Sin(a) * dist;
		if (bx < -12.f && by > 18.f) continue;
		const float r = 30.f + Prng(i * 3.f) * 28.f;
		B(FColor(36,96,20)).AddBox(GroundPos(bx, by, r * 0.5f), FVector(r, r, r), Prng(i) * 45.f);
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
			B(FColor(232,240,248)).AddCone(at + FVector(0, 0, sz * 0.9f - 200.f), sz * 0.32f, sz * 0.42f, 5);
	}
}

// ---------------------------------------------------------------------------
// Water & clouds (animated in Tick)
// ---------------------------------------------------------------------------
void AMillhavenWorldGen::BuildWater()
{
	FMeshBatch W;
	const FVector c(-30 * 100.f, 32 * 100.f, 5.f);
	W.AddHQuad(c, 4800.f, 4800.f);
	TArray<FLinearColor> Cols; Cols.Init(FLinearColor(0.16f, 0.44f, 0.72f), W.V.Num());
	WaterMesh->CreateMeshSection_LinearColor(0, W.V, W.T, W.N, W.UV, Cols,
		TArray<FProcMeshTangent>(), false);
	UMaterialInstanceDynamic* MID = MakeColorMID(this, FColor(40, 112, 184));
	MIDs.Add(MID);
	WaterMesh->SetMaterial(0, MID);
}

void AMillhavenWorldGen::BuildClouds()
{
	FMeshBatch Cl;
	for (int32 i = 0; i < 14; i++)
	{
		const float cx = (Prng(i * 23.f) - 0.5f) * 16000.f;
		const float cy = (Prng(i * 31.f) - 0.5f) * 16000.f;
		const float cz = 1800.f + Prng(i * 5.f) * 1400.f;
		const int32 blobs = 3 + (int32)(Prng(i * 7.f) * 4.f);
		for (int32 j = 0; j < blobs; j++)
		{
			const float r = 250.f + Prng(i * j + 1.f) * 250.f;
			const FVector bp(cx + (Prng(i * j + 2.f) - 0.5f) * 900.f,
			                 cy + (Prng(i * j + 4.f) - 0.5f) * 600.f,
			                 cz + Prng(i * j + 3.f) * 200.f);
			Cl.AddBox(bp, FVector(r, r, r * 0.7f));
		}
	}
	TArray<FLinearColor> Cols; Cols.Init(FLinearColor(0.98f, 0.99f, 1.f), Cl.V.Num());
	CloudMesh->CreateMeshSection_LinearColor(0, Cl.V, Cl.T, Cl.N, Cl.UV, Cols,
		TArray<FProcMeshTangent>(), false);
	UMaterialInstanceDynamic* MID = MakeColorMID(this, FColor(250, 252, 255));
	MIDs.Add(MID);
	CloudMesh->SetMaterial(0, MID);
}

// ---------------------------------------------------------------------------
// NPCs + dialogue trees
// ---------------------------------------------------------------------------
void AMillhavenWorldGen::SpawnNPCs()
{
	UWorld* W = GetWorld();
	if (!W) return;

	auto Spawn = [&](float AX, float AY, const FString& Name, const FString& Role,
	                 FColor Body, FColor Skin, FColor Hair) -> AMillhavenNPC*
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Loc = GroundPos(AX, AY, 0.f);
		AMillhavenNPC* N = W->SpawnActor<AMillhavenNPC>(AMillhavenNPC::StaticClass(), Loc, FRotator::ZeroRotator, P);
		if (N) N->Init(Name, Role, Body, Skin, Hair);
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
		N->AddOption("accept", TEXT("I'll find him right away!"), "x",
			TEXT("Get the Wheat"), TEXT("Find Farmer Aldric east of the well"));
	}

	// --- Farmer Aldric ---
	if (AMillhavenNPC* N = Spawn(5.2f, 3.8f, TEXT("Farmer Aldric"), TEXT("Local Farmer"),
		FColor(80,136,40), FColor(232,192,144), FColor(96,56,24)))
	{
		N->AddNode("start", TEXT("Morning! Fine weather for crops. Name's Aldric. I've been up since before the sun. What brings you to my corner of Millhaven?"));
		N->AddOption("start", TEXT("Maren sent me for wheat."), "wheat");
		N->AddOption("start", TEXT("Tell me about your farm."), "farm");
		N->AddOption("start", TEXT("Just exploring. Goodbye!"), "x");

		N->AddNode("wheat", TEXT("Ah - for the festival! My golden wheat is ready and waiting. Head to the north field, take the tallest stalks. You can't miss them - they glow in morning light."));
		N->AddOption("wheat", TEXT("Thank you, Aldric!"), "x");

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
		N->AddOption("cave", TEXT("I'll be careful. Goodbye."), "x");
	}

	// --- Captain Wren ---
	if (AMillhavenNPC* N = Spawn(-24.f, 20.f, TEXT("Captain Wren"), TEXT("Harbour Master"),
		FColor(32,80,128), FColor(232,200,160), FColor(96,56,32)))
	{
		N->AddNode("start", TEXT("Ahoy! First time to our little harbour? The fishing's been poor this week - something spooks the catch. Where are you headed, stranger?"));
		N->AddOption("start", TEXT("Just exploring the valley."), "explore");
		N->AddOption("start", TEXT("What spooked the fish?"), "fish");
		N->AddOption("start", TEXT("Safe travels. Goodbye!"), "x");

		N->AddNode("explore", TEXT("Ha! A wanderer. Best advice: follow the coast north for the salt flats - beautiful views. South gets rocky fast. And for gods' sake avoid the cave at night."));
		N->AddOption("explore", TEXT("Why avoid the cave at night?"), "cave");
		N->AddOption("explore", TEXT("Good to know. Goodbye!"), "x");

		N->AddNode("fish", TEXT("Started three nights ago. Fish just vanish from the bay. I heard a low rumbling from under the water - my old boat's timbers shook. Something is down there."));
		N->AddOption("fish", TEXT("That's alarming. Goodbye."), "x");

		N->AddNode("cave", TEXT("The cave mouth glows at midnight. Blue light, like starfire. Elder Sylva says it's safe if you follow the moss-lights. I say let sleeping wolves lie."));
		N->AddOption("cave", TEXT("Good advice. Goodbye."), "x");
	}
}

// ---------------------------------------------------------------------------
void AMillhavenWorldGen::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TimeAccum += DeltaTime;

	if (WaterMesh)
		WaterMesh->SetRelativeLocation(FVector(0, 0, FMath::Sin(TimeAccum * 0.8f) * 4.f));

	if (CloudMesh)
	{
		FVector L = CloudMesh->GetRelativeLocation();
		L.X += DeltaTime * 55.f;
		if (L.X > 8000.f) L.X = -8000.f;
		CloudMesh->SetRelativeLocation(L);
	}
}
