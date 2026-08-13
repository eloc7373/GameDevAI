// Automation Spec tests for Millhaven's pure, engine-state-free code.
//
// These cover the two things that are cheap to test and expensive to get
// wrong: the analytic terrain function (which player spawn, prop placement,
// the water line and the biome banner all agree with), and the geometry kit
// every mesh in the game is built from.
//
// Run from the editor console:   Automation RunTests Millhaven
// or from the Session Frontend's Automation tab under "Millhaven".

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "MillhavenWorldGen.h"
#include "ProcMeshLib.h"

// The flag expression is written out at every call site rather than being
// hoisted into a constant: UE 5.5 turned EAutomationTestFlags into an enum
// class, so `int32 Flags = A | B;` no longer compiles, while passing the
// expression straight to the macro works on either side of that change.
#define MILLHAVEN_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------
// TerrainHeight
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMillhavenTerrainTest,
	"Millhaven.WorldGen.TerrainHeight", MILLHAVEN_TEST_FLAGS)

bool FMillhavenTerrainTest::RunTest(const FString& Parameters)
{
	// The village sits on a deliberately flat disc of radius 16m. Buildings,
	// paths and the player spawn all assume this.
	for (float ax = -15.f; ax <= 15.f; ax += 2.5f)
	{
		for (float ay = -15.f; ay <= 15.f; ay += 2.5f)
		{
			if (FMath::Sqrt(ax * ax + ay * ay) > 15.f)
			{
				continue;
			}
			const float H = AMillhavenWorldGen::TerrainHeight(ax * 100.f, ay * 100.f);
			TestEqual(*FString::Printf(TEXT("village ground is flat at (%.1f, %.1f)"), ax, ay),
				H, 0.f, 0.01f);
		}
	}

	// Pure function: same input, same output, no hidden state.
	const float A = AMillhavenWorldGen::TerrainHeight(-3600.f, 3000.f);
	const float B = AMillhavenWorldGen::TerrainHeight(-3600.f, 3000.f);
	TestEqual(TEXT("TerrainHeight is deterministic"), A, B, 0.0001f);

	// GroundPos must agree with the function the mesh is built from, or props
	// float and the player spawns inside the terrain.
	const FVector P = AMillhavenWorldGen::GroundPos(12.f, -7.f, 50.f);
	TestEqual(TEXT("GroundPos X converts metres to cm"), (float)P.X, 1200.f, 0.01f);
	TestEqual(TEXT("GroundPos Y converts metres to cm"), (float)P.Y, -700.f, 0.01f);
	TestEqual(TEXT("GroundPos Z is terrain height plus the lift"),
		(float)P.Z, AMillhavenWorldGen::TerrainHeight(1200.f, -700.f) + 50.f, 0.01f);

	return true;
}

// ---------------------------------------------------------------------------
// Waterline
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMillhavenWaterlineTest,
	"Millhaven.WorldGen.Waterline", MILLHAVEN_TEST_FLAGS)

bool FMillhavenWaterlineTest::RunTest(const FString& Parameters)
{
	const float FloodsAt =
		AMillhavenWorldGen::WaterlineCm - AMillhavenWorldGen::MinWaterDepthCm;

	// Regression: the old water plane was a hard-edged square that covered the
	// flat village ground, because that ground sits at exactly z=0 and the old
	// water sat at z=+5. Nothing inside the village may ever flood.
	for (float ax = -15.f; ax <= 15.f; ax += 2.5f)
	{
		for (float ay = -15.f; ay <= 15.f; ay += 2.5f)
		{
			if (FMath::Sqrt(ax * ax + ay * ay) > 15.f)
			{
				continue;
			}
			const float H = AMillhavenWorldGen::TerrainHeight(ax * 100.f, ay * 100.f);
			TestTrue(*FString::Printf(TEXT("village stays dry at (%.1f, %.1f)"), ax, ay),
				H > FloodsAt);
		}
	}

	// ...and the bay still has to hold water, or the fix went too far.
	const float BayFloor = AMillhavenWorldGen::TerrainHeight(-3600.f, 3000.f);
	TestTrue(TEXT("the coastal basin is below the waterline"), BayFloor < FloodsAt);

	// The dock deck must sit above the water it is built over.
	const FVector2D Dock = AMillhavenWorldGen::DockM();
	const float DockGround = AMillhavenWorldGen::TerrainHeight(
		(float)Dock.X * 100.f, (float)Dock.Y * 100.f);
	TestTrue(TEXT("the dock stands in water"), DockGround < AMillhavenWorldGen::WaterlineCm);
	TestTrue(TEXT("IsWaterAt agrees that the dock is over water"),
		AMillhavenWorldGen::IsWaterAt((float)Dock.X, (float)Dock.Y));

	// The boardwalk searches along this line for its landing point. If the
	// whole span were wet it would run all the way into the village square.
	bool bFoundLand = false;
	for (int32 s = 1; s <= 80; s++)
	{
		const FVector2D P = FMath::Lerp(Dock, FVector2D::ZeroVector, (double)s / 80.0);
		if (!AMillhavenWorldGen::IsWaterAt((float)P.X, (float)P.Y))
		{
			bFoundLand = true;
			TestTrue(TEXT("the pier reaches land without crossing the village"),
				P.Size() > 16.0);
			break;
		}
	}
	TestTrue(TEXT("there is dry land between the pier and the village"), bFoundLand);

	return true;
}

// ---------------------------------------------------------------------------
// BiomeAt
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMillhavenBiomeTest,
	"Millhaven.WorldGen.BiomeAt", MILLHAVEN_TEST_FLAGS)

bool FMillhavenBiomeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("origin is the village"),
		AMillhavenWorldGen::BiomeAt(0.f, 0.f), FString(TEXT("Millhaven Village")));
	TestEqual(TEXT("south-west coast is the harbour"),
		AMillhavenWorldGen::BiomeAt(-2500.f, 2600.f), FString(TEXT("Shellwater Harbour")));
	TestEqual(TEXT("far north is the hills"),
		AMillhavenWorldGen::BiomeAt(0.f, -3000.f), FString(TEXT("Northern Hills")));
	TestEqual(TEXT("north-east is the forest"),
		AMillhavenWorldGen::BiomeAt(2500.f, -1500.f), FString(TEXT("Pinewood Forest")));

	// Every landmark the quest system sends the player to needs a name for the
	// location banner - an empty one means BiomeAt has a hole in it.
	const FVector2D Cave = AMillhavenWorldGen::CaveMouthM();
	TestTrue(TEXT("the cave mouth is inside a named region"),
		!AMillhavenWorldGen::BiomeAt((float)Cave.X * 100.f, (float)Cave.Y * 100.f).IsEmpty());

	return true;
}

// ---------------------------------------------------------------------------
// Geometry kit
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMillhavenMeshBatchTest,
	"Millhaven.Geometry.MeshBatch", MILLHAVEN_TEST_FLAGS)

bool FMillhavenMeshBatchTest::RunTest(const FString& Parameters)
{
	// Flat shading means every triangle owns its three vertices outright.
	{
		FMillhavenMeshBatch Box;
		Box.AddBox(FVector::ZeroVector, FVector(100.0, 200.0, 300.0));
		TestEqual(TEXT("a box is 12 triangles"), Box.T.Num(), 36);
		TestEqual(TEXT("no vertex is shared between triangles"), Box.V.Num(), 36);
		TestEqual(TEXT("one normal per vertex"), Box.N.Num(), 36);
		TestEqual(TEXT("one UV per vertex"), Box.UV.Num(), 36);

		// Size is the FULL extent, not a half-extent. Getting this backwards
		// doubles every prop in the world.
		FVector Min = Box.V[0];
		FVector Max = Box.V[0];
		for (const FVector& V : Box.V)
		{
			Min = Min.ComponentMin(V);
			Max = Max.ComponentMax(V);
		}
		TestEqual(TEXT("box spans its full X extent"), (float)(Max.X - Min.X), 100.f, 0.01f);
		TestEqual(TEXT("box spans its full Y extent"), (float)(Max.Y - Min.Y), 200.f, 0.01f);
		TestEqual(TEXT("box spans its full Z extent"), (float)(Max.Z - Min.Z), 300.f, 0.01f);

		for (const FVector& N : Box.N)
		{
			TestEqual(TEXT("normals are unit length"), (float)N.Size(), 1.f, 0.001f);
		}
	}

	// A horizontal quad has to face up, or the ground renders as a hole.
	{
		FMillhavenMeshBatch Flat;
		Flat.AddHQuad(FVector::ZeroVector, 100.f, 100.f);
		TestEqual(TEXT("a quad is two triangles"), Flat.T.Num(), 6);
		for (const FVector& N : Flat.N)
		{
			TestTrue(TEXT("AddHQuad faces +Z"), N.Z > 0.9);
		}
	}

	// Cone: one side triangle and one base triangle per segment.
	{
		FMillhavenMeshBatch Cone;
		Cone.AddCone(FVector::ZeroVector, 50.f, 120.f, 6);
		TestEqual(TEXT("a 6-sided cone is 12 triangles"), Cone.T.Num(), 36);
	}

	// Cylinder: a side quad plus two cap triangles per segment.
	{
		FMillhavenMeshBatch Cyl;
		Cyl.AddCylinder(FVector::ZeroVector, 30.f, 20.f, 90.f, 8);
		TestEqual(TEXT("an 8-sided cylinder is 32 triangles"), Cyl.T.Num(), 96);
	}

	// Degenerate side counts are clamped rather than producing a broken mesh.
	{
		FMillhavenMeshBatch Degenerate;
		Degenerate.AddCone(FVector::ZeroVector, 10.f, 10.f, 1);
		TestTrue(TEXT("a cone clamps to at least 3 sides"), Degenerate.T.Num() >= 18);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Village table
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMillhavenVillageTest,
	"Millhaven.WorldGen.VillageBuildings", MILLHAVEN_TEST_FLAGS)

bool FMillhavenVillageTest::RunTest(const FString& Parameters)
{
	const TArray<FMillhavenBuildingDef>& Defs = AMillhavenWorldGen::VillageBuildings();
	TestTrue(TEXT("the village has houses"), Defs.Num() > 0);

	for (const FMillhavenBuildingDef& D : Defs)
	{
		TestTrue(TEXT("house footprints are positive"), D.W > 0.f && D.D > 0.f);

		// Houses stand on the flat disc, so their ground must be flat too.
		const float H = AMillhavenWorldGen::TerrainHeight(
			(float)D.At.X * 100.f, (float)D.At.Y * 100.f);
		TestEqual(TEXT("houses sit on level ground"), H, 0.f, 0.01f);
	}

	// No two houses may occupy the same spot - a duplicated row in the table
	// would silently stack two buildings and one minimap blip.
	for (int32 i = 0; i < Defs.Num(); i++)
	{
		for (int32 j = i + 1; j < Defs.Num(); j++)
		{
			TestTrue(TEXT("houses do not overlap"),
				FVector2D::Distance(Defs[i].At, Defs[j].At) > 1.0);
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// PRNG
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMillhavenPrngTest,
	"Millhaven.WorldGen.Prng", MILLHAVEN_TEST_FLAGS)

bool FMillhavenPrngTest::RunTest(const FString& Parameters)
{
	// Regression: BuildClouds used to seed with i*j, which collapses to the
	// same value whenever either index is zero, stacking cloud clusters in one
	// spot. Seeds are now mixed with distinct primes - check that the first
	// row and column really are all different.
	// A plain array rather than a TSet: hashing floats would lean on a
	// GetTypeHash overload this test has no reason to depend on, and 36
	// elements make the quadratic scan free.
	TArray<float> Seen;
	int32 Collisions = 0;
	for (int32 i = 0; i < 6; i++)
	{
		for (int32 j = 0; j < 6; j++)
		{
			const float Seed = i * 37.f + j * 11.f;
			const float Value = AMillhavenWorldGen::Prng(Seed);

			TestTrue(TEXT("Prng stays in [0,1)"), Value >= 0.f && Value < 1.f);

			for (const float Previous : Seen)
			{
				if (FMath::IsNearlyEqual(Previous, Value, 1.e-6f))
				{
					Collisions++;
					break;
				}
			}
			Seen.Add(Value);
		}
	}
	TestEqual(TEXT("mixed seeds do not collapse onto each other"), Collisions, 0);

	return true;
}

#undef MILLHAVEN_TEST_FLAGS

#endif // WITH_AUTOMATION_TESTS
