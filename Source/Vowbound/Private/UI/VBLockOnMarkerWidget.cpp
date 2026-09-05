// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "UI/VBLockOnMarkerWidget.h"

void UVBLockOnMarkerWidget::PlayFadeIn()
{
	if (FadeIn)
	{
		FWidgetAnimationDynamicEvent Delegate;
		Delegate.BindDynamic(this, &UVBLockOnMarkerWidget::OnFadeInFinished);
		BindToAnimationFinished(FadeIn, Delegate);
		PlayAnimation(FadeIn);
	}
}

void UVBLockOnMarkerWidget::PlayFadeOut()
{
	StopPulse();
	if (FadeOut)
	{
		FWidgetAnimationDynamicEvent Delegate;
		Delegate.BindDynamic(this, &UVBLockOnMarkerWidget::OnFadeOutFinished);
		BindToAnimationFinished(FadeOut, Delegate);
		PlayAnimation(FadeOut);
	}
}

void UVBLockOnMarkerWidget::StopPulse()
{
	if (Pulse) 
		StopAnimation(Pulse);
}

void UVBLockOnMarkerWidget::OnFadeInFinished()
{
	if (Pulse)
	{
		PlayAnimation(Pulse, 0.0f, 0);
	}
	UnbindAllFromAnimationFinished(FadeOut);
}

void UVBLockOnMarkerWidget::OnFadeOutFinished()
{
	SetVisibility(ESlateVisibility::Collapsed);
	UnbindAllFromAnimationFinished(FadeOut);
}
