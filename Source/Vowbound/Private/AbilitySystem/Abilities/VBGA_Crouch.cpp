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
	// 질주 + C 는 미끄러지기라 자세 묶음과 따로 막는다(AVBCharacter::HandleCrouchPressed 가 미끄러지기로 보낸다).
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Locomotion_Sprinting);
	// 공격·처형·피격·회피 등 몸통을 쓰는 동작 중에는 앉지 않는다 - 묶음의 홈은 GetPostureBlockingTags 하나다.
	//   사망 게이트(FIND-067)도 이 묶음에 들어 있다: CancelAllAbilities() 는 이미 활성인 GA 만 끄고 신규 활성화는 못 막으므로
	//   태그 게이트가 필수이고, 입력 차단(AVBCharacter::HandleDeathCosmetic 의 DisableInput)과 이중화한다 - GA 를 우회하는 입력 경로가 실재한다(FIND-063).
	ActivationBlockedTags.AppendTags(GetPostureBlockingTags());
}

const FGameplayTagContainer& UVBGA_Crouch::GetPostureBlockingTags()
{
	// 가드가 들어 있는 이유: 가드 몽타주도 DefaultSlot(전신)이다 - UVBGA_Dodge 생성자 주석 «회피가 가드 몽타주를 같은 DefaultSlot 에서 덮는다».
	//   질주는 막는 것이 아니라 미끄러지기라 여기 없다.
	static const FGameplayTagContainer Tags = []
	{
		FGameplayTagContainer Result;
		Result.AddTag(VBGameplayTags::State_Combat_Attacking);
		Result.AddTag(VBGameplayTags::State_Combat_Finishing);
		Result.AddTag(VBGameplayTags::State_Combat_Guarding);
		Result.AddTag(VBGameplayTags::State_Combat_HitReacting);
		Result.AddTag(VBGameplayTags::State_Combat_Dodging);
		Result.AddTag(VBGameplayTags::State_Combat_Staggered);
		Result.AddTag(VBGameplayTags::State_Combat_Downed);
		Result.AddTag(VBGameplayTags::State_Combat_GettingUp);
		Result.AddTag(VBGameplayTags::State_Weapon_Summoning);
		Result.AddTag(VBGameplayTags::State_Weapon_Sheathing);
		Result.AddTag(VBGameplayTags::State_Dead);
		return Result;
	}();
	return Tags;
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
