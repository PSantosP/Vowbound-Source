// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_GetUp.generated.h"

/**
 * 처형을 맞고도 죽지 않은 대상이 일어난다.
 *
 * 발동: `Event.Combat.FinisherSurvived` 로 트리거된다(AbilityTriggers). 쏘는 쪽은 UVBGA_Execute 다.
 *   스태거(UVBGA_Stagger)와 같은 배선이라 새 구독 코드가 없다 - ASC 가 GiveAbility 때 등록한다.
 *
 * 물리 래그돌은 OptionalObject의 VBRagdollConfig에서 native Chooser를 가져온다.
 *   EnemyBase가 저장한 물리 포즈로 검색하고 결과의 MontageStartTime부터 재생한다.
 *   비물리 기존 처형의 고정 몽타주는 현재 쓰러진 방향과 클립 시작 방향을 맞춘 뒤 재생한다.
 *   이 회전 정합은 GASP Chooser 경로에는 적용하지 않는다.
 *
 * 몽타주가 없으면(설정 자산이 비었으면) 즉시 끝낸다 - 연출 없는 처형도 성립해야 한다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_GetUp : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_GetUp();

protected:
	// 베이스가 ActivateAbility 를 final 로 잠그고 CommitAbility 를 강제하므로 여기를 오버라이드한다.
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
	                            const FGameplayAbilityActorInfo*     ActorInfo,
	                            const FGameplayAbilityActivationInfo ActivationInfo,
	                            const FGameplayEventData*            TriggerEventData) override;

private:
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 완료/중단/재생 실패는 종료한다. 블렌드아웃 시작만으로 기상 상태를 해제하지 않는다.
	// 외부 취소를 포함한 물리 상태 정리는 EndAbility에서 수행한다.
	UFUNCTION()
	void OnGetUpEnded();
};
