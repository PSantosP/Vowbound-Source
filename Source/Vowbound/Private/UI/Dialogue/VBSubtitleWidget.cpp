// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/Dialogue/VBSubtitleWidget.h"

#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Narrative/Dialogue/VBSpeakerDefinition.h"
#include "Narrative/Dialogue/VBSpokenLineComponent.h"
#include "TimerManager.h"

void UVBSubtitleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UVBSubtitleWidget::NativeDestruct()
{
	BindTo(nullptr);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimer);
	}
	Super::NativeDestruct();
}

void UVBSubtitleWidget::BindTo(UVBSpokenLineComponent* InLines)
{
	if (UVBSpokenLineComponent* Old = BoundLines.Get())
	{
		Old->OnLineStarted.Remove(LineHandle);
	}
	LineHandle.Reset();
	BoundLines = InLines;
	if (InLines)
	{
		LineHandle = InLines->OnLineStarted.AddUObject(this, &UVBSubtitleWidget::HandleLine);
	}
}

void UVBSubtitleWidget::HandleLine(const FVBSpokenLine& Line, float Seconds)
{
	TxtLine->SetText(Line.Text);
	if (TxtSpeaker)
	{
		const bool bHasSpeaker = Line.Speaker && !Line.Speaker->DisplayName.IsEmpty();
		TxtSpeaker->SetText(bHasSpeaker ? Line.Speaker->DisplayName : FText::GetEmpty());
		TxtSpeaker->SetVisibility(bHasSpeaker ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// 새 줄은 지난 줄의 타이머를 덮는다 - 짧은 줄 뒤에 긴 줄이 오면 긴 줄이 끝까지 보인다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HideTimer, this, &UVBSubtitleWidget::HideLine, FMath::Max(Seconds, 0.1f), false);
	}
}

void UVBSubtitleWidget::HideLine()
{
	SetVisibility(ESlateVisibility::Collapsed);
}
