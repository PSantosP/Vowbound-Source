// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_LightAttack.h"
#include "AbilitySystem/VBGameplayTags.h"

UVBGA_LightAttack::UVBGA_LightAttack()
{
	// 이 GA 의 식별자(AssetTags). 차단/취소 계열은 전부 대상 GA 의 AssetTags 를 매칭하므로,
	//   이것이 없으면 경직(UVBGA_Stagger)의 BlockAbilitiesWithTag 가 아무 로그 없이 무음 실패한다.
	//   그래서 BP 저작이 아니라 C++ 생성자에 심는다 - BP 에 두면 새 공격 BP 마다 빠질 수 있고,
	//   빠졌다는 사실이 "경직 중에도 공격이 나간다"로만 나타나 눈에 안 띈다.
	// 2026-08-19 신설 전까지 소스 전체에서 AssetTags 를 가진 GA 는 Crouch/Guard/Sprint 셋뿐이었고
	//   공격 계열은 상속 사슬을 포함해 0건이었다 - 즉 공격은 차단/취소 매칭 대상이 아예 아니었다.
	// 부모 태그(Ability.Attack)로 차단하는 쪽이 leaf 를 나열하는 것보다 낫다:
	//   엔진이 후보의 태그를 부모까지 확장해 비교하므로, 공격을 하나 더해도 차단 쪽은 안 고친다.
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VBGameplayTags::Ability_Attack_Light);
	SetAssetTags(AssetTags);

	InputTag = VBGameplayTags::Input_Attack_Light;
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking_Light);

	// 가드⊥공격 게이트를 양방향으로 완성 (FIND-054, 2026-07-26)
	// 가드 중(State.Combat.Guarding 보유)에는 경공격이 활성화되지 않는다.
	// 왜: 반대 방향은 이미 막혀 있었다(UVBGA_Guard 가 Attacking 차단, VBGA_Guard.cpp:31). 이쪽만 비어 있어
	//   RMB 홀드 + LMB 가 통과했고, 두 몽타주가 같은 DefaultSlot 이라 화면엔 공격만 보이는데
	//   AVBCharacter::GuardStartServerTime(VBCharacter.h:317)은 살아 있었다 → 자기 스윙 도중 맞은 피격이
	//   데미지 hub 의 가드 분기에서 패링/블록으로 처리됐다.
	// 왜 "공격이 가드를 취소"가 아닌가: CancelAbilitiesWithTag 는 대상 GA 의 AssetTags 를 매칭한다
	//   (엔진 AbilitySystemComponent_Abilities.cpp::UAbilitySystemComponent::CancelAbilities). 2026-07-26 정정 — 당시엔 UVBGA_Guard 에 AssetTags 가 없어
	//   무음 실패하는 것이 기각의 1차 근거였으나, FIND-063 대응으로 Ability.Defense.Guard 가 심겼다(VBGA_Guard ctor).
	//   즉 이제 기술적으로는 가능하다. 그럼에도 기각을 유지하는 근거는 아래 부작용과 phase-2 슬롯 예약이다.
	//   취소 경로는 가드 End 섹션(복귀 루트모션)이 공격 블렌드아웃에 먹히는 부작용도 동반한다.
	// 왜 GAS 네이티브인가: 커스텀 판정 코드 0줄 — UVBGA_HitReact 의 뚝심/사망 게이트와 동일 패턴
	//   (VBGA_HitReact.cpp:44-50 + 헤더 주석 :24-25 "능력이 활성화되지 않음").
	// 부작용(의도된 예약): "가드 중 공격" 슬롯이 비워진 채 예약된다. DEC-009 phase-2 리포스트가 그 슬롯의
	//   주인이며, 그때는 리포스트 전용 GA 만 이 태그를 차단하지 않게 만들면 된다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Guarding);

	// 반격 창이 열려 있는 동안에는 경공격이 뜨지 않는다 — 그 입력의 주인은 UVBGA_Riposte 다 (DEC-009 phase-2).
	// 왜 필요한가: 가드를 떼면 Guarding 이 사라져 경공격이 정상 활성화되고, 입력 디스패처는 성공한
	//   첫 후보에서 멈추므로 반격이 도달하지 못한다(2026-07-29 user 실측 "홀드해야만 반격이 나간다").
	//   창은 가드 유지와 무관하게 0.8s 살아 있어야 한다 — user 결정: "떼어도 0.8초 안이면 나가게".
	// 이로써 게이트가 양방향으로 닫힌다: 가드 중이면 Guarding 이, 창이 열려 있으면 RiposteWindow 가 경공격을 막는다.
	//   두 태그 모두 없을 때만 경공격이 이 입력의 주인이다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_RiposteWindow);

	// 사망 게이트 (FIND-067). 사망 구현으로 State.Dead 가 처음 켜지는 순간 "시체가 공격한다"가 된다.
	// CancelAllAbilities() 는 *이미 활성인* GA 만 끄고 신규 활성화는 못 막으므로 태그 게이트가 필수.
	// 입력 차단(AVBCharacter::HandleDeathCosmetic 의 DisableInput)과 이중화 — GA 를 우회하는 입력 경로가
	// 실재하기 때문(FIND-063). UVBGA_HitReact 의 사망 게이트와 동일 패턴.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);
}
