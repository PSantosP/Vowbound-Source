// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBLockOnMarkerWidget.generated.h"

/**
 * 
 */
UCLASS()
class VOWBOUND_API UVBLockOnMarkerWidget : public UUserWidget
{
	GENERATED_BODY()
	
public:
	void PlayFadeIn();
	void PlayFadeOut();
	void StopPulse();
	
protected:
	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> FadeIn;
	
	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> FadeOut;
	
	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> Pulse;
	
private:
	UFUNCTION()
	void OnFadeInFinished();
	UFUNCTION()
	void OnFadeOutFinished();
	
};
