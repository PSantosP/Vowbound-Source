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
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Riposte);
	// 부모 태그. Dodge·Guard 를 한 번에 막을 때 쓴다 - 엔진이 후보 GA 의 AssetTags 를 부모까지
	// 확장해 비교하므로 이 하나로 Ability.Defense.* 전부가 걸린다(Ability.Attack 과 같은 수단).
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Defense);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Defense_Dodge);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Defense_Guard);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Medical_Purify);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Medical_Diagnose);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Medical_Execute);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Locomotion_Crouch);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Locomotion_Sprint);
	// UVBGA_Interact 의 AssetTag. 다른 GA 가 상호작용을 막거나 끊을 때 가리키는 이름이다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Interact);
	// UVBGA_UseItem 의 AssetTag(.sw/quickslot-input 6.1절). 다른 GA 가 아이템 사용을 막거나 끊을 때 가리키는 이름이다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Item_Use);

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
	// 회피 커밋: 회피 시작부터 AttackCancelSeconds 까지 공격이 롤을 끊지 못한다. 그 사이 공격 입력은 캐릭터가 저장했다가 커밋이 풀리면 내보낸다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_DodgeCommit);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Staggered);
	// 그로기(다운) - 넘어져 있는 동안(넘어짐·대기·기상 전부). UVBGA_Knockdown 이 유일한 생산자(루즈태그, 서버).
	//   Staggered 와 함께 붙는다(Knockdown 이 Stagger 의 파생이라 Staggered 는 상속으로 온다) - BT 는 Staggered 로 닫히고,
	//   이 태그는 «지금 넘어져 있다» 만 말한다. 소비자 = 다운 중 사망(사망 몽타주 생략), 다운 중 반격 GA(경직 취소 감시 생략).
	//   design .sw/boss/design.md §14.3.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Downed);
	// 히트리액트(플린치) 재생 중. UVBGA_HitReact 의 ActivationOwnedTags. Staggered(Poise 파탄)와 다른 태그인 이유:
	//   Staggered 는 «한 대로는 안 끊긴다» 를 MaxPoise 가 정하는 상태이고, 이것은 «맞은 그 순간» 이다.
	//   소비자 = BT_Infected 의 이동 분기 게이트(맞는 동안 걸어 나가지 않게). 2026-09-06 user 「맞으면서 도망간다」.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_HitReacting);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Guarding);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_SuperArmor);
	// 보스 페이즈 전환 몽타주가 도는 중. AVBBossBase 가 단독 생산자(로스태그). 소비자 둘 - BT_Infected 의 이동·공격 분기
	//   (Staggered·HitReacting·FinisherWindow 와 같은 반전 데코레이터 줄)와 UVBGA_EnemyMeleeAttack 의 ActivationBlockedTags.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_PhaseTransition);
	// 조우가 보류 중이다(시네마틱 장면 동안 - .sw/cinematic-playback 10절 4). UVBEncounterGateComponent 가 단독 생산자(로스태그).
	//   소비자 = UVBGA_EnemyMeleeAttack 의 ActivationBlockedTags. 행동 트리 멈춤은 AI 컨트롤러가 같은 문의 신호로 한다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Encounter_Held);
	// 처형 창이 열려 있다. 이 태그를 든 대상만 처형·정화의 대상이 된다.
	// UVBGA_FinisherWindow 가 단독 생산자다(2026-09-29 부터 켜질 때가 아니라 반응이 끝나 창이 «열릴 때» 붙인다). 시한부라서 «저체력» 이 아니라 «창» 이다 -
	//   체력이 문턱 아래여도 창이 닫혀 있으면 이 태그는 없고, 그때는 처형이 안 걸린다.
	// 종전 State.Combat.LowHealth 를 대체한다(2026-09-11). 그 태그는 선언만 있고 생산자도 소비자도 없었다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_FinisherWindow);
	// 처형 창 GA 가 켜져 있다 - 반응이 끝나기를 기다리는 중이거나 이미 열렸다. UVBGA_FinisherWindow 의 ActivationOwnedTags(단독 생산자).
	//   읽는 곳은 UVBAbilitySystemStatics::IsFinisherWindowArmed 하나를 거친다. 소비자 = UVBGA_Stagger 의 기절 게이지 소진 거절,
	//   UVBBTService_UpdateCombat 의 포커스 게이트(design .sw/hit-reaction-tiers/design.md 1-B 네·다섯 번째 PIE).
	//   위 창 태그를 여는 순간으로 미루자 대기 구간에 경직이 들어와 마무리 타의 반응을 끊고 곧바로 창에 끊겼다 - 그 구간을 이 태그가 덮는다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_FinisherWindowArmed);

	// 처형을 «거는 쪽» 의 상태. 위 FinisherWindow(피해자)와 짝이다.
	// 생산자는 UVBGA_Execute 의 ActivationOwnedTags 하나뿐이고 엔진이 EndAbility 에서 뗀다.
	// 연출 갈래는 장면이 끝날 때까지 어빌리티가 살아 있으므로 태그 수명이 곧 장면 수명이다.
	// 소비자 셋: 피격 반응·경직이 자기를 막고(반응 GA 는 Ability.* 자산 태그가 없어 차단 목록으로는 못 막는다),
	//   피해 계산이 무적으로 읽고, 비-GA 진입점(무기 교체·트래버설)이 IsActorFinishing 으로 묻는다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Finishing);

	// 처형을 버티고 일어나는 중. UVBGA_GetUp 이 단독 생산자다(ActivationOwnedTags).
	//   이 동안 공격을 막는다 - 일어나자마자 때릴 수 있으면 처형을 버틴 것이 그냥 이득이 된다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_GettingUp);
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

	// 환경 상호작용 중. 생산자는 UVBGA_Interact 의 ActivationOwnedTags 하나다.
	//   공격·회피가 상호작용 몽타주를 덮지 못하게 막는 쪽은 GA 의 BlockAbilitiesWithTag 이고,
	//   이 태그는 GA 가 아닌 진입점(무기 교체 등)이 «지금 상호작용 중인가» 를 묻는 데 쓴다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Interacting);
	

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
	// 실효 등급이 넘어뜨림 이상인 타격(DEC-025). 허브가 Event.Combat.Hit 보다 먼저 쏘고 UVBGA_Knockdown 이 받는다.
	// 왜 Hit 과 따로인가: 넘어짐 GA 가 먼저 서서 Downed 를 붙여야 뒤따르는 Hit 의 플린치가 저절로 막힌다.
	//   같은 이벤트에 둘을 걸면 어느 쪽이 먼저 서느냐에 따라 결과가 뒤집힌다(FinisherWindow 와 같은 판단).
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_KnockedDown);

	// 처형을 맞았는데 죽지 않았다. UVBGA_Execute 가 장면 끝에 쏘고 UVBGA_GetUp 이 받는다.
	//   OptionalObject 에 기상 몽타주를 싣는다 - 클립이 처형마다 짝이라(카타나 SSH / 대검 2H)
	//   피해자가 「어떤 처형에 맞았는지」를 알 필요 없이 맞는 클립을 받는다.
	// 공격 버튼이 처형을 여는 길. 생산자 = UVBGA_MeleeAttackBase(bCanOpenFinisher 가 켜진 것만),
	// 소비자 = UVBGA_Execute 의 AbilityTriggers.
	// 왜 입력 태그를 하나 더 두지 않고 이벤트인가: 그러면 Execute 가 «어느 입력으로 들어왔나» 를
	//   알아야 한다. 이벤트로 넘기면 Execute 는 지금 모양 그대로 두고 진입만 하나 더 생긴다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_ExecuteRequested);

	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_FinisherSurvived);
	// 처형 창 개방 에지 — UVBHealthAttributeSet 이 체력 문턱 아래에서 쏘고 UVBGA_FinisherWindow 가 받는다.
	// Poise 파탄과 같은 이벤트를 쓰지 않는다: 트리거 하나에 GA 둘이 걸리면 어느 쪽이 먼저 서느냐에
	//   따라 서로의 ActivationBlockedTags 판정이 뒤집힌다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_FinisherWindow);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Medical_Cured);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Montage_AttackTrace);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combo_WindowOpen);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combo_WindowClose);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combo_InputReceived);

	// Input
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Light);
	// 적 «슈퍼아머 반격» GA 의 InputTag(design .sw/enemy-soldier/design.md §3). 플레이어 입력이 아니라 BT 가 고르는 이름이다 -
	//   UVBEnemyConfig::ArmoredCounterAttackInputTag 가 이 값을 가리키고, 연속 피격 조건이 맞으면 GetAttackInputTag 가 이것을 돌려준다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Armored);
	// 보스 페이즈 2 의 공격 GA 태그. 페이즈 1 은 Input_Attack_Light 를 그대로 쓴다 - 태그는 «어느 GA 를 켤까» 의
	//   선택자라 ASC 가 다르면 병사와 겹쳐도 충돌하지 않는다. 페이즈 2 는 같은 ASC 안에서 갈라야 해서 따로 둔다.
	// 왜 ini 가 아니라 네이티브인가: 이 프로젝트의 Input.* 태그는 전부 네이티브이고(GA 생성자가 기본 InputTag 를 상수로 든다),
	//   태그 하나만 ini 로 빼면 «어디에 정의됐나» 의 홈이 둘이 된다. 페이즈 3 이 생기면 이 줄 아래에 하나 더 적는다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Boss_Phase2);
	// 보스가 넘어진 채 휘두르는 반격 GA 의 태그. BT 가 아니라 UVBGA_Knockdown 이 켠다(BT 는 Staggered 로 닫혀 있다).
	//   페이즈 태그와 같은 이유로 네이티브다. design .sw/boss/design.md §14.3.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Boss_Down);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Combat_Guard);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Dodge);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Purify);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Execute);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Crouch);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Sprint);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_LockOn);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_SwitchTarget);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_WeaponSummon);
	// 환경 상호작용(문·레버 등). UVBGA_Interact 의 InputTag. 탭과 홀드가 같은 입력이고 갈림은 대상 설정이 정한다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Interact);

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
	// 공격이 선언한 반응 등급(EVBHitImpact 의 서수, DEC-025). 없으면 받는 쪽이 가벼움으로 읽는다 - 비근접 피해의 현행 보존.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_HitImpact);
	// 띄우는 높이(cm). 실효 등급이 띄움일 때만 허브가 읽는다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_LaunchHeight);

	// Combat.Impact - 허브가 판정한 «실효» 반응 등급(DEC-025). Event.Combat.Hit 의 InstigatorTags 에 하나만 실린다.
	//  공격이 선언한 값이 아니라 받는 쪽 저항까지 반영한 값이다 - 반응 GA 는 이 태그만 보고 콘텐츠를 고른다.
	//  태그가 없는 이벤트(패링 되받기 등 옛 경로)는 가벼움으로 읽는다(VBHitImpactFromTags).
	// None 은 «저항이 등급을 다 깎았다» 다. 태그 없음(= 옛 경로 = 가벼움)과 갈라야 해서 따로 둔다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Impact_None);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Impact_Light);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Impact_Heavy);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Impact_Knockdown);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Impact_Launch);

	// Cooldown
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Purify);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Execute);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Attack_Light);
	// 적 슈퍼아머 반격의 쿨다운 태그. 보통 공격(Light)과 다른 태그여야 둘이 서로를 막지 않는다 - 반격만 길게 쉰다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Attack_Armored);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Dodge);
	// 처형 창의 재무장 쿨다운. 창이 닫힌 뒤 이만큼은 다시 안 열린다 - 없으면 약한 연타가 적을 영구 무력화한다.
	// 이 계열 태그들과 마찬가지로 C++ 소비자가 없다. GE 자산이 이름으로 들고 엔진의 CheckCooldown 이 읽는다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_FinisherWindow);
	
	// GameplayCue
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Damage_Physical);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Damage_Physical_Heavy);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_State_Staggered);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_State_Death);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_HitStop);
	// 히트스톱과 같은 Combat 가지에 둔다 — 둘 다 '타격 게임필' 축이고, 데미지 종류(Damage.*)와는 다른 축이다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_HitFlash);

	// 처형 창이 열린 동안 대상 몸에 거는 지속 발광. UVBGA_FinisherWindow 가 켜고 끈다.
	//   루프 큐인 이유: 발광은 보는 모든 사람에게 보여야 하는데 능력 본문은 서버에서만 돈다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_FinisherWindow);
	// 방향성 임팩트 쉐이크. 히트스톱/히트플래시와 같은 '타격 게임필' 축이라 같은 Combat 가지.
	// 무기별로 태그를 쪼개지 않는다 — 무기 차별화는 프로필 자산의 ShakeClass 가 담당하므로
	//  태그를 쪼개면 태그와 GCN BP 가 무기 수만큼 증식하기만 한다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_SwingImpact);
	// 적 공격 예고(design §11). Add/Remove 큐 - 예고 시작에 붙고 스윙 시작·취소에 떨어진다.
	//  답(Attack.Answer.*)은 파라미터 AggregatedSourceTags 로 싣고, 예고 길이는 RawMagnitude 로 싣는다.
	//  답마다 큐 태그를 쪼개지 않는다 - 색·소리 차별화는 UVBTelegraphConfig 의 행이 담당한다(SwingImpact 와 같은 판단).
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_Telegraph);

	// Attack.Answer - 그 공격에 플레이어가 무엇으로 답할 수 있는가(design §11). UVBAttackConfig::AttackAnswer 의 값.
	//  두 소비자가 같은 값을 읽는다: 예고 큐(색·소리 = 약속)와 데미지 허브(가드·패링 통과 여부 = 이행).
	//  Parryable = 가드·패링이 통한다(흰색). DodgeOnly = 가드를 무시한다(빨강) - 회피 무적은 여전히 통한다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attack_Answer_Parryable);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attack_Answer_DodgeOnly);

	// UI.Layer - 뿌리 레이아웃(UVBPrimaryLayout)의 화면 층 넷. 이름은 Lyra 와 같다(.sw/ui-foundation 2.1).
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_Game);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_GameMenu);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_Menu);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_Modal);

	// UI.Screen - 화면을 여는 쪽이 부르는 이름. 층·위젯은 UVBUISettings::Screens 표가 정한다.
	//  코드가 직접 여는 화면만 네이티브로 둔다. 데이터(대화 효과 등)만 여는 화면은 ini 태그로 충분하다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_HUD);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Pause);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_GameOver);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_MainMenu);

	// Melee.Shape - 무기 궤적 판정이 어느 «날» 로 쓸지(.sw/weapon-trace 3절). 모양 자체는 공격자의 UVBMeleeTraceComponent 가 든다.
	//  Weapon = 지금 든 무기(외형 데이터에서 계산), 나머지 = 뼈 기준(맨손·발차기·무기 없는 적).
	//  부모 태그는 받는 쪽(공격 GA)이 이벤트 태그 묶음에서 모양을 골라낼 때 쓴다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_Weapon);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_HandL);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_HandR);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_FootL);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_FootR);
	// 판정 창 이벤트의 표지. 창 시작도 기존 Event.Montage.AttackTrace 로 가므로(「닿는 프레임」 신호 계약) 옛 한 프레임 노티와 가르는 데 쓴다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Window);
	// 판정 창 닫힘. UVBANS_AttackTraceWindow::NotifyEnd 가 보낸다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Montage_AttackTraceEnd);

	// 아이템 (.sw/narrative 10절). 사건은 UVBGameEventSubsystem 으로 가고 대상 태그 = 아이템 ItemId. 퀘스트 목표가 이것을 듣는다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Acquired);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Used);
	// 사용 동작(.sw/quickslot-input 6.1절). 둘은 GAS 게임플레이 이벤트다 - 위 둘(게임 사건)과 길이 다르다.
	//   Use = 인벤토리가 폰 ASC 로 보내는 시작 요청(UVBGA_UseItem 의 트리거, OptionalObject = 아이템). UseMarker = 몽타주 표식 노티가 보내는 확정 시각.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Use);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_UseMarker);
	// 장비(.sw/remaining-features/b1-equipment.md). 칸 종류 넷 - 칸 배치(머리·몸·팔·장신구·장신구)는 UVBEquipmentComponent::SlotLayout 데이터가 정한다.
	//   사건 둘은 게임 사건(UVBGameEventSubsystem, 대상 = ItemId)이다 - 퀘스트가 «무엇을 입었나» 를 듣는다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Slot_Head);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Slot_Body);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Slot_Arms);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Slot_Charm);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Equipped);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Unequipped);
	// 성장(.sw/remaining-features/b2-growth.md) - 게임 사건(UVBGameEventSubsystem). LevelUp 의 수 = 오른 레벨 수, SkillUnlocked 의 대상 = 연 칸(Skill.Node.*).
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Progression_LevelUp);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Progression_SkillUnlocked);
	// 스킬 트리 화면(UVBSkillTreeScreenWidget). 층·클래스는 UVBUISettings::Screens.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_SkillTree);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Inventory);
	// 치유량 SetByCaller(GE_Heal). 치유량의 주인은 GE 를 거는 쪽의 데이터(아이템 사용 효과 등)다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Heal);

	// 대화 (.sw/narrative 11절). 노드의 ReachedTag 가 있으면 이 사건의 대상 태그로 방송된다.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Dialogue_NodeReached);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Dialogue);

	// 퀘스트 (.sw/narrative 12절). 사건은 UVBGameEventSubsystem 으로 간다 - 퀘스트 일지가 듣는다. 대상 태그는 각 주석.
	// 적 처치. 대상 = 죽은 적의 UVBEnemyConfig::EnemyTags.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Enemy_Killed);
	// 상호작용 완료. 대상 = 그 대상의 UVBInteractableComponent::EventTags(비면 방송 안 함).
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Interaction_Completed);
	// 영역 진입(.sw/prologue-opening 3.3). 대상 = AVBAreaTrigger::EventTags(비면 방송 안 함).
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Area_Entered);
	// 퀘스트 자신의 진행. 대상 = QuestId.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Quest_Started);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Quest_StageAdvanced);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Quest_Completed);
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Journal);
	// 상점 화면(.sw/narrative 13.5). 여는 쪽 = AVBPlayerController(상점 컴포넌트의 보기를 듣는다).
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Shop);
	// 키 다시 지정 화면(.sw/quickslot-input 10.1). 여는 쪽 = UVBSettingsWidget 의 «조작» 버튼. Modal 층.
	VOWBOUND_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_KeyBindings);
}
