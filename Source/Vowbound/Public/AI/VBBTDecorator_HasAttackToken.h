// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AI/VBBTDecorator_LiveCondition.h"
#include "VBBTDecorator_HasAttackToken.generated.h"

/**
 * 이 적이 지금 어택 토큰을 쥐고 있는가 (프로젝트 최초의 커스텀 BTDecorator).
 *
 * 무엇: 블랙보드를 거치지 않고 UVBEnemyCombatComponent 에 직접 묻는다.
 *
 * 왜 블랙보드가 아닌가: 토큰 보유는 공격 종료와 함께 그 프레임에 바뀌는데, 블랙보드는 BT 서비스가
 *   주기적으로 쓰는 캐시다(0.2초). 공격이 끝나면 토큰은 즉시 반납되지만 BT 는 같은 프레임에 트리를
 *   재탐색하고, 그때 블랙보드는 아직 Attacker 라 같은 적이 토큰 없이 다시 때린다.
 *   2026-08-20 PIE 로그에서 이 경합이 5회 관측됐다. 주기를 줄여도 경합은 남는다 -
 *   게이트가 활성화 시점에 원본을 물어야 사라진다.
 *
 * 왜 GA 의 CanActivateAbility 가 아닌가: 순환한다. 코디네이터의 후보 수집이 IsReadyToAttack ->
 *   CanActivateAbility 를 쓰는데, 토큰이 그 조건에 들어가면 토큰 없는 적은 영원히 후보가 되지 못한다.
 *   우회하려면 IsReadyToAttack 을 쿨다운 검사만으로 좁혀야 하고, 그러면 사망/경직 차단태그 검사가
 *   빠져 경직된 적이 토큰 후보가 된다.
 *
 * 왜 스스로 중단을 거는가: 관찰이 없으면 걷는 도중 토큰을 회수당해도 트리가 그것을 모르고
 *   끝까지 걸어가 때린다 - 2026-08-20 에 고쳤던 '토큰 없이 공격' 이 그대로 되돌아온다.
 *   관찰 기계는 UVBBTDecorator_LiveCondition 이 소유하고 여기는 질문만 남긴다.
 */
UCLASS()
class VOWBOUND_API UVBBTDecorator_HasAttackToken : public UVBBTDecorator_LiveCondition
{
	GENERATED_BODY()

public:
	UVBBTDecorator_HasAttackToken();

protected:
	virtual bool    CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;
};
