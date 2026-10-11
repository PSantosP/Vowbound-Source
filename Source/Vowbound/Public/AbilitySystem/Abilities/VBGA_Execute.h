// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGA_MedicalAbilityBase.h"
#include "VBGA_Execute.generated.h"

/**
 * 처치 어빌리티
 * 처형 창이 열린 적에게만 사용 가능 (판정 = 대상의 State.Combat.FinisherWindow 태그. 2026-09-11 전환)
 *   창은 UVBGA_FinisherWindow 가 열고 닫는다. 여기서 체력을 다시 계산하지 않는다.
 * 대상이 없으면 활성화 자체가 안 된다 - 베이스의 CanActivateAbility 가 CommitAbility 앞에서 막으므로
 *   헛발에 쿨다운이 걸리지 않는다.
 * 즉사 데미지 -> 명성 감소
 *
 * 두 갈래로 끝난다 (2026-09-11, DEC-019):
 *  - 현재 무기에 처형 연출이 있으면 장면을 걸고, 장면이 끝날 때 피해를 준다.
 *  - 없으면 부모의 즉시 처치 그대로다. 자산이 없는 무기에서 기능이 사라지지 않는다.
 *
 * 왜 피해를 장면 끝에 주나: 시작할 때 주면 대상이 첫 프레임에 죽어 처형 애니 중간에 쓰러진다.
 *   정확히는 타격 노티파이 시점이 맞지만 그것은 다음 단계다 - 지금은 «끝» 이 «시작» 보다 낫다.
 * 전제: 아바타에 UVBFinisherComponent 가 있어야 연출 갈래가 성립한다. 없으면 조용히 즉시 갈래로 간다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_Execute : public UVBGA_MedicalAbilityBase
{
	GENERATED_BODY()

public:
	UVBGA_Execute();

protected:
	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	// 처형 장면이 끝났다. 피해를 주고 어빌리티를 끝낸다.
	UFUNCTION()
	void HandleFinisherEnded();

	// 피해자 native notify 시점에 권위측 피해를 한 번 확정한다.
	// 생존한 불사 더미에는 래그돌 예약을 남기지 않는다.
	UFUNCTION()
	void HandleInteractionVictimLanded(FVector LaunchVelocity);

	// 진행 중인 내 공격을 끊는다. 두 경로가 장면을 시작하기 «전» 에 각각 부른다 - 왜 필요한지는 본문 주석.
	void CancelOwnAttacks();

	// 이번 처형의 피해를 이미 줬는가. 상호작용 갈래는 피해자 착지에서 주므로, 뒤이어 오는
	//   HandleFinisherEnded 가 또 주면 안 된다(두 번 주면 명성도 두 번 깎인다).
	bool bFinisherDamageApplied = false;
	bool bFinisherVictimEnteredRecoverableRagdoll = false;

	// 연출 갈래로 갔는가. 종료 경로가 구독을 풀어야 하는지 이것으로 가른다.
	bool bFinisherActive = false;

	// 그 연출이 «새 경로» 였는가(GASP 5.8 상호작용). 끝 처리 하나가 이것으로 갈린다.
	//
	// 무엇이 갈리나: 캡슐을 몸 자리로 데려오는 보정을 새 경로에서는 «하지 않는다».
	//   그 보정은 옛 경로의 피해자 클립이 루트모션 없이 루트만 움직여서 생긴 빚을 갚는 것인데,
	//   새 경로는 모션워핑이 장면 내내 캡슐째로 맞춰 오므로 갚을 것이 없다. 그래도 하면 두 번 민다.
	bool bFinisherUsedInteraction = false;

	// 장면을 «걸 때» 잡은 대상. 끝날 때 피해를 이 액터에 준다.
	// 왜 다시 안 찾나: 장면이 3.3초고 그 사이 락온이 바뀌거나 콘 안의 다른 적이 더 높은 점수를 받을 수 있다.
	//   끝에서 다시 찾으면 처형 애니를 다 맞은 적이 살고 옆의 적이 죽는다(2026-09-11 /verify 적발).
	// 약참조인 이유: 장면 도중 대상이 파괴될 수 있다. 그때는 피해 없이 종료하는 것이 맞다.
	TWeakObjectPtr<AActor> FinisherTarget;

	// 이번 처형의 설정. 장면이 끝날 때 「버텼으면 무엇으로 일어나나」를 여기서 꺼낸다.
	//   컴포넌트의 ActiveConfig 를 다시 읽지 않는 이유: 그 값은 장면이 끝나며 비워지므로
	//   끝 처리에서 읽으면 이미 없다. 시작할 때 잡은 것을 쓴다(FinisherTarget 과 같은 규약).
	TWeakObjectPtr<const class UVBFinisherConfig> FinisherConfig;
};
