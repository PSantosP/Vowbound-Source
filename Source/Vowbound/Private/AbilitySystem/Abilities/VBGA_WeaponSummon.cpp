// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_WeaponSummon.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"

UVBGA_WeaponSummon::UVBGA_WeaponSummon()
{
	bIsToggleAbility = false;
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Weapon_Summoning);
	
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Weapon_Summoning);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Weapon_Sheathing);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
	// ActivationBlockedTags.AddTag(VBGameplayTags::State_Traversal);
	
	InputTag = VBGameplayTags::Input_WeaponSummon;
}

void UVBGA_WeaponSummon::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter());

	if (!VBChar)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UVBWeaponStateComponent* WSC = VBChar->GetWeaponStateComponent();
	if (!WSC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// WSC->SummonWeapon/SheatheWeapon은 내부적으로 Server RPC로 라우팅됨 (§12 RPC #3, #4).
	// 서버에서 호출 시 local 경로, 클라에서 호출 시 RPC 송신. ServerInitiated 양쪽 실행에서
	// 클라 호출까지 가면 불필요한 RPC + 서버 중복 호출 (idempotent지만 낭비).
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (WSC->IsWeaponSummoned())
		{
			WSC->SheatheWeapon();
		}
		else
		{
			// FIND-030: 선택 무기(상태2 잔존) → 마지막 사용 무기 → 디폴트 — 자동납도 뒤에도 직전 무기 재발도
			WSC->SummonWeapon(WSC->GetResummonWeaponType());
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
