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
}
