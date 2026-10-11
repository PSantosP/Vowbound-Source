// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBInteractPromptWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

void UVBInteractPromptWidget::SetPrompt(const FText& ActionText, const FText& KeyText)
{
	if (ActionLabel)
	{
		ActionLabel->SetText(ActionText);
	}
	if (KeyLabel)
	{
		KeyLabel->SetText(KeyText);
		KeyLabel->SetVisibility(KeyText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UVBInteractPromptWidget::SetHoldProgress(float Progress)
{
	if (!HoldProgress)
	{
		return;
	}
	if (Progress < 0.0f)
	{
		HoldProgress->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	HoldProgress->SetPercent(Progress);
	HoldProgress->SetVisibility(ESlateVisibility::HitTestInvisible);
}
