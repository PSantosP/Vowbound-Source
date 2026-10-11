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

	// 규칙 A(design .sw/enemy-soldier/design.md §2): 예고 중에 맞으면(히트리액트가 켜지면) 이 공격을 끝낸다.
	//   스윙 중 피격은 종전대로 히트리액트 몽타주가 공격 몽타주를 멈춰 끝난다 - 이 감시는 예고 구간을 메운다.
	//   슈퍼아머 공격은 자동으로 예외다: SuperArmor 가 히트리액트 활성화를 막아 이 태그가 아예 안 붙는다.
	UFUNCTION()
	void OnHitDuringAttack();

	// 예고 상태 해제 - 로스태그와 예고 큐(GameplayCue.Combat.Telegraph)를 함께 뗀다. 정상 진행(BeginSwing)과
	//   취소(EndAbility) 둘 다 여기로 수렴한다. 두 곳에 같은 블록이 있던 것을 2026-09-06 에 하나로 모았다.
	void ReleaseTelegraph();

	// 전투 컴포넌트의 타겟으로 접근 워프를 세운다(design .sw/boss/design.md §12). 베이스의 SetWarpTargetLockedEnemy 는
	//   플레이어의 락온·소프트 콘 경로라 적에서는 아무것도 안 세운다 - 적의 «누구를 때리나» 는 블랙보드가 이미 정했으므로 그것을 쓴다.
	//   워프 창은 빠른 클립도 0.9초 뒤(트롤 05)라 몽타주 시작 직후 세워도 늦지 않고, bFollowComponent 라 움직이는 대상을 매 프레임 따른다.
	void SetWarpTargetToCombatTarget();

	// 거리에 맞는 변형(design .sw/boss/design.md §12.8). 변형마다 «가야 하는 거리 = 지금 거리 - 그 변형의 멈춤» 을 따로 재고,
	//   워프 허용비 안에 양쪽으로 드는 것 중 «전진이 가장 긴 것» 을 고른다 - 닿으면서 안 미끄러지는 것 중 큰 공격. 하나도 없으면
	//   덜 틀린 것(|ln(전진/필요)| 최소)이다. 워프는 그 뒤 잔차만 보정하므로 다리와 몸이 어긋나는 압축이 줄어든다.
	//   규칙이 두 번 바뀌었다 - §12.4 의 «|전진 - 필요| 최소» 는 대역 밖 클립을 골랐고(압축 2.63), §12.8 초판은 위쪽 경계가 없어
	//   제자리 클립이 4m 밖에서 뽑혔다. 타겟이 없으면 베이스의 순환 커서.
protected:
	virtual int32 SelectVariantIndex(const UVBAttackConfig& Leaf, int32 VariantCount) override;

private:

	// 예고 로스태그와 큐를 이 활성화가 부여했는지. 부여한 경우에만 제거한다(누수/중복제거 방지).
	// bGrantedSuperArmor(VBGA_MeleeAttackBase)와 같은 이유의 래치다. 게임스레드 전용.
	bool bGrantedTelegraph = false;
};
