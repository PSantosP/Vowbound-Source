// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBHealthBarWidget.h"
#include "VBLockOnMarkerWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "VBHUDWidget.generated.h"

class AVBCharacter;

/**
 * 
 */
UCLASS()
class VOWBOUND_API UVBHUDWidget : public UUserWidget
{
	GENERATED_BODY()
	
public:
	// 초기화 - PlayerController에서 호출
	void InitializeHUD(AVBCharacter* Owner);
	
	// Lock-On 연동
	UFUNCTION()
	void OnLockOnTargetChanged(AActor* Target);
	UFUNCTION()
	void OnLockOnTargetCleared(AActor* Target);
	
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	
	
	
protected:
	
	// HUD 안에 HealthBar 위젯을 자식으로 배치한다.
	// BP에서는 PlayerHealthBar 이름의 VBHealthBarWidget을 추가한다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UVBHealthBarWidget> PlayerHealthBar;
	
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UVBLockOnMarkerWidget> LockOnMarker;

	// 락온 마커 월드 Z 오프셋(타겟 머리 위 표시 보정). 과거 cpp 30.0f 하드코딩 외부화 — 위젯 BP에서 조정.
	UPROPERTY(EditAnywhere, Category="Vowbound|HUD")
	float LockOnMarkerZOffset = 30.0f;

private:
	TWeakObjectPtr<AVBCharacter> OwnerCharacter;
	TWeakObjectPtr<AActor> LockedTarget;
	
	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> HUDFadeIn;
	
	UPROPERTY()
	UCanvasPanelSlot* MarkerSlot = nullptr;
	
	void UpdateLockOnMarkerPosition();
};
