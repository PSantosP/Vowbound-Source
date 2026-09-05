// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Crouch.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "GameFramework/Character.h"

UVBGA_Crouch::UVBGA_Crouch()
{
	InputTag = VBGameplayTags::Input_Crouch;
	bIsToggleAbility = true;
	
	FGameplayTagContainer Tags;
	Tags.AddTag(VBGameplayTags::Ability_Locomotion_Crouch);
	SetAssetTags(Tags);
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Locomotion_Crouching);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Locomotion_Sprinting);

	// 사망 게이트 (FIND-067). 사망 구현으로 State.Dead 가 처음 켜지는 순간 "시체가 앉는다"가 된다.
	// CancelAllAbilities() 는 *이미 활성인* GA 만 끄고 신규 활성화는 못 막으므로 태그 게이트가 필수.
	// 입력 차단(AVBCharacter::HandleDeathCosmetic 의 DisableInput)과 이중화 — GA 를 우회하는 입력 경로가
	// 실재하기 때문(FIND-063).
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);
}

void UVBGA_Crouch::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ACharacter* Char = GetAvatarCharacter();
	if (!Char)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Crouch는 CMC의 network prediction을 쓰는 특수 케이스 → 양쪽 모두 호출해야 함.
	//   서버: bWantsToCrouch=true → CMC tick → bIsCrouched=true → 복제
	//   클라: bWantsToCrouch=true → 로컬 prediction → 서버 reconciliation → 일치
	//   클라가 호출 안 하면: 서버 bIsCrouched 복제는 오지만 CMC::IsCrouching() 류 조건 쓰는 ABP에서 전환 실패.
	// 다른 CMC 경로(LaunchCharacter, Sprint CMC state 등)와 달리 Crouch는 UE 빌트인 prediction 플로우.
	Char->Crouch();
}

void UVBGA_Crouch::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// UnCrouch도 양쪽 호출 필요 (CMC prediction 대칭)
	if (ACharacter* Char = GetAvatarCharacter())
	{
		Char->UnCrouch();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
