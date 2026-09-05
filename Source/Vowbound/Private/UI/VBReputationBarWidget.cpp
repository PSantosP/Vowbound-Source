// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBReputationBarWidget.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

void UVBReputationBarWidget::InitializeWithASC(UAbilitySystemComponent* ASC)
{
	if (ASC == nullptr)
	{
		return;
	}

	CurrentReputation = ASC->GetNumericAttribute(UVBReputationAttributeSet::GetReputationAttribute());

	// 값 변화 구독. UVBHealthBarWidget::InitializeWithASC 와 같은 관용구다.
	ASC->GetGameplayAttributeValueChangeDelegate(
	                                             UVBReputationAttributeSet::GetReputationAttribute()
	                                            ).AddUObject(this, &UVBReputationBarWidget::OnReputationChanged);

	UpdateBar();
}

void UVBReputationBarWidget::OnReputationChanged(const FOnAttributeChangeData& Data)
{
	CurrentReputation = Data.NewValue;
	UpdateBar();
}

void UVBReputationBarWidget::UpdateBar()
{
	// 명성 축은 -100 ~ +100 이라 0~1 로 옮겨야 바에 그릴 수 있다.
	// 끝점은 UVBReputationAttributeSet 이 소유한다 - 여기에 숫자를 다시 적으면 축의 홈이 둘이 되고,
	//   한쪽만 바뀌는 날 바가 아무 경고 없이 틀린 비율을 그린다.
	const float Min  = UVBReputationAttributeSet::ReputationMin;
	const float Max  = UVBReputationAttributeSet::ReputationMax;
	const float Span = FMath::Max(Max - Min, KINDA_SMALL_NUMBER);

	if (ReputationBar)
	{
		ReputationBar->SetPercent(FMath::Clamp((CurrentReputation - Min) / Span, 0.0f, 1.0f));
	}

	if (ReputationText)
	{
		// 부호를 붙인다. 0 을 기준으로 의미가 갈리는 축이라 "20" 과 "-20" 이 반대 방향이고,
		//   부호가 없으면 어느 쪽인지 화면만 보고는 알 수 없다.
		ReputationText->SetText(FText::FromString(
			FString::Printf(TEXT("%+d"), FMath::RoundToInt(CurrentReputation))));
	}
}
