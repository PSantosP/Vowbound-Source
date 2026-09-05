// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"

UVBGameplayAbility::UVBGameplayAbility()
{
	// 네트워크: 서버가 활성화 시작 + 소유자 클라에 GA 인스턴스 복제 (2026-04-20 전환).
	// 이유:
	//   - ServerOnly는 소유자 클라에 인스턴스가 없어 몽타주 로컬 재생 불가 (Bug B).
	//   - ServerInitiated: 클라도 GA 인스턴스로 ExecuteAbility 실행 → PlayMontageAndWait Task가 로컬에서 재생.
	//   - 서버 전용 부작용(GE 적용, Spawn, CMC 변경)은 각 파생 GA에서 `ActorInfo->IsNetAuthority()` 가드로 보호.
	//   - 참조: Engine/Plugins/Runtime/GameplayAbilities/.../AbilitySystemComponent_Abilities.cpp::UAbilitySystemComponent::InternalTryActivateAbility
	//            (bIsLocal=false && !ServerOnly인 경우에만 ClientActivateAbilitySucceed 송출 → 클라 CallActivateAbility)
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	// Instancing: 액터당 1개 인스턴스 (ServerInitiated 복제의 필수 조건)
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

UVBAbilitySystemComponent* UVBGameplayAbility::GetVBAbilitySystemComponent() const
{
	return Cast<UVBAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
}

ACharacter* UVBGameplayAbility::GetAvatarCharacter() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
}

void UVBGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	
	// OnSpawn 정책: 부여 즉시 활성화 (패시브용)
	if (ActivationPolicy == EVBAbilityActivationPolicy::OnSpawn)
	{
		if (ActorInfo && !Spec.IsActive())
		{
			ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle);
		}
	}
}

void UVBGameplayAbility::PlayHitStop(float RealDuration, float TimeDilation)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}
	
	FGameplayCueParameters CueParams;
	CueParams.RawMagnitude = RealDuration;
	CueParams.NormalizedMagnitude = TimeDilation;
	ASC->ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_HitStop, CueParams);
}

void UVBGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	// 1단계: CommitAbility (모든 GA에 강제)
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	
	// 2단계: 자식의 실제 로직
	ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}






