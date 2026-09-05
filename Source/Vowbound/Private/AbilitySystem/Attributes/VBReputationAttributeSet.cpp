// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UVBReputationAttributeSet::UVBReputationAttributeSet()
{
	// 기본값: 중립 상태
	InitReputation(0.0f);
}

void UVBReputationAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// 여기서 잡히는 것은 CurrentValue 뿐이다(엔진: FGameplayAttribute::SetNumericValueChecked 가
	// SetCurrentValue 직전에 이 함수를 부른다). BaseValue 클램프는 PostGameplayEffectExecute 소관이다.
	if (Attribute == GetReputationAttribute())
	{
		ClampAttributeOnChange(Attribute, NewValue, ReputationMin, ReputationMax);
	}
}

void UVBReputationAttributeSet::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// IncomingReputationChange 처리 (Meta Attribute 파이프라인)
	if (Data.EvaluatedData.Attribute == GetIncomingReputationChangeAttribute())
	{
		const float ChangeValue = GetIncomingReputationChange();
		SetIncomingReputationChange(0.0f);

		if (ChangeValue != 0.0f)
		{
			// 이 클램프는 PreAttributeChange 와 중복이 아니다 - 저쪽은 CurrentValue, 이쪽은 BaseValue 다.
			// 지우면 base 가 +-100 을 넘어 흘러가고, 화면에는 클램프된 current 가 보여 눈에 안 띈다.
			// 그리고 세이브는 base 를 저장하므로(AVBPlayerState::SavedAttributes) 범위를 벗어난 명성이
			// 그대로 디스크에 굳는다. 정리 대상으로 보이지만 지우면 안 되는 줄이다.
			const float NewReputation = FMath::Clamp(GetReputation() + ChangeValue, ReputationMin, ReputationMax);
			SetReputation(NewReputation);
		}
	}
}

void UVBReputationAttributeSet::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME_CONDITION_NOTIFY(UVBReputationAttributeSet,
		Reputation,
		COND_None,
		REPNOTIFY_Always);
}

void UVBReputationAttributeSet::OnRep_Reputation(const FGameplayAttributeData& OldReputation)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBReputationAttributeSet, Reputation, OldReputation);
}
