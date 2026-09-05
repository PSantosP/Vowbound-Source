// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "VBGA_Crouch.generated.h"

/**
 * 앉기 어빌리티 (Toggle 패턴)
 * 무엇: 입력 한 번으로 Crouch 토글 (누르면 앉기, 다시 누르면 일어서기)
 * 왜: GAS 기반으로 Crouch 상태를 관리하여 Sprint와 상호 블록 가능
 * 가정: bIsToggleAbility=true, InputTag=Input_Crouch
 *       두 번째 입력 시 HandleAbilityInputPressed()에서 EndAbility 호출
 * 부작용: ExecuteAbility → ACharacter::Crouch() 호출 (bIsCrouched=true)
 *         EndAbility → ACharacter::UnCrouch() 호출 (bIsCrouched=false)
 *         ActivationBlockedTags로 Sprint 중 앉기 차단
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_Crouch : public UVBGameplayAbility
{
	GENERATED_BODY()
public:
	UVBGA_Crouch();
	
private:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
