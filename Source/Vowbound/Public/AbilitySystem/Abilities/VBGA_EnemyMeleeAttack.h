// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"
#include "VBGA_EnemyMeleeAttack.generated.h"

/**
 * UVBGA_EnemyMeleeAttack
 *
 * 적 전용 근접 공격. 베이스와의 차이는 단 하나 - 스윙 진입을 TelegraphDuration 만큼 미루는 예고 단계다(D3).
 *
 * 왜 자산 교체가 아니라 클래스인가: 예고는 데이터가 아니라 코드다. 베이스의 ExecuteAbility 는 진입 즉시
 *   공격 몽타주를 재생하므로, "그 진입을 늦춘다"는 어떤 자산 값으로도 표현되지 않는다.
 *   (적 전용 공격 자산 분리는 별개의 사안이고 이 클래스가 없어도 성립한다 - 적은 AVBCharacter 캐스트에
 *    실패해 이미 레거시 폴백 경로를 타므로, 자산만 갈아 끼우면 플레이어 자산과의 결합은 풀린다.)
 *
 * 왜 BT 태스크가 아니라 GA 안인가: 예고와 스윙이 두 노드로 갈리면 그 사이에 State.Combat.Attacking 이
 *   잠깐 비는 이음매가 생겨 토큰 보유 구간이 GA 수명과 어긋나고, 경직이 예고 중에 들어올 때 취소 지점이 둘이 된다.
 *   GA 안에 넣으면 토큰 보유 구간 = GA 수명이라 이음매가 없고 취소 지점이 하나다.
 *
 * 쿨다운은 예고 시작 시점부터 돈다: CommitAbility 를 베이스 UVBGameplayAbility::ActivateAbility 가
 *   ExecuteAbility 앞에서 이미 통과시키기 때문이다. 예고까지가 한 번의 공격 행동이므로 옳다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_EnemyMeleeAttack : public UVBGA_MeleeAttackBase
{
	GENERATED_BODY()

public:
	UVBGA_EnemyMeleeAttack();

protected:
	// Super 를 즉시 부르지 않는다. 이 클래스의 일은 "그 진입을 예고 시간만큼 미루는 것" 하나다.
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
	                            const FGameplayAbilityActorInfo*     ActorInfo,
	                            const FGameplayAbilityActivationInfo ActivationInfo,
	                            const FGameplayEventData*            TriggerEventData) override;

	// 예고 태그 제거. 취소(경직)로 들어와도 여기로 수렴한다.
	// 예고 지연은 어빌리티 태스크라 엔진이 함께 끝낸다 - 여기서 지울 타이머가 없다.
	virtual void EndAbility(const FGameplayAbilitySpecHandle     Handle,
	                        const FGameplayAbilityActorInfo*     ActorInfo,
	                        const FGameplayAbilityActivationInfo ActivationInfo,
	                        bool                                 bReplicateEndAbility,
	                        bool                                 bWasCancelled) override;

private:
	// 예고가 끝난 뒤 실제 스윙 진입. 베이스의 ExecuteAbility 를 현재 컨텍스트로 부른다.
	// UFUNCTION 인 이유: FWaitDelayDelegate 가 DYNAMIC 이라 AddDynamic 대상은 UFUNCTION 이어야 한다.
	UFUNCTION()
	void BeginSwing();

	// 경직 태그가 붙는 즉시 이 공격을 끝낸다.
	// 왜 필요한가: UVBGA_Stagger 는 BlockAbilitiesWithTag 만 쓴다 - 신규 활성화만 막고 진행 중인 GA 는
	//   끊지 않는다. 그래서 예고 구간에는 취소 지점이 하나도 없었고, 적이 경직 애니메이션을 뚫고 나와
	//   정상 타격했다(2026-08-20 감사 C2).
	UFUNCTION()
	void OnStaggeredDuringAttack();

	// 예고 로스태그를 이 활성화가 부여했는지. 부여한 경우에만 제거한다(누수/중복제거 방지).
	// bGrantedSuperArmor(VBGA_MeleeAttackBase)와 같은 이유의 래치다. 게임스레드 전용.
	bool bGrantedTelegraph = false;
};
