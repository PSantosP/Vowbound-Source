// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "UI/VBHealthBarWidget.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"

void UVBHealthBarWidget::InitializeWithASC(UAbilitySystemComponent* ASC)
{
	if (ASC == nullptr) return;

	// 현재 값 초기화
	CurrentHealth = ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute());
	CurrentMaxHealth = ASC->GetNumericAttribute(UVBHealthAttributeSet::GetMaxHealthAttribute());
	CurrentShield = ASC->GetNumericAttribute(UVBHealthAttributeSet::GetShieldAttribute());
	CurrentMaxShield = ASC->GetNumericAttribute(UVBHealthAttributeSet::GetMaxShieldAttribute());
	
	// 각 속성의 ValueChangeDelegate 구독
	ASC->GetGameplayAttributeValueChangeDelegate(
	                                             UVBHealthAttributeSet::GetHealthAttribute()
	                                            ).AddUObject(this, &UVBHealthBarWidget::OnHealthChanged);

	ASC->GetGameplayAttributeValueChangeDelegate(
	                                             UVBHealthAttributeSet::GetMaxHealthAttribute()
	                                            ).AddUObject(this, &UVBHealthBarWidget::OnMaxHealthChanged);

	ASC->GetGameplayAttributeValueChangeDelegate(
	                                             UVBHealthAttributeSet::GetShieldAttribute()
	                                            ).AddUObject(this, &UVBHealthBarWidget::OnShieldChanged);

	ASC->GetGameplayAttributeValueChangeDelegate(
	                                             UVBHealthAttributeSet::GetMaxShieldAttribute()
	                                            ).AddUObject(this, &UVBHealthBarWidget::OnMaxShieldChanged);
	
	// 초기 바 업데이트
	UpdateHealthBar();
	UpdateShieldBar();
	HealthBarDelay->SetPercent(CurrentHealth / FMath::Max(CurrentMaxHealth, 1.0f));
}

void UVBHealthBarWidget::StartDelayBar()
{
	bIsDelayBarActive = false;
	GetWorld()->GetTimerManager().SetTimer(
		DelayBarTimerHandle, this, &UVBHealthBarWidget::TickDelayBar,
		DelayBarUpdateInterval,
		true,
		DelayBarStartDelay);
}

void UVBHealthBarWidget::TickDelayBar()
{
	float HealthPercent = CurrentHealth / FMath::Max(CurrentMaxHealth, 1.0f);
	float CurrentDelayPercent = HealthBarDelay->GetPercent();
	
	// 딜레이 바를 HP 바 쪽으로 천천히 보간
	float NewPercent = FMath::FInterpTo(CurrentDelayPercent, HealthPercent, DelayBarUpdateInterval, DelayBarInterpSpeed);
	HealthBarDelay->SetPercent(NewPercent);
	
	// 거의 같아지면 정지
	if (FMath::IsNearlyEqual(NewPercent, HealthPercent, 0.01f))
	{
		HealthBarDelay->SetPercent(HealthPercent);
		GetWorld()->GetTimerManager().ClearTimer(DelayBarTimerHandle);
	}
}

void UVBHealthBarWidget::OnHealthChanged(const FOnAttributeChangeData& Data)
{
	CurrentHealth = Data.NewValue;
	UpdateHealthBar();
	StartDelayBar();
	if (DamagePunch && Data.NewValue < Data.OldValue) PlayAnimation(DamagePunch);
}

void UVBHealthBarWidget::OnMaxHealthChanged(const FOnAttributeChangeData& Data)
{
	CurrentMaxHealth = Data.NewValue;
	UpdateHealthBar();
}

void UVBHealthBarWidget::OnShieldChanged(const FOnAttributeChangeData& Data)
{
	CurrentShield = Data.NewValue;
	UpdateShieldBar();
}

void UVBHealthBarWidget::OnMaxShieldChanged(const FOnAttributeChangeData& Data)
{
	CurrentMaxShield = Data.NewValue;
	UpdateShieldBar();
}

void UVBHealthBarWidget::UpdateHealthBar()
{
	float Percent = CurrentHealth / FMath::Max(CurrentMaxHealth, 1.0f);
	HealthBar->SetPercent(Percent);
	
	// 저체력 맥동 체크
	if (Percent <= LowHealthThreshold && !bIsLowHealth)
	{
		bIsLowHealth = true;
		if (LowHealthPulse)
			PlayAnimation(LowHealthPulse, 0.0f, 0);
	}
	else if (Percent > LowHealthThreshold && bIsLowHealth)
	{
		bIsLowHealth = false;
		if (LowHealthPulse)
			StopAnimation(LowHealthPulse);
	}
	
	if (HealthText)
		HealthText->SetText(FText::AsNumber(static_cast<int32>(CurrentHealth)));
}

void UVBHealthBarWidget::UpdateShieldBar()
{
	float Percent = CurrentShield / FMath::Max(CurrentMaxShield, 1.0f);
	ShieldBar->SetPercent(Percent);
}
