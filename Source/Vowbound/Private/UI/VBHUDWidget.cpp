// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "UI/VBHUDWidget.h"

#include "Character/VBCharacter.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Character/VBTargetLockComponent.h"

void UVBHUDWidget::InitializeHUD(AVBCharacter* Owner)
{
	// Owner 캐싱
	OwnerCharacter = Owner;

	// ASC 가져와서 HealthBar 초기화
	// 굳이 Initialize를 한 이유는 ASC가 초기화가 준비된 이후에 호출하는게 안전하기 때문
	// BeginPlay보다 안전
	if (UAbilitySystemComponent* ASC = OwnerCharacter->GetAbilitySystemComponent(); ASC && PlayerHealthBar)
	{
		PlayerHealthBar->InitializeWithASC(ASC);
	}
	

	// Lock-On 델리게이트 구독
	if (UVBTargetLockComponent* LockComp = OwnerCharacter->GetTargetLockComponent())
	{
		LockComp->OnTargetLocked.AddDynamic(this, &UVBHUDWidget::OnLockOnTargetChanged);
		LockComp->OnTargetUnlocked.AddDynamic(this, &UVBHUDWidget::OnLockOnTargetCleared);
	}
	
	// MarkerSlot 캐시 + Alignment 설정
	if (LockOnMarker)
	{
		MarkerSlot = Cast<UCanvasPanelSlot>(LockOnMarker->Slot);
		if (MarkerSlot)
		{
			MarkerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		}
		
		LockOnMarker->SetVisibility(ESlateVisibility::Collapsed);
	}
	
	if (HUDFadeIn)
		PlayAnimation(HUDFadeIn);
}

void UVBHUDWidget::OnLockOnTargetChanged(AActor* Target)
{
	LockedTarget = Target;
}

void UVBHUDWidget::OnLockOnTargetCleared(AActor* Target)
{
	LockedTarget = nullptr;
	if (LockOnMarker)
	{
		LockOnMarker->PlayFadeOut();
	}
}

void UVBHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (LockedTarget.IsValid())
		UpdateLockOnMarkerPosition();
}

void UVBHUDWidget::UpdateLockOnMarkerPosition()
{
	if (!MarkerSlot || !OwnerCharacter.IsValid()) return;
	
	APlayerController* PC = GetOwningPlayer();
	
	if (!PC) return;
	
	FVector TargetLocation = LockedTarget->GetActorLocation() + FVector(0.0f, 0.0f, LockOnMarkerZOffset);
	FVector2D ScreenPosition;
	bool bOnScreen = PC->ProjectWorldLocationToScreen(TargetLocation, ScreenPosition);
	
	if (!bOnScreen)
	{
		LockOnMarker->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	
	if (LockOnMarker->GetVisibility() == ESlateVisibility::Collapsed)
	{
		LockOnMarker->SetVisibility(ESlateVisibility::HitTestInvisible);
		LockOnMarker->PlayFadeIn();
	}

		
	float Scale = UWidgetLayoutLibrary::GetViewportScale(PC);
	ScreenPosition /= Scale;
	
	MarkerSlot->SetPosition(ScreenPosition);
}
