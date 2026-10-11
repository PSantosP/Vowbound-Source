// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AbilitySystem/VBHitImpact.h"
#include "VBDownedImpactReceiver.generated.h"

/**
 * «물리로 누운 몸이 타격을 받는다» 계약. 설계 .sw/downed-hit/design.md 3절.
 *
 * 왜 인터페이스인가: 피해 허브(UVBHealthAttributeSet::PostGameplayEffectExecute)가 이동(넉백)을 정한다. 누운 몸이면 넉백 대신
 *   이 충격으로 돌린다 - 허브가 적 클래스를 include·Cast 하지 않게(IVBGuardable 과 같은 판단, FIND-057a).
 * 판단은 받는 쪽이 한다: 지금 누운 단계인가, 상한이 지났나, 등급별로 얼마나 미나. 허브는 방향·등급·몸집 배수만 넘긴다 -
 *   서 있을 때의 넉백과 같은 값(밀린 방향 PushDir, 실효 등급, KnockbackTakenScale)이라 두 경로가 한 축을 쓴다.
 * 둘 다 서버에서만 부른다(허브가 권위 머신 전용이다).
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UVBDownedImpactReceiver : public UInterface
{
	GENERATED_BODY()
};

class IVBDownedImpactReceiver
{
	GENERATED_BODY()

public:
	// 지금 몸이 물리로 누워 있어 이 타격의 이동을 몸이 받는가. 참이면 허브는 넉백을 걸지 않고 ReceiveDownedImpact 를 부른다.
	virtual bool IsBodyDownedInPhysics() const = 0;

	// 맞은 자리(Hit - 판정이 준 뼈·접점, 없으면 nullptr)에 밀린 방향으로 충격을 준다.
	// 반환 = 실제로 준 속도 충격의 크기(cm/s). 상한이 지났거나 그 등급이 0 이면 0 - 허브 로그가 이 값을 찍는다.
	virtual float ReceiveDownedImpact(const FHitResult* Hit, const FVector& PushDir, EVBHitImpact Impact, float KnockbackScale) = 0;
};
