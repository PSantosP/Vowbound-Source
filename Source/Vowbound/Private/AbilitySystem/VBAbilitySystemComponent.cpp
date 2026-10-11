// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimMontage.h"


UVBAbilitySystemComponent::UVBAbilitySystemComponent()
{
	// Mixed 모드 : GE는 서버->오너만, Cue/Tag는 모든 클라이언트
	// 생성자(CDO 생성 시점)에선 SetIsReplicated 가 ensure 발화 — ByDefault 가 공식 경로 (ActorComponent.cpp::UActorComponent::SetIsReplicated)
	SetIsReplicatedByDefault(true);
	UAbilitySystemComponent::SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

void UVBAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);

	// 초기화 완료
	bAbilityActorInfoSet = true;
}

bool UVBAbilitySystemComponent::AcquireExternalMontageOwnership(UObject* InOwner, uint8 PreviousPlayInstanceId)
{
	if (!IsValid(InOwner) || (ExternalMontageOwner.IsValid() && ExternalMontageOwner.Get() != InOwner))
	{
		return false;
	}
	ExternalMontageOwner = InOwner;
	SupersededPlayInstanceId = PreviousPlayInstanceId;
	bHasSupersededMontage = true;
	// Stop only the montage tracked by GAS, before the external producer starts its pair.
	// On authority this also publishes IsStopped; callbacks cannot start a competing GAS montage.
	CurrentMontageStop();
	return true;
}

void UVBAbilitySystemComponent::ReleaseExternalMontageOwnership(UObject* InOwner)
{
	if (ExternalMontageOwner.Get() != InOwner)
	{
		return;
	}
	ExternalMontageOwner.Reset();
	if (bDeferredExternalMontageRep && !IsOwnerActorAuthoritative())
	{
		bDeferredExternalMontageRep = false;
		OnRep_ReplicatedAnimMontage();
	}
}

float UVBAbilitySystemComponent::PlayMontage(UGameplayAbility* AnimatingAbility,
	FGameplayAbilityActivationInfo ActivationInfo, UAnimMontage* Montage, float InPlayRate,
	FName StartSectionName, float StartTimeSeconds)
{
	if (ExternalMontageOwner.IsValid())
	{
		return 0.0f;
	}
	return Super::PlayMontage(AnimatingAbility, ActivationInfo, Montage, InPlayRate, StartSectionName, StartTimeSeconds);
}

void UVBAbilitySystemComponent::OnRep_ReplicatedAnimMontage()
{
	if (ExternalMontageOwner.IsValid())
	{
		bDeferredExternalMontageRep = true;
		return;
	}
	const FGameplayAbilityRepAnimMontage& Rep = GetRepAnimMontageInfo();
	// An old actor-channel update may arrive even after the finisher channel closed.
	// Retire that GAS play instance, not the asset: a later ability may play the same asset again.
	if (HasMatchingGameplayTag(VBGameplayTags::State_Dead) ||
		(bHasSupersededMontage && SupersededPlayInstanceId == Rep.PlayInstanceId))
	{
		bPendingMontageRep = false;
		return;
	}
	if (Rep.IsStopped)
	{
		// Base OnRep can first start a different montage even when its incoming state is stopped.
		// Never create that obsolete instance: stop only a matching existing GAS instance.
		if (LocalAnimMontageInfo.AnimMontage == Rep.Animation &&
			LocalAnimMontageInfo.PlayInstanceId == Rep.PlayInstanceId)
		{
			CurrentMontageStop(Rep.BlendTime);
		}
		bPendingMontageRep = false;
		return;
	}
	bHasSupersededMontage = false;
	Super::OnRep_ReplicatedAnimMontage();
}

