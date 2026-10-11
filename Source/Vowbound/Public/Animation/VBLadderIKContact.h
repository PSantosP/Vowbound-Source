#pragma once

#include "CoreMinimal.h"

// 사다리 지역 공간. 표시용 가로대와 같은 간격/범위로만 접촉점을 만든다.
struct FVBLadderIKGeometry
{
	FTransform ToWorld = FTransform::Identity;
	float Height = 0.0f;
	float Width = 0.0f;
	float Spacing = 0.0f;
	float Drop = 0.0f;
	float Depth = 0.0f;
	float Radius = 0.0f;

	FVector GetPoseDepthOffset(float AuthoredDepth, float BodyDepth) const
	{
		// 클립/몸 거리는 월드 cm, 발판 깊이는 사다리 지역 cm다.
		return ToWorld.GetUnitAxis(EAxis::X) * (AuthoredDepth - BodyDepth)
			- ToWorld.TransformVector(FVector(Depth, 0.0f, 0.0f));
	}

	bool FindContact(const FVector& Raw, float Clearance, FVector& Out) const
	{
		if (Spacing <= KINDA_SMALL_NUMBER || Height <= 0.0f || Width <= 0.0f)
		{
			return false;
		}
		const int32 First = FMath::CeilToInt(Drop / Spacing);
		const int32 Last = FMath::FloorToInt((Height + Drop) / Spacing);
		if (First > Last) return false;
		const int32 Rung = FMath::Clamp(FMath::RoundToInt((Raw.Z - Clearance + Drop) / Spacing), First, Last);
		Out = FVector(-Depth, FMath::Clamp(Raw.Y, -Width * 0.5f, Width * 0.5f), Rung * Spacing - Drop + Clearance);
		return true;
	}
};

// 보정 전 포즈만 입력으로 받는다. IK 출력은 속도 판정/접촉점 선택에 되먹이지 않는다.
struct FVBLadderIKContact
{
	FVector PreviousRaw = FVector::ZeroVector;
	FVector Anchor = FVector::ZeroVector;
	FVector PlantRaw = FVector::ZeroVector;
	float Weight = 0.0f;
	float QuietTime = 0.0f;
	bool bPrimed = false;
	bool bPlanted = false;

	void Update(const FVBLadderIKGeometry& Geometry, const FVector& Raw, float Dt,
		float Clearance, float PlantSpeed, float ReleaseSpeed, float BlendTime, float PlantDwellTime = 0.06f,
		float MaxPlantDistance = MAX_flt, bool bUseAxialMotion = false, float ReleaseTravel = 0.0f)
	{
		if (Dt <= SMALL_NUMBER) return;
		if (!bPrimed)
		{
			PreviousRaw = Raw;
			bPrimed = true;
			return; // 진입 블렌드의 첫 표본을 '정지'로 오인하지 않는다.
		}
		const FVector Velocity = Geometry.ToWorld.TransformVector(Raw - PreviousRaw) / Dt;
		const FVector Axis = Geometry.ToWorld.GetUnitAxis(EAxis::Z);
		// 손목의 앞뒤 정렬/회전은 봉을 떠나 다음 봉으로 이동하는 동작과 구분한다.
		const float Speed = bUseAxialMotion ? FMath::Abs(FVector::DotProduct(Velocity, Axis)) : Velocity.Size();
		PreviousRaw = Raw;
		FVector Candidate;
		const bool bNearRung = Geometry.FindContact(Raw, Clearance, Candidate)
			&& Geometry.ToWorld.TransformVector(Candidate - Raw).Size() <= FMath::Max(MaxPlantDistance, 0.0f);
		QuietTime = Speed <= PlantSpeed && bNearRung ? QuietTime + Dt : 0.0f;
		const float AxialTravel = FMath::Abs(FVector::DotProduct(Geometry.ToWorld.TransformVector(Raw - PlantRaw), Axis));
		const bool bLeaving = Speed > ReleaseSpeed && (!bUseAxialMotion || AxialTravel > ReleaseTravel);
		if (bPlanted && (bLeaving || FMath::Abs(Raw.Z - Anchor.Z) > Geometry.Spacing * 0.65f))
		{
			bPlanted = false;
		}
		// 놓는 동안 목표를 다른 가로대로 바꾸면 페이드 중에도 손발이 한 칸 튄다.
		// 스윙 꼭짓점에서 잠깐 느려진 것과 실제 붙잡기를 구별한다.
		if (!bPlanted && Weight <= SMALL_NUMBER && bNearRung && Speed <= PlantSpeed && QuietTime >= PlantDwellTime)
		{
			Anchor = Candidate;
			PlantRaw = Raw;
			bPlanted = true;
		}
		Weight = FMath::FInterpConstantTo(Weight, bPlanted ? 1.0f : 0.0f, Dt,
			1.0f / FMath::Max(BlendTime, 0.001f));
	}
};
