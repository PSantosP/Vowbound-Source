// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Sprint.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"

UVBGA_Sprint::UVBGA_Sprint()
{
	InputTag = VBGameplayTags::Input_Sprint;
	FGameplayTagContainer Tags;
	Tags.AddTag(VBGameplayTags::Ability_Locomotion_Sprint);
	SetAssetTags(Tags);
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Locomotion_Sprinting);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Locomotion_Crouching);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Attacking);

	// 사망 게이트 (FIND-067). 사망 구현으로 State.Dead 가 처음 켜지는 순간 "시체가 스프린트한다"가 된다.
	// CancelAllAbilities() 는 *이미 활성인* GA 만 끄고 신규 활성화는 못 막으므로 태그 게이트가 필수.
	// 입력 차단(AVBCharacter::HandleDeathCosmetic 의 DisableInput)과 이중화 — GA 를 우회하는 입력 경로가
	// 실재하기 때문(FIND-063).
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);
}

void UVBGA_Sprint::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter());
	if (!VBChar)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// StartSprint 내부는 ServerApplySprintState RPC. 서버에서만 호출하면 local 경로, 클라에서 호출하면 RPC 경유로 서버 재진입.
	// ServerInitiated로 클라도 ExecuteAbility 실행하므로 중복 호출 방지를 위해 권한 가드.
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		VBChar->StartSprint();
	}
}

void UVBGA_Sprint::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// EndAbility는 양쪽에서 호출될 수 있음(timer, cancel, explicit end). 서버에서만 실제 sprint 토글.
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter()))
		{
			VBChar->StopSprint();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
