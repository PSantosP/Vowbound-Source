// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_PlayerRagdoll.generated.h"

/** Server-selected GASP recovery montage. The component owns physical ragdoll entry. */
UCLASS()
class VOWBOUND_API UVBGA_PlayerRagdoll : public UVBGameplayAbility
{
	GENERATED_BODY()
public:
	UVBGA_PlayerRagdoll();

protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION() void OnMontageEnded();
};
