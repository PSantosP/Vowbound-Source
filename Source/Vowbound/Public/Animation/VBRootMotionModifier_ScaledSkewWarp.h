// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RootMotionModifier_SkewWarp.h"
#include "VBRootMotionModifier_ScaledSkewWarp.generated.h"

/**
 * 메시 배율을 아는 Skew Warp. design .sw/boss/design.md §17.
 *
 * 무엇이 틀리나(엔진 5.8): SkewWarp 는 창 안 루트모션을 «애니 단위(배율 1)» 로 뽑고, 목표까지의 거리는 «월드 단위» 로 재서
 *   둘의 비로 걸음을 늘린다(RootMotionModifier_SkewWarp.cpp::URootMotionModifier_SkewWarp::ProcessRootMotion 의
 *   InverseTransformPositionNoScale). 그 결과를 CMC 가 메시 트랜스폼으로 월드에 옮기면서 배율을 한 번 더 곱한다
 *   (SkeletalMeshComponent.cpp::USkeletalMeshComponent::ConvertLocalRootMotionToWorld). 도착점은 맞지만 속도 모양이
 *   «남은 거리 ∝ 남은 애니 이동^배율» 로 휜다 - 배율 1.6 이면 창 초반 몸이 발보다 1.6배 빨리 미끄러지고 끝에서는 발만 걷는다.
 *
 * 고침: 부모를 부르는 동안만 목표·시작 위치를 «지금 몸 뿌리» 쪽으로 1/배율 당긴다. 그러면 부모 안의 거리가 전부 애니 단위가
 *   되고(창 뒤에 남는 루트모션도 애니 단위라 같이 맞는다), 결과에 배율이 곱해져 정확히 월드가 된다. 같은 점에서 당기므로
 *   Facing 회전의 방향은 안 바뀐다. 엔진 수식을 베끼지 않고 부모를 부른다 - 엔진이 바뀌어도 따라간다.
 * 배율 1 이면 부모 그대로다(플레이어·병사처럼 배율 1 인 몸은 결과가 같다).
 *
 * 쓰는 곳: 몽타주 MotionWarping 노티의 RootMotionModifier 틀. 배율이 1 이 아닌 몸이 트는 몽타주는 이것이어야 한다 -
 *   아니면 적 공격 GA 가 경고한다(UVBGA_EnemyMeleeAttack::SetWarpTargetToCombatTarget).
 */
UCLASS(meta = (DisplayName = "Skew Warp (메시 배율 보정)"))
class VOWBOUND_API UVBRootMotionModifier_ScaledSkewWarp : public URootMotionModifier_SkewWarp
{
	GENERATED_BODY()

public:
	UVBRootMotionModifier_ScaledSkewWarp(const FObjectInitializer& ObjectInitializer);

	virtual FTransform ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds) override;
};
