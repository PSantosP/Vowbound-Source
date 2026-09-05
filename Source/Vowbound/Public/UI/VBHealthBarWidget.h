// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "VBHealthBarWidget.generated.h"

class UAbilitySystemComponent;
struct FOnAttributeChangeData;

/**
 * 
 */
UCLASS()
class VOWBOUND_API UVBHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	// ASC에서 속성 변화를 받아 바를 갱신하는 함수
	void InitializeWithASC(UAbilitySystemComponent* ASC);
	
protected:
	// BindWidget: BP Designer에서 동일 이름의 위젯과 자동 연결
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> HealthBar;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> HealthBarDelay;
	
	FTimerHandle DelayBarTimerHandle;
	float DelayBarTarget = 1.0f;
	bool bIsDelayBarActive = false;
	
	void StartDelayBar();
	void TickDelayBar();
	
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> ShieldBar;
	
	// 숫자 텍스트
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;
	
	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> DamagePunch;
	
	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> LowHealthPulse;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	float LowHealthThreshold = 0.3f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	float DelayBarUpdateInterval = 0.016f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	float DelayBarInterpSpeed = 5.0f;

	// 딜레이 바 틱 시작 지연(초) — 피격 직후 이 시간 뒤부터 잔상 바가 따라붙기 시작. 현행 0.5f 보존.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	float DelayBarStartDelay = 0.5f;
	
	bool bIsLowHealth = false;
private:
	// 속성 변화 콜백
	void OnHealthChanged(const FOnAttributeChangeData& Data);
	void OnMaxHealthChanged(const FOnAttributeChangeData& Data);
	void OnShieldChanged(const FOnAttributeChangeData& Data);
	void OnMaxShieldChanged(const FOnAttributeChangeData& Data);
	
	// 현재 값 캐싱 (비율 계산용)
	float CurrentHealth = 0.0f;
	float CurrentMaxHealth = 1.0f;
	float CurrentShield = 0.0f;
	float CurrentMaxShield = 1.0f;
	
	void UpdateHealthBar();
	void UpdateShieldBar();
};
