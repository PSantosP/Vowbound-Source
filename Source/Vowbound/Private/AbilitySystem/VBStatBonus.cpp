// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBStatBonus.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Vowbound/Vowbound.h"

namespace VBStatBonus
{
	FActiveGameplayEffectHandle Apply(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> Effect,
		const TMap<FGameplayTag, float>& Stats, const FString& SourceName)
	{
		if (!ASC || !Effect)
		{
			return FActiveGameplayEffectHandle();
		}
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Effect, 1.0f, ASC->MakeEffectContext());
		if (!Spec.IsValid())
		{
			return FActiveGameplayEffectHandle();
		}
		TSet<FGameplayTag> Known;
		for (const FGameplayModifierInfo& Modifier : Effect.GetDefaultObject()->Modifiers)
		{
			if (Modifier.ModifierMagnitude.GetMagnitudeCalculationType() == EGameplayEffectMagnitudeCalculation::SetByCaller)
			{
				const FGameplayTag DataTag = Modifier.ModifierMagnitude.GetSetByCallerFloat().DataTag;
				Known.Add(DataTag);
				Spec.Data->SetSetByCallerMagnitude(DataTag, Stats.FindRef(DataTag));
			}
		}
		for (const TPair<FGameplayTag, float>& Stat : Stats)
		{
			VB_CLOG(!Known.Contains(Stat.Key), Warning, "능력치 효과: %s 의 능력치 %s 를 %s 가 받지 않는다 - 그 능력치는 안 바뀐다(GE 에 모디파이어를 더하라)",
				*SourceName, *Stat.Key.ToString(), *GetNameSafe(Effect.Get()));
		}
		return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}
