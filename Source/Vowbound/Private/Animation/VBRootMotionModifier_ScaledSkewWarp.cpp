// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBRootMotionModifier_ScaledSkewWarp.h"

#include "Components/SkeletalMeshComponent.h"
#include "MotionWarpingAdapter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VBRootMotionModifier_ScaledSkewWarp)

UVBRootMotionModifier_ScaledSkewWarp::UVBRootMotionModifier_ScaledSkewWarp(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FTransform UVBRootMotionModifier_ScaledSkewWarp::ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds)
{
	const UMotionWarpingBaseAdapter* OwnerAdapter = GetOwnerAdapter();
	const USkeletalMeshComponent*    Mesh         = OwnerAdapter ? OwnerAdapter->GetMesh() : nullptr;
	// 균일 배율을 가정하고 X 를 쓴다 - 몸 배율의 홈(AVBEnemyBase::GetStrideScale)과 같은 축이다.
	//   0 이나 음수는 보정할 수 없는 상태라 부모에게 그대로 넘긴다(보정 없는 엔진 거동 = 종전과 같다).
	const double MeshScale = Mesh ? Mesh->GetComponentScale().X : 1.0;
	if (MeshScale <= UE_KINDA_SMALL_NUMBER || FMath::IsNearlyEqual(MeshScale, 1.0, 1.0e-3))
	{
		return Super::ProcessRootMotion(InRootMotion, DeltaSeconds);
	}

	// 부모가 «지금 위치» 로 쓰는 점과 같은 점에서 당겨야 부모 안의 거리가 정확히 1/배율 이 된다.
	const FVector Current = OwnerAdapter->GetVisualRootLocation();
	const double  InvScale = 1.0 / MeshScale;

	const FTransform SavedTarget = CachedTargetTransform;
	const FTransform SavedStart  = StartTransform;
	CachedTargetTransform.SetLocation(Current + (SavedTarget.GetLocation() - Current) * InvScale);
	// 시작 위치는 애니에 이동이 없을 때만 부모가 쓴다(창 시작~목표를 보간하는 기준점). 같은 식으로 당겨야 보간 결과도 1/배율 이 된다.
	StartTransform.SetLocation(Current + (SavedStart.GetLocation() - Current) * InvScale);

	const FTransform Result = Super::ProcessRootMotion(InRootMotion, DeltaSeconds);

	// 되돌린다 - 다음 프레임의 Update 가 목표 변화 여부를 이 값과 비교한다(URootMotionModifier_Warp::Update).
	CachedTargetTransform = SavedTarget;
	StartTransform        = SavedStart;
	return Result;
}
