// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/Notice/VBNoticeFeedWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "UI/Notice/VBNoticeSubsystem.h"
#include "Vowbound/Vowbound.h"

void UVBNoticeFeedWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Notices = UVBNoticeSubsystem::Get(this);
	bDirty = true; // 붙지 않은 동안 바뀐 줄을 한 번 다시 그린다
	if (UVBNoticeSubsystem* Subsystem = Notices.Get())
	{
		// 바뀌었다는 표시만 한다 - 다시 그리기는 틱에서 한 번. GetEntries 가 걷어 내며 울리는 알림이 그리기 도중에 오지 않게.
		if (!ChangedHandle.IsValid())
		{
			ChangedHandle = Subsystem->OnNoticesChanged.AddWeakLambda(this, [this]() { bDirty = true; });
		}
	}
	else
	{
		VB_LOG(Warning, "알림 줄 %s: 로컬 플레이어의 알림 서브시스템이 없어 아무것도 안 그린다", *GetName());
	}
}

void UVBNoticeFeedWidget::NativeDestruct()
{
	if (UVBNoticeSubsystem* Subsystem = Notices.Get())
	{
		Subsystem->OnNoticesChanged.Remove(ChangedHandle);
	}
	ChangedHandle.Reset();
	Super::NativeDestruct();
}

void UVBNoticeFeedWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UVBNoticeSubsystem* Subsystem = Notices.Get();
	if (!Subsystem || !EntryBox)
	{
		return;
	}
	const TArray<FVBNoticeEntry>& Entries = Subsystem->GetEntries();
	if (bDirty)
	{
		bDirty = false;
		Rebuild(Entries);
	}
	const double Now = Subsystem->GetNow();
	for (int32 Index = 0; Index < Entries.Num() && Index < Lines.Num(); ++Index)
	{
		const double Left = Entries[Index].ExpiresAt - Now;
		const float Opacity = FadeSeconds > 0.0f ? FMath::Clamp(static_cast<float>(Left / FadeSeconds), 0.0f, 1.0f) : 1.0f;
		Lines[Index]->SetRenderOpacity(Opacity);
	}
}

void UVBNoticeFeedWidget::Rebuild(const TArray<FVBNoticeEntry>& Entries)
{
	while (Lines.Num() < Entries.Num())
	{
		UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Line->GetFont();
		Font.Size = FontSize;
		Line->SetFont(Font);
		Line->SetJustification(LineJustification);
		Line->SetShadowOffset(ShadowOffset);
		Line->SetShadowColorAndOpacity(ShadowColor);
		EntryBox->AddChild(Line);
		Lines.Add(Line);
	}
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		UTextBlock* Line = Lines[Index];
		if (!Entries.IsValidIndex(Index))
		{
			Line->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FVBNoticeEntry& Entry = Entries[Index];
		const FSlateColor* Color = CategoryColors.Find(Entry.Category);
		Line->SetText(Entry.Text);
		Line->SetColorAndOpacity(Color ? *Color : DefaultColor);
		Line->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}
