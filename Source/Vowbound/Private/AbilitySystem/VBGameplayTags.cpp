// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBGameplayTags.h"

namespace VBGameplayTags
{
	// Ability
	// 공격 계열 부모. 경직(UVBGA_Stagger)의 BlockAbilitiesWithTag 가 이 하나로 공격 leaf 전부를 막는다.
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack, "Ability.Attack");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack_Light, "Ability.Attack.Light");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack_Riposte, "Ability.Attack.Riposte");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Defense, "Ability.Defense");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Defense_Dodge, "Ability.Defense.Dodge");
	// 가드 GA 의 AssetTag — 무기 교체 시 CancelAbilities 로 가드를 끊기 위한 식별자(FIND-063).
	//  AssetTags 가 없으면 CancelAbilitiesWithTag 계열이 무음 실패한다(엔진은 대상 GA 의 AssetTags 를 매칭).
	UE_DEFINE_GAMEPLAY_TAG(Ability_Defense_Guard, "Ability.Defense.Guard");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Medical_Purify, "Ability.Medical.Purify");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Medical_Diagnose, "Ability.Medical.Diagnose");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Medical_Execute, "Ability.Medical.Execute");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Locomotion_Crouch, "Ability.Locomotion.Crouch");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Locomotion_Sprint, "Ability.Locomotion.Sprint");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Interact, "Ability.Interact");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Item_Use, "Ability.Item.Use");
	
	// State
	UE_DEFINE_GAMEPLAY_TAG(State_Dead, "State.Dead");
	UE_DEFINE_GAMEPLAY_TAG(State_Locomotion_Crouching, "State.Locomotion.Crouching");
	UE_DEFINE_GAMEPLAY_TAG(State_Locomotion_Sprinting, "State.Locomotion.Sprinting");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_InCombat, "State.Combat.InCombat");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Attacking, "State.Combat.Attacking");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Attacking_Light, "State.Combat.Attacking.Light");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_RiposteWindow, "State.Combat.RiposteWindow");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Dodging, "State.Combat.Dodging");
	// 회피 커밋(2026-09-05): 무적(Dodging, 0.4초)과 별개로 «공격이 롤을 끊을 수 없는 구간». user 「회피 도중에 끊긴다」 - 종전엔 공격이 언제든 롤을 잘랐다.
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_DodgeCommit, "State.Combat.DodgeCommit");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Staggered, "State.Combat.Staggered");
	// 그로기(다운) 중. UVBGA_Knockdown 이 넘어짐~기상 사이에 든다(design .sw/boss/design.md §14).
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Downed, "State.Combat.Downed");
	// 가드/패링 상태 (DEC-009 카타나 파일럿) — VBGA_Guard 활성 중 부여, 데미지 hub 가 읽어 블록/패링 분기.
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Guarding, "State.Combat.Guarding");
	// 슈퍼아머(SuperArmor): 보유 중엔 피격 히트리액트 몽타주를 스킵 → 공격이 안 끊김. 거대검 강스윙 중 부여.
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_SuperArmor, "State.Combat.SuperArmor");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_PhaseTransition, "State.Combat.PhaseTransition");
	UE_DEFINE_GAMEPLAY_TAG(State_Encounter_Held, "State.Encounter.Held");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_FinisherWindow, "State.Combat.FinisherWindow");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_FinisherWindowArmed, "State.Combat.FinisherWindowArmed");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Finishing, "State.Combat.Finishing");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_GettingUp, "State.Combat.GettingUp");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_LockedOn, "State.Combat.LockedOn");
	// 예고(텔레그래프) 중 — UVBGA_EnemyMeleeAttack 이 스윙 직전까지 로스태그로 부여한다.
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Telegraphing, "State.Combat.Telegraphing");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_HitReacting, "State.Combat.HitReacting");
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
	UE_DEFINE_GAMEPLAY_TAG(State_Interacting, "State.Interacting");

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
	// 넘어뜨림 이상의 실효 등급(DEC-025) — 허브가 Hit 보다 먼저 쏘고 UVBGA_Knockdown 이 받는다.
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_KnockedDown, "Event.Combat.KnockedDown");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_ExecuteRequested, "Event.Combat.ExecuteRequested");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_FinisherSurvived, "Event.Combat.FinisherSurvived");
	// 처형 창 개방 에지 — UVBHealthAttributeSet 이 쏘고 UVBGA_FinisherWindow 가 AbilityTriggers 로 받는다.
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_FinisherWindow, "Event.Combat.FinisherWindow");
	UE_DEFINE_GAMEPLAY_TAG(Event_Medical_Cured, "Event.Medical.Cured");
	UE_DEFINE_GAMEPLAY_TAG(Event_Montage_AttackTrace, "Event.Montage.AttackTrace");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_WindowOpen, "Event.Combo.WindowOpen");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_WindowClose, "Event.Combo.WindowClose");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_InputReceived, "Event.Combo.InputReceived");

	// Input
	UE_DEFINE_GAMEPLAY_TAG(Input_Attack_Light, "Input.Attack.Light");
	UE_DEFINE_GAMEPLAY_TAG(Input_Attack_Armored, "Input.Attack.Armored");
	UE_DEFINE_GAMEPLAY_TAG(Input_Attack_Boss_Phase2, "Input.Attack.Boss.Phase2");
	UE_DEFINE_GAMEPLAY_TAG(Input_Attack_Boss_Down, "Input.Attack.Boss.Down");
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
	UE_DEFINE_GAMEPLAY_TAG(Input_Interact, "Input.Interact");

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
	UE_DEFINE_GAMEPLAY_TAG(Data_HitImpact, "Data.HitImpact");
	UE_DEFINE_GAMEPLAY_TAG(Data_LaunchHeight, "Data.LaunchHeight");

	UE_DEFINE_GAMEPLAY_TAG(Combat_Impact_None, "Combat.Impact.None");
	UE_DEFINE_GAMEPLAY_TAG(Combat_Impact_Light, "Combat.Impact.Light");
	UE_DEFINE_GAMEPLAY_TAG(Combat_Impact_Heavy, "Combat.Impact.Heavy");
	UE_DEFINE_GAMEPLAY_TAG(Combat_Impact_Knockdown, "Combat.Impact.Knockdown");
	UE_DEFINE_GAMEPLAY_TAG(Combat_Impact_Launch, "Combat.Impact.Launch");

	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Purify, "Cooldown.Purify");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Execute, "Cooldown.Execute");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Attack_Light, "Cooldown.Attack.Light");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Attack_Armored, "Cooldown.Attack.Armored");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Dodge, "Cooldown.Dodge");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_FinisherWindow, "Cooldown.FinisherWindow");
	
	// GameplayCue
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Damage_Physical, "GameplayCue.Damage.Physical");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Damage_Physical_Heavy, "GameplayCue.Damage.Physical.Heavy");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_State_Staggered, "GameplayCue.State.Staggered");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_State_Death, "GameplayCue.State.Death");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_HitStop, "GameplayCue.Combat.HitStop");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_HitFlash, "GameplayCue.Combat.HitFlash");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_FinisherWindow, "GameplayCue.Combat.FinisherWindow");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_SwingImpact, "GameplayCue.Combat.SwingImpact");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_Telegraph, "GameplayCue.Combat.Telegraph");

	// Attack.Answer (design §11) - 공격에 대한 «답». 예고 큐(약속)와 데미지 허브(이행)가 같은 값을 읽는다.
	UE_DEFINE_GAMEPLAY_TAG(Attack_Answer_Parryable, "Attack.Answer.Parryable");
	UE_DEFINE_GAMEPLAY_TAG(Attack_Answer_DodgeOnly, "Attack.Answer.DodgeOnly");

	// UI (.sw/ui-foundation 2.1)
	UE_DEFINE_GAMEPLAY_TAG(UI_Layer_Game, "UI.Layer.Game");
	UE_DEFINE_GAMEPLAY_TAG(UI_Layer_GameMenu, "UI.Layer.GameMenu");
	UE_DEFINE_GAMEPLAY_TAG(UI_Layer_Menu, "UI.Layer.Menu");
	UE_DEFINE_GAMEPLAY_TAG(UI_Layer_Modal, "UI.Layer.Modal");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_HUD, "UI.Screen.HUD");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Pause, "UI.Screen.Pause");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_GameOver, "UI.Screen.GameOver");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_MainMenu, "UI.Screen.MainMenu");

	// 무기 궤적 판정 (.sw/weapon-trace)
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape, "Melee.Shape");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_Weapon, "Melee.Shape.Weapon");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_HandL, "Melee.Shape.HandL");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_HandR, "Melee.Shape.HandR");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_FootL, "Melee.Shape.FootL");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_FootR, "Melee.Shape.FootR");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Window, "Melee.Window");
	UE_DEFINE_GAMEPLAY_TAG(Event_Montage_AttackTraceEnd, "Event.Montage.AttackTraceEnd");

	// 아이템 (.sw/narrative 10절)
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_Acquired, "Event.Item.Acquired");
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_Used, "Event.Item.Used");
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_Use, "Event.Item.Use");
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_UseMarker, "Event.Item.UseMarker");
	UE_DEFINE_GAMEPLAY_TAG(Equipment_Slot_Head, "Equipment.Slot.Head");
	UE_DEFINE_GAMEPLAY_TAG(Equipment_Slot_Body, "Equipment.Slot.Body");
	UE_DEFINE_GAMEPLAY_TAG(Equipment_Slot_Arms, "Equipment.Slot.Arms");
	UE_DEFINE_GAMEPLAY_TAG(Equipment_Slot_Charm, "Equipment.Slot.Charm");
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_Equipped, "Event.Item.Equipped");
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_Unequipped, "Event.Item.Unequipped");
	UE_DEFINE_GAMEPLAY_TAG(Event_Progression_LevelUp, "Event.Progression.LevelUp");
	UE_DEFINE_GAMEPLAY_TAG(Event_Progression_SkillUnlocked, "Event.Progression.SkillUnlocked");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_SkillTree, "UI.Screen.SkillTree");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Inventory, "UI.Screen.Inventory");
	UE_DEFINE_GAMEPLAY_TAG(Data_Heal, "Data.Heal");
	UE_DEFINE_GAMEPLAY_TAG(Event_Dialogue_NodeReached, "Event.Dialogue.NodeReached");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Dialogue, "UI.Screen.Dialogue");
	UE_DEFINE_GAMEPLAY_TAG(Event_Enemy_Killed, "Event.Enemy.Killed");
	UE_DEFINE_GAMEPLAY_TAG(Event_Interaction_Completed, "Event.Interaction.Completed");
	UE_DEFINE_GAMEPLAY_TAG(Event_Area_Entered, "Event.Area.Entered");
	UE_DEFINE_GAMEPLAY_TAG(Event_Quest_Started, "Event.Quest.Started");
	UE_DEFINE_GAMEPLAY_TAG(Event_Quest_StageAdvanced, "Event.Quest.StageAdvanced");
	UE_DEFINE_GAMEPLAY_TAG(Event_Quest_Completed, "Event.Quest.Completed");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Journal, "UI.Screen.Journal");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Shop, "UI.Screen.Shop");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_KeyBindings, "UI.Screen.KeyBindings");
}
