// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"

class UAbilitySystemComponent;
class UGameplayEffect;

/**
 * 능력치 덧셈 효과 하나 걸기 - 장비 칸과 성장(연 스킬 칸의 합)이 같이 쓴다. 설계 .sw/remaining-features/b2-growth.md 2절.
 * 효과(무한 GE)의 SetByCaller 모디파이어를 전부 훑어 지도 값으로 채운다 - 지도에 없는 능력치는 0(무변화).
 *   지도에만 있고 GE 가 받지 않는 능력치는 경고한다(조용히 버려지지 않게). 실패(ASC·효과 없음, 스펙·적용 실패)면 무효 핸들.
 */
namespace VBStatBonus
{
	VOWBOUND_API FActiveGameplayEffectHandle Apply(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> Effect,
		const TMap<FGameplayTag, float>& Stats, const FString& SourceName);
}
