// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_Stagger.generated.h"

class UVBHitReactConfig;

/**
 * UVBGA_Stagger
 *
 * 경직(Poise break) 어빌리티 (D8). 피격자 본인의 ASC 에서 활성화되어 경직 몽타주를 재생하고,
 * 그 동안 신규 공격을 막는다.
 *
 * 활성화 경로: UVBCombatAttributeSet::PostGameplayEffectExecute 가 Poise 를 0 으로 만들면
 *              Event.Combat.PoiseBroken 을 HandleGameplayEvent 로 송신 → 아래 AbilityTriggers 가 수신.
 *              (UVBGA_HitReact 가 Event.Combat.Hit 로 활성화되는 것과 정확히 같은 구조)
 *
 * 왜 히트리액트와 같은 클래스로 합치지 않는가: 트리거(Hit vs PoiseBroken), 게이트(뚝심 vs 회피/가드),
 *   강도(플린치 vs 큰 경직)가 전부 다르다. 같은 클래스로 합치면 그 셋을 런타임 분기로 갈라야 하고,
 *   분기 조건이 곧 두 번째 홈이 된다.
 *
 * 토큰 반납 코드가 없는 것이 요점이다: 경직이 공격 GA 를 끝내면 State.Combat.Attacking 이 사라지고
 *   UVBEnemyCombatComponent 가 그 하강 에지에서 반납한다. FR8 이 코드 0줄로 성립한다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_Stagger : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_Stagger();

protected:
	// 경직 데이터(피격자 소유). BP_GA_Stagger 클래스 디폴트에서 DA_HitReact_Default 를 할당한다.
	// 왜 히트리액트와 같은 자산인가: 둘 다 "맞은 쪽의 반응 콘텐츠"라는 같은 관심사다.
	//   자산을 가르면 적 BP 가 반응 자산을 두 개 꽂아야 해서 한쪽만 빠지는 사고가 생긴다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBHitReactConfig> HitReactConfig;

	// 베이스가 ActivateAbility 를 final 로 잠그고 CommitAbility 를 강제하므로 여기를 오버라이드한다.
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
	                            const FGameplayAbilityActorInfo*     ActorInfo,
	                            const FGameplayAbilityActivationInfo ActivationInfo,
	                            const FGameplayEventData*            TriggerEventData) override;

private:
	// 몽타주 종료 4경로(완료/블렌드아웃/중단/취소) 공통 종료 — 경직은 경로별 분기가 필요 없다.
	UFUNCTION()
	void OnStaggerEnded();
};
