// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

namespace VBGameplayTags
{
	// Ability
	// 공격 계열의 부모 태그. 개별 공격 GA 의 AssetTags 는 이 아래 leaf 를 단다.
	// 왜 부모가 필요한가: 경직(UVBGA_Stagger)의 BlockAbilitiesWithTag 가 "공격 전부"를 하나로 가리켜야 한다.
	//   leaf 를 나열하면 공격을 하나 더할 때마다 경직 생성자를 고쳐야 한다.
	//   엔진 UAbilitySystemComponent::AreAbilityTagsBlocked 가 후보의 태그를 부모까지 확장해 비교하므로
	//   여기 부모 하나로 leaf 전부가 걸린다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Light);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Defense_Dodge);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Defense_Guard);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Medical_Purify);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Medical_Diagnose);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Medical_Execute);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Locomotion_Crouch);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Locomotion_Sprint);

	// State
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Locomotion_Crouching);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Locomotion_Sprinting);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_InCombat);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Attacking);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Attacking_Light);

	// 패링 성공 직후 열리는 반격 창. 이 태그가 있는 동안에만 UVBGA_Riposte 가 활성화된다.
	// 경공격은 State.Combat.Guarding 으로 차단돼 있어 둘이 같은 입력을 써도 경합이 성립하지 않는다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_RiposteWindow);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Dodging);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Staggered);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Guarding);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_SuperArmor);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_LowHealth);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_LockedOn);
	// 공격 예고 중(D3). 스윙이 나가기 전 UVBEnemyConfig::TelegraphDuration 동안 유지된다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Telegraphing);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Medical_Infected);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Medical_Purifying);
	
	// 무기 State
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed_Katana);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed_BigSword);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed_Fighter);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed_Magic);
	
	// 전투 상태
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Exiting);
	
	// 무기 동작
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Weapon_Summoning);		// 소환
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Weapon_Sheathing);		// 수납
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Weapon_TempSheathed);		// 파쿠르 임시 수납
	

	// Effect
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Effect_Damage_Physical);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Effect_Damage_Infection);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Effect_Purify);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Effect_Execute);

	// Event
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_Hit);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_Death);
	// 예고 시작 에지(D3). 비주얼/사운드가 붙는 훅이자 미래의 회피 보조·경고 UI 의 입력점.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_TelegraphStart);
	// Poise 가 0에 도달(D8). UVBGA_Stagger 의 AbilityTriggers 가 이것을 받는다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_PoiseBroken);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Medical_Cured);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Montage_AttackTrace);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combo_WindowOpen);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combo_WindowClose);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combo_InputReceived);

	// Input
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Light);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Combat_Guard);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Dodge);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Purify);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Execute);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Crouch);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Sprint);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_LockOn);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_SwitchTarget);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_WeaponSummon);

	// AI
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_State_Idle);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_State_Combat);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_State_Dead);
	
	// DATA (SetByCaller용)
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_ReputationChange);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_MedicalAmount);
	// 넉백 세기(cm/s) — 공격자 스탠스값을 데미지 스펙에 실어 피격자 hub(PostGameplayEffectExecute)로 전달
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_KnockbackStrength);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_KnockbackZ);
	// 넉백 수평 방향을 월드 yaw(도)로 싣는다. SetByCaller 는 float 만 나르는데, 넉백은 수평 성분과
	// 상방 성분(KnockbackZ)이 분리돼 있어 수평 방향은 yaw 하나로 완전히 표현된다 - 벡터를 쪼개 실을 이유가 없다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_KnockbackYaw);
	// 이 타격이 대상 Poise 에서 깎는 양(D8). 넉백 3종과 같은 축이라 같은 데미지 스펙에 실어 보낸다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_PoiseDamage);

	// Cooldown
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Purify);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Execute);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Attack_Light);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Dodge);
	
	// GameplayCue
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Damage_Physical);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Damage_Physical_Heavy);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_State_Staggered);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_State_Death);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_HitStop);
	// 히트스톱과 같은 Combat 가지에 둔다 — 둘 다 '타격 게임필' 축이고, 데미지 종류(Damage.*)와는 다른 축이다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_HitFlash);
	// 방향성 임팩트 쉐이크. 히트스톱/히트플래시와 같은 '타격 게임필' 축이라 같은 Combat 가지.
	// 무기별로 태그를 쪼개지 않는다 — 무기 차별화는 프로필 자산의 ShakeClass 가 담당하므로
	//  태그를 쪼개면 태그와 GCN BP 가 무기 수만큼 증식하기만 한다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_SwingImpact);
}
