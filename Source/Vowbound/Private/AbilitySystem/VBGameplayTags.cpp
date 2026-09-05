#include "AbilitySystem/VBGameplayTags.h"

namespace VBGameplayTags
{
	// Ability
	// 공격 계열 부모. 경직(UVBGA_Stagger)의 BlockAbilitiesWithTag 가 이 하나로 공격 leaf 전부를 막는다.
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack, "Ability.Attack");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack_Light, "Ability.Attack.Light");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Defense_Dodge, "Ability.Defense.Dodge");
	// 가드 GA 의 AssetTag — 무기 교체 시 CancelAbilities 로 가드를 끊기 위한 식별자(FIND-063).
	//  AssetTags 가 없으면 CancelAbilitiesWithTag 계열이 무음 실패한다(엔진은 대상 GA 의 AssetTags 를 매칭).
	UE_DEFINE_GAMEPLAY_TAG(Ability_Defense_Guard, "Ability.Defense.Guard");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Medical_Purify, "Ability.Medical.Purify");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Medical_Diagnose, "Ability.Medical.Diagnose");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Medical_Execute, "Ability.Medical.Execute");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Locomotion_Crouch, "Ability.Locomotion.Crouch");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Locomotion_Sprint, "Ability.Locomotion.Sprint");
	
	// State
	UE_DEFINE_GAMEPLAY_TAG(State_Dead, "State.Dead");
	UE_DEFINE_GAMEPLAY_TAG(State_Locomotion_Crouching, "State.Locomotion.Crouching");
	UE_DEFINE_GAMEPLAY_TAG(State_Locomotion_Sprinting, "State.Locomotion.Sprinting");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_InCombat, "State.Combat.InCombat");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Attacking, "State.Combat.Attacking");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Attacking_Light, "State.Combat.Attacking.Light");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_RiposteWindow, "State.Combat.RiposteWindow");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Dodging, "State.Combat.Dodging");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Staggered, "State.Combat.Staggered");
	// 가드/패링 상태 (DEC-009 카타나 파일럿) — VBGA_Guard 활성 중 부여, 데미지 hub 가 읽어 블록/패링 분기.
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Guarding, "State.Combat.Guarding");
	// 뚝심(SuperArmor): 보유 중엔 피격 히트리액트 몽타주를 스킵 → 공격이 안 끊김. 거대검 강스윙 중 부여.
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_SuperArmor, "State.Combat.SuperArmor");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_LowHealth, "State.Combat.LowHealth");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_LockedOn, "State.Combat.LockedOn");
	// 예고(텔레그래프) 중 — UVBGA_EnemyMeleeAttack 이 스윙 직전까지 로스태그로 부여한다.
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Telegraphing, "State.Combat.Telegraphing");
	UE_DEFINE_GAMEPLAY_TAG(State_Medical_Infected, "State.Medical.Infected");
	UE_DEFINE_GAMEPLAY_TAG(State_Medical_Purifying, "State.Medical.Purifying");
	
	UE_DEFINE_GAMEPLAY_TAG(State_Armed, "State.Armed");
	UE_DEFINE_GAMEPLAY_TAG(State_Armed_Katana, "State.Armed.Katana");
	UE_DEFINE_GAMEPLAY_TAG(State_Armed_BigSword, "State.Armed.BigSword");
	UE_DEFINE_GAMEPLAY_TAG(State_Armed_Fighter, "State.Armed.Fighter");
	UE_DEFINE_GAMEPLAY_TAG(State_Armed_Magic, "State.Armed.Magic");
	
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Exiting, "State.Combat.Exiting");
	
	UE_DEFINE_GAMEPLAY_TAG(State_Weapon_Summoning, "State.Weapon.Summoning");
	UE_DEFINE_GAMEPLAY_TAG(State_Weapon_Sheathing, "State.Weapon.Sheathing");
	UE_DEFINE_GAMEPLAY_TAG(State_Weapon_TempSheathed, "State.Weapon.TempSheathed");

	// Effect
	UE_DEFINE_GAMEPLAY_TAG(Effect_Damage_Physical, "Effect.Damage.Physical");
	UE_DEFINE_GAMEPLAY_TAG(Effect_Damage_Infection, "Effect.Damage.Infection");
	UE_DEFINE_GAMEPLAY_TAG(Effect_Purify, "Effect.Purify");
	UE_DEFINE_GAMEPLAY_TAG(Effect_Execute, "Effect.Execute");

	// Event
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_Hit, "Event.Combat.Hit");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_Death, "Event.Combat.Death");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_TelegraphStart, "Event.Combat.TelegraphStart");
	// Poise 파괴 에지 — UVBCombatAttributeSet 이 쏘고 UVBGA_Stagger 가 AbilityTriggers 로 받는다.
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_PoiseBroken, "Event.Combat.PoiseBroken");
	UE_DEFINE_GAMEPLAY_TAG(Event_Medical_Cured, "Event.Medical.Cured");
	UE_DEFINE_GAMEPLAY_TAG(Event_Montage_AttackTrace, "Event.Montage.AttackTrace");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_WindowOpen, "Event.Combo.WindowOpen");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_WindowClose, "Event.Combo.WindowClose");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_InputReceived, "Event.Combo.InputReceived");

	// Input
	UE_DEFINE_GAMEPLAY_TAG(Input_Attack_Light, "Input.Attack.Light");
	// 가드 입력 (DEC-009) — RMB 홀드. 강공격을 대체했고 Heavy 공격 태그 일습은 phase-1b 에서 제거 완료.
	UE_DEFINE_GAMEPLAY_TAG(Input_Combat_Guard, "Input.Combat.Guard");
	UE_DEFINE_GAMEPLAY_TAG(Input_Dodge, "Input.Dodge");
	UE_DEFINE_GAMEPLAY_TAG(Input_Purify, "Input.Purify");
	UE_DEFINE_GAMEPLAY_TAG(Input_Execute, "Input.Execute");
	UE_DEFINE_GAMEPLAY_TAG(Input_Crouch, "Input.Crouch");
	UE_DEFINE_GAMEPLAY_TAG(Input_Sprint, "Input.Sprint");
	UE_DEFINE_GAMEPLAY_TAG(Input_LockOn, "Input.LockOn");
	UE_DEFINE_GAMEPLAY_TAG(Input_SwitchTarget, "Input.SwitchTarget");
	UE_DEFINE_GAMEPLAY_TAG(Input_WeaponSummon, "Input.WeaponSummon");

	// AI
	UE_DEFINE_GAMEPLAY_TAG(AI_State_Idle, "AI.State.Idle");
	UE_DEFINE_GAMEPLAY_TAG(AI_State_Combat, "AI.State.Combat");
	UE_DEFINE_GAMEPLAY_TAG(AI_State_Dead, "AI.State.Dead");
	
	// DATA (SetByCaller용)
	UE_DEFINE_GAMEPLAY_TAG(Data_ReputationChange, "Data.ReputationChange");
	UE_DEFINE_GAMEPLAY_TAG(Data_MedicalAmount, "Data.MedicalAmount");
	UE_DEFINE_GAMEPLAY_TAG(Data_KnockbackStrength, "Data.KnockbackStrength");
	UE_DEFINE_GAMEPLAY_TAG(Data_KnockbackZ, "Data.KnockbackZ");
	UE_DEFINE_GAMEPLAY_TAG(Data_KnockbackYaw, "Data.KnockbackYaw");
	UE_DEFINE_GAMEPLAY_TAG(Data_PoiseDamage, "Data.PoiseDamage");

	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Purify, "Cooldown.Purify");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Execute, "Cooldown.Execute");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Attack_Light, "Cooldown.Attack.Light");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Dodge, "Cooldown.Dodge");
	
	// GameplayCue
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Damage_Physical, "GameplayCue.Damage.Physical");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Damage_Physical_Heavy, "GameplayCue.Damage.Physical.Heavy");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_State_Staggered, "GameplayCue.State.Staggered");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_State_Death, "GameplayCue.State.Death");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_HitStop, "GameplayCue.Combat.HitStop");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_HitFlash, "GameplayCue.Combat.HitFlash");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_SwingImpact, "GameplayCue.Combat.SwingImpact");
}
