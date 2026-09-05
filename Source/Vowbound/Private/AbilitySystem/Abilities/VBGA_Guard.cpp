// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Guard.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBTargetLockComponent.h"   // 가드 진입 자동 락온
#include "Character/VBWeaponStateComponent.h"
#include "Character/VBWeaponStateTypes.h"
#include "Data/VBParryConfig.h"   // UVBParryConfig::FindGuardSet — 무기 게이트의 데이터 원천(CHORE-032)
#include "Vowbound/Vowbound.h"

UVBGA_Guard::UVBGA_Guard()
{
	InputTag = VBGameplayTags::Input_Combat_Guard;

	// AssetTags 는 필수다. 이게 있어야 외부에서 이 GA 를 취소할 수 있다 (FIND-063, 2026-07-26).
	//  엔진의 CancelAbilities 계열은 대상 GA 의 AssetTags 를 매칭한다(AbilitySystemComponent_Abilities.cpp::UAbilitySystemComponent::CancelAbilities).
	//  비어 있으면 CancelAbilitiesWithTag / CancelAbilities 가 아무 로그 없이 무음 실패한다 —
	//  FIND-054 때 "공격이 가드를 취소" 안을 기각한 이유가 정확히 이것이었다.
	//  지금은 무기 교체(UVBWeaponStateComponent::ServerSummonWeapon)가 이 태그로 가드를 끊는다.
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VBGameplayTags::Ability_Defense_Guard);
	SetAssetTags(AssetTags);

	// 가드 상태 태그 — 데미지 hub 및 anim 이 참조. 활성 동안 모든 인스턴스에 부여된다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Guarding);
	// 회피/공격/사망 중엔 가드 진입 불가.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Dodging);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);
}

void UVBGA_Guard::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter());
	if (!VBChar)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 무기 게이트는 데이터다 (CHORE-032 청산). 카타나 리터럴을 지우고 config 조회로 대체했다.
	//  규약: WeaponGuardSets 키 존재 = 가드 권한, GuardMontage 유무 = 콘텐츠 유무.
	//  무기 확장은 소스 수정이 아니라 DA 행 1개. 게이트와 콘텐츠가 같은 데이터라 이중 진실이 원천 불가능.
	// 이 게이트를 BeginGuard 안으로 옮기지 말 것:
	//  ActivationOwnedTags(State.Combat.Guarding)는 EndAbility 로 회수된다. BeginGuard 안으로 옮기면
	//  가드 못 하는 무기가 Guarding 태그를 단 채 남아 UVBGA_LightAttack 의 ActivationBlockedTags 에 걸려
	//  가드도 못 하는데 공격까지 막힌다.
	const UVBWeaponStateComponent* WSC = VBChar->GetWeaponStateComponent();
	const UVBParryConfig* Cfg = VBChar->GetParryConfig();   // null 아님(CDO 폴백 = IVBGuardable 계약 1)
	if (!WSC || !Cfg->FindGuardSet(WSC->GetCurrentWeaponType()))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// BeginGuard 는 서버 시각 기록 + 몽타주. 패링 판정이 서버(PostGameplayEffectExecute)에서만 일어나므로 상태도 서버 권위.
	// ServerInitiated 라 클라도 여기 실행되지만, 클라에서 BeginGuard 하면 GuardStartServerTime 이 클라 시각이라 무의미 → 권한 가드.
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		VBChar->BeginGuard();
	}

	// 가드를 올리는 순간 자동 락온 (DEC-009 phase-2, user 요구 "가드 올리는 순간 락온").
	// 왜 로컬 조종자 분기인가: 대상 탐색이 카메라 기반이라 서버가 원격 클라 대신 할 수 없다
	//   (서버측 PlayerCameraManager 가 tick 되지 않아 stale - ToggleLockOn 과 같은 계약).
	//   ServerInitiated 라 이 함수는 양쪽에서 돌고, 각자 자기 몫만 한다.
	// 왜 ToggleLockOn 이 아닌가: 그건 이미 락온 중이면 해제한다. 가드마다 락이 켜졌다 꺼진다.
	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		// 플래그는 이 GA 가 소유한다(2026-08-09 이관). 순서가 뒤집힌 것에 의미가 있다 -
		// 예전엔 컴포넌트가 있어야 플래그를 읽을 수 있어서, 락온 컴포넌트가 없는 폰(적)은
		// 이 기능을 끄고 켜는 것 자체가 불가능했다.
		if (bAutoLockOnGuard)
		{
			if (UVBTargetLockComponent* TLC = VBChar->GetTargetLockComponent())
			{
				TLC->TryLockOnFromLocalView();
			}
		}
	}
}

void UVBGA_Guard::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter()))
		{
			VBChar->EndGuard();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
