#pragma once

#include "CoreMinimal.h"

/**
 * Accumulates flat-shaded low-poly geometry. Every triangle gets its own
 * three vertices with a single face normal, which is what produces the
 * faceted "low poly" look (equivalent of flatShading in Three.js).
 *
 * NOTE: deliberately NOT called FMeshBatch - the engine already declares a
 * global FMeshBatch in SceneManagement.h/MeshBatch.h, which the Engine shared
 * PCH pulls in. A second global FMeshBatch is a redefinition error.
 */
struct FMillhavenMeshBatch
{
	TArray<FVector> V;
	TArray<int32> T;
	TArray<FVector> N;
	TArray<FVector2D> UV;

	/** Pre-size the arrays for an expected triangle count (pure optimisation). */
	void ReserveTriangles(int32 TriCount)
	{
		V.Reserve(V.Num() + TriCount * 3);
		N.Reserve(N.Num() + TriCount * 3);
		UV.Reserve(UV.Num() + TriCount * 3);
		T.Reserve(T.Num() + TriCount * 3);
	}

	void AddTriangle(const FVector& A, const FVector& B, const FVector& C)
	{
		const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		const int32 Base = V.Num();
		V.Add(A); V.Add(B); V.Add(C);
		N.Add(Normal); N.Add(Normal); N.Add(Normal);
		UV.Add(FVector2D::ZeroVector); UV.Add(FVector2D::ZeroVector); UV.Add(FVector2D::ZeroVector);
		T.Add(Base); T.Add(Base + 1); T.Add(Base + 2);
	}

	// Quad wound so the front face normal is cross(B-A, C-A)
	void AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D)
	{
		AddTriangle(A, B, C);
		AddTriangle(A, C, D);
	}

	// Axis-aligned (optionally yawed) box. Size is the FULL extent.
	void AddBox(const FVector& Center, const FVector& Size, float YawDeg = 0.f)
	{
		const FRotator Rot(0.f, YawDeg, 0.f);
		auto P = [&](int32 SX, int32 SY, int32 SZ)
		{
			const FVector Local(
				Size.X * 0.5 * (SX ? 1.0 : -1.0),
				Size.Y * 0.5 * (SY ? 1.0 : -1.0),
				Size.Z * 0.5 * (SZ ? 1.0 : -1.0));
			return Center + Rot.RotateVector(Local);
		};
		ReserveTriangles(12);
		// +Z (top)
		AddQuad(P(0,0,1), P(1,0,1), P(1,1,1), P(0,1,1));
		// -Z (bottom)
		AddQuad(P(0,0,0), P(0,1,0), P(1,1,0), P(1,0,0));
		// +X
		AddQuad(P(1,0,0), P(1,1,0), P(1,1,1), P(1,0,1));
		// -X
		AddQuad(P(0,0,0), P(0,0,1), P(0,1,1), P(0,1,0));
		// +Y
		AddQuad(P(0,1,0), P(0,1,1), P(1,1,1), P(1,1,0));
		// -Y
		AddQuad(P(0,0,0), P(1,0,0), P(1,0,1), P(0,0,1));
	}

	// Cone: BaseCenter at the bottom, apex straight up. StartDeg rotates the ring.
	void AddCone(const FVector& BaseCenter, float Radius, float Height, int32 Sides, float StartDeg = 0.f)
	{
		Sides = FMath::Max(3, Sides);
		const FVector Apex = BaseCenter + FVector(0.0, 0.0, (double)Height);
		ReserveTriangles(Sides * 2);
		for (int32 i = 0; i < Sides; i++)
		{
			const float A0 = FMath::DegreesToRadians(StartDeg + (360.f * i) / Sides);
			const float A1 = FMath::DegreesToRadians(StartDeg + (360.f * (i + 1)) / Sides);
			const FVector P0 = BaseCenter + FVector(FMath::Cos(A0) * Radius, FMath::Sin(A0) * Radius, 0.f);
			const FVector P1 = BaseCenter + FVector(FMath::Cos(A1) * Radius, FMath::Sin(A1) * Radius, 0.f);
			AddTriangle(P0, P1, Apex);          // side (outward)
			AddTriangle(BaseCenter, P1, P0);    // base cap (faces down)
		}
	}

	// Cylinder / tapered cylinder. R0 = bottom radius, R1 = top radius.
	void AddCylinder(const FVector& BaseCenter, float R0, float R1, float Height, int32 Sides)
	{
		Sides = FMath::Max(3, Sides);
		const FVector TopCenter = BaseCenter + FVector(0.0, 0.0, (double)Height);
		ReserveTriangles(Sides * 4);
		for (int32 i = 0; i < Sides; i++)
		{
			const float A0 = (2.f * PI * i) / Sides;
			const float A1 = (2.f * PI * (i + 1)) / Sides;
			const FVector B0 = BaseCenter + FVector(FMath::Cos(A0) * R0, FMath::Sin(A0) * R0, 0.f);
			const FVector B1 = BaseCenter + FVector(FMath::Cos(A1) * R0, FMath::Sin(A1) * R0, 0.f);
			const FVector T0 = TopCenter + FVector(FMath::Cos(A0) * R1, FMath::Sin(A0) * R1, 0.f);
			const FVector T1 = TopCenter + FVector(FMath::Cos(A1) * R1, FMath::Sin(A1) * R1, 0.f);
			AddQuad(B0, B1, T1, T0);            // side
			AddTriangle(TopCenter, T0, T1);     // top cap (up)
			AddTriangle(BaseCenter, B1, B0);    // bottom cap (down)
		}
	}

	// Horizontal, upward-facing rectangle (paths, water).
	void AddHQuad(const FVector& Center, float SizeX, float SizeY)
	{
		const double HX = SizeX * 0.5, HY = SizeY * 0.5;
		AddQuad(
			Center + FVector(-HX, -HY, 0.0),
			Center + FVector( HX, -HY, 0.0),
			Center + FVector( HX,  HY, 0.0),
			Center + FVector(-HX,  HY, 0.0));
	}

	// Vertical rectangle facing +Y (or both ways if bTwoSided).
	void AddVQuadFacingY(const FVector& Center, float SizeX, float SizeZ, bool bTwoSided = false)
	{
		const double HX = SizeX * 0.5, HZ = SizeZ * 0.5;
		const FVector BL = Center + FVector(-HX, 0.0, -HZ);
		const FVector TL = Center + FVector(-HX, 0.0,  HZ);
		const FVector TR = Center + FVector( HX, 0.0,  HZ);
		const FVector BR = Center + FVector( HX, 0.0, -HZ);
		AddQuad(BL, TL, TR, BR); // normal +Y
		if (bTwoSided)
		{
			AddQuad(BR, TR, TL, BL); // normal -Y
		}
	}
};
