// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Riposte.h"
#include "AbilitySystem/VBGameplayTags.h"

UVBGA_Riposte::UVBGA_Riposte()
{
	// 경공격과 같은 입력을 쓴다. 경합이 없는 근거는 헤더 주석 참조
	// (경공격은 Guarding 차단, 반격 창은 가드 중에만 열린다).
	InputTag = VBGameplayTags::Input_Attack_Light;

	// 반격 창이 열려 있을 때만 활성화된다. 이 태그는 패링 성립 시에만 붙는다.
	ActivationRequiredTags.AddTag(VBGameplayTags::State_Combat_RiposteWindow);

	// 공격 상태는 공유한다 - 가드 GA 가 Attacking 을 차단하므로 반격이 나가면 가드는 자연히 풀린다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);

	// 사망 게이트. 다른 공격 GA 와 동일 규칙 - 입력 차단과 이중화한다(FIND-063).
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);

	// 이 GA 의 식별자. 경직(UVBGA_Stagger)이 `Ability.Attack` 부모로 차단하므로 그 아래 잎을 들어야
	//   매칭된다. 2026-09-07 /verify 가 이 누락을 잡았다 - 반격만 그 차단을 비껴가고 있었는데,
	//   차단하는 쪽 주석은 「부모 하나로 Ability.Attack.* 전부가 걸린다」고 적고 있어 거짓이었다.
	//   자산에도 없음을 확인했다(Content 전체 uasset 에서 `Ability.Attack` 문자열 0건, 대조군 통과).
	// 왜 Light 를 재사용하지 않나: 반격은 경공격이 아니다. 차단·취소는 부모로 걸리므로 잎을 나눠도
	//   막는 쪽은 안 고친다 - 잎이 정확할수록 나중에 「반격만 예외」를 표현할 수 있다.
	// 왜 BP 가 아니라 여기인가: BP 저작이면 새 반격 BP 마다 빠질 수 있고, 빠졌다는 사실이
	//   「경직 중에도 반격이 나간다」로만 나타나 눈에 안 띈다(LightAttack 과 같은 근거).
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VBGameplayTags::Ability_Attack_Riposte);
	SetAssetTags(AssetTags);
}
