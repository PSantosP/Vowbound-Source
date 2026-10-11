// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBInteractableConfig.h"

float FVBEnvironmentHandContact::GetWeight(float MontageTime) const
{
	if (PropBone.IsNone() || !FMath::IsFinite(MontageTime) || !FMath::IsFinite(StartTime)
		|| !FMath::IsFinite(EndTime) || !FMath::IsFinite(BlendTime)
		|| EndTime <= StartTime || MontageTime <= StartTime || MontageTime >= EndTime) return 0.0f;
	const float Ramp = FMath::Min(FMath::Max(BlendTime, 0.0f), (EndTime - StartTime) * 0.5f);
	if (Ramp <= KINDA_SMALL_NUMBER) return 1.0f;
	const float Weight = FMath::Clamp(FMath::Min(MontageTime - StartTime, EndTime - MontageTime) / Ramp, 0.0f, 1.0f);
	return Weight * Weight * (3.0f - 2.0f * Weight);
}

const FVBInteractionMotion* UVBInteractableConfig::GetMotionByIndex(uint8 Index) const
{
	switch (Index)
	{
	case 0: return &ActivateMotion;
	case 1: return &DeactivateMotion;
	case 2: return &ActivateMotionBack;
	case 3: return &DeactivateMotionBack;
	case 4: return &KickOpenMotion;
	default: return nullptr;
	}
}

uint8 UVBInteractableConfig::GetMotionIndex(const FVBInteractionMotion& Motion) const
{
	for (uint8 Index = 0; Index < 5; ++Index)
	{
		if (GetMotionByIndex(Index) == &Motion) return Index;
	}
	return MAX_uint8;
}

const FVBInteractionMotion* UVBInteractableConfig::SelectMotion(bool bActivating, const FTransform& PropTransform,
                                                               const FVector& InteractorLocation, bool bSprinting,
                                                               const FVector& Velocity) const
{
	const FVBInteractionMotion& Front = bActivating ? ActivateMotion : DeactivateMotion;
	const FVBInteractionMotion& Back  = bActivating ? ActivateMotionBack : DeactivateMotionBack;

	const bool bHasFront = Front.HasCharacterMotion();
	const bool bHasBack  = Back.HasCharacterMotion();
	const bool bHasPlane = bUseSidePlane && !SidePlaneNormal.GetSafeNormal2D().IsNearlyZero();
	const FVector FrontStand = PropTransform.TransformPosition(Front.StandTransform.GetLocation());
	const FVector BackStand = PropTransform.TransformPosition(Back.StandTransform.GetLocation());
	const bool bFront = bHasPlane
		? FVector::DotProduct(PropTransform.InverseTransformPosition(InteractorLocation) - SidePlaneOrigin,
			SidePlaneNormal.GetSafeNormal2D()) >= 0.0f
		: FVector::DistSquared2D(InteractorLocation, FrontStand) <= FVector::DistSquared2D(InteractorLocation, BackStand);

	// 질주 키만 누른 정지/후진도 발차기가 되지 않게 서버 속도를 접근 방향에 투영한다.
	const FVector KickForward = (KickOpenMotion.StandTransform * PropTransform).GetUnitAxis(EAxis::X).GetSafeNormal2D();
	if (bHasPlane && bActivating && bSprinting && bFront == bKickFromFront
		&& KickOpenMotion.HasCharacterMotion() && KickOpenMotion.PropAnimation
		&& FVector::DotProduct(Velocity, KickForward) >= FMath::Max(KickMinSpeed, KINDA_SMALL_NUMBER))
	{
		return &KickOpenMotion;
	}
	if (bHasPlane)
	{
		return bFront ? (bHasFront ? &Front : nullptr) : (bHasBack ? &Back : nullptr);
	}
	if (!bHasFront && !bHasBack)
	{
		return nullptr;
	}
	if (bHasFront != bHasBack)
	{
		return bHasFront ? &Front : &Back;
	}

	return bFront ? &Front : &Back;
}
