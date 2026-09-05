// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "VBGA_Sprint.generated.h"

/**
 * 스프린트 어빌리티 (Held 패턴)
 * 무엇: 입력을 누르고 있는 동안 MaxWalkSpeed 증가 + 이동 방향 자동 회전
 * 왜: GAS 기반으로 Sprint 상태를 관리하여 태그/블록 시스템과 통합
 * 가정: InputTag=Input_Sprint, NetExecutionPolicy=ServerOnly(기본값 상속)
 *       HandleAbilityInputReleased()에서 EndAbility 호출 (Hold 해제 시 종료)
 * 부작용: StartSprint() → CMC MaxWalkSpeed/회전 모드 변경
 *         EndAbility → StopSprint() → 원래 속도/회전 모드 복원
 *         ActivationBlockedTags로 Crouch/Attack 중 스프린트 차단
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_Sprint : public UVBGameplayAbility
{
	GENERATED_BODY()
	
public:
	UVBGA_Sprint();
	
private:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
