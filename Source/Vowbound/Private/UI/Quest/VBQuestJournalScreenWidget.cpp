// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/Quest/VBQuestJournalScreenWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Input/CommonUIInputTypes.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Quest/VBQuestText.h"
#include "UI/VBListRowWidget.h"
#include "Vowbound/Vowbound.h"

#define LOCTEXT_NAMESPACE "VBQuestJournal"

void UVBQuestJournalScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (CloseInputAction)
	{
		// 메뉴 화면이라 바인딩 기본 모드(Menu)가 맞다 - HUD 의 여는 바인딩만 Game 이다.
		RegisterUIActionBinding(FBindUIActionArgs(CloseInputAction, /*bShouldDisplayInActionBar=*/false,
		                                          FSimpleDelegate::CreateUObject(this, &UVBQuestJournalScreenWidget::HandleCloseInput)));
	}
}

void UVBQuestJournalScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	QuestLog = UVBQuestLogComponent::FindFor(GetOwningPlayer());
	if (UVBQuestLogComponent* Log = QuestLog.Get())
	{
		QuestLogChangedHandle = Log->OnQuestLogChanged.AddUObject(this, &UVBQuestJournalScreenWidget::Rebuild);
	}
	else
	{
		VB_LOG(Warning, "퀘스트 일지 화면: 이 플레이어의 퀘스트 일지를 찾지 못했다(PlayerState 에 UVBQuestLogComponent)");
	}
	Selected.Reset();
	Rebuild();
}

void UVBQuestJournalScreenWidget::NativeOnDeactivated()
{
	if (UVBQuestLogComponent* Log = QuestLog.Get())
	{
		Log->OnQuestLogChanged.Remove(QuestLogChangedHandle);
	}
	QuestLogChangedHandle.Reset();
	Super::NativeOnDeactivated();
}

UWidget* UVBQuestJournalScreenWidget::NativeGetDesiredFocusTarget() const
{
	// 게임패드로 열었을 때 첫 줄의 버튼에 초점. 목록이 비면 부모 규칙.
	if (EntryBox && EntryBox->GetChildrenCount() > 0)
	{
		if (const UVBListRowWidget* Row = Cast<UVBListRowWidget>(EntryBox->GetChildAt(0)))
		{
			return Row->GetButton();
		}
	}
	return Super::NativeGetDesiredFocusTarget();
}

void UVBQuestJournalScreenWidget::Rebuild()
{
	RowQuests.Reset();
	if (EntryBox)
	{
		EntryBox->ClearChildren();
	}
	const UVBQuestLogComponent* Log = QuestLog.Get();
	// 진행 중을 위에(최근 것부터), 끝난 것을 아래에.
	TArray<const FVBQuestLogEntry*> Rows;
	if (Log)
	{
		for (const FVBQuestLogEntry& Entry : Log->GetEntries())
		{
			if (Entry.Definition)
			{
				Rows.Add(&Entry);
			}
		}
	}
	Rows.Sort([](const FVBQuestLogEntry& A, const FVBQuestLogEntry& B)
	{
		const bool bActiveA = A.Status == EVBQuestStatus::Active;
		const bool bActiveB = B.Status == EVBQuestStatus::Active;
		return bActiveA != bActiveB ? bActiveA : A.RecentOrder > B.RecentOrder;
	});
	if (!EntryWidgetClass && !Rows.IsEmpty())
	{
		VB_LOG(Warning, "퀘스트 일지 화면: EntryWidgetClass 미지정 - 목록을 그릴 수 없다(WBP 클래스 기본값)");
	}
	bool bSelectedStillListed = false;
	for (const FVBQuestLogEntry* Entry : Rows)
	{
		const int32 RowIndex = RowQuests.Add(Entry->Definition.Get());
		bSelectedStillListed |= Entry->Definition == Selected.Get();
		if (!EntryBox || !EntryWidgetClass)
		{
			continue;
		}
		UVBListRowWidget* Row = CreateWidget<UVBListRowWidget>(this, EntryWidgetClass);
		const FText Detail = Entry->Status == EVBQuestStatus::Completed ? CompletedDetail : FText::GetEmpty();
		Row->Setup(RowIndex, Entry->Definition->Title, Detail, /*bEnabled=*/true);
		Row->OnRowClicked.BindUObject(this, &UVBQuestJournalScreenWidget::Select);
		EntryBox->AddChild(Row);
	}
	if (TxtEmpty)
	{
		TxtEmpty->SetVisibility(Rows.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!bSelectedStillListed)
	{
		Selected = RowQuests.IsEmpty() ? nullptr : RowQuests[0].Get();
	}
	RefreshDetail();
}

void UVBQuestJournalScreenWidget::Select(int32 RowIndex)
{
	if (RowQuests.IsValidIndex(RowIndex))
	{
		Selected = RowQuests[RowIndex];
		RefreshDetail();
	}
}

void UVBQuestJournalScreenWidget::RefreshDetail()
{
	const UVBQuestDefinition* Quest = Selected.Get();
	const UVBQuestLogComponent* Log = QuestLog.Get();
	const FVBQuestLogEntry* Entry = nullptr;
	if (Log && Quest)
	{
		Entry = Log->GetEntries().FindByPredicate([Quest](const FVBQuestLogEntry& E) { return E.Definition == Quest; });
	}
	if (TxtQuestTitle)
	{
		TxtQuestTitle->SetText(Quest ? Quest->Title : FText::GetEmpty());
	}
	if (!TxtQuestDetail)
	{
		return;
	}
	if (!Quest || !Entry)
	{
		TxtQuestDetail->SetText(FText::GetEmpty());
		return;
	}
	// 요약 / 지난 단계(완료) / 지금 단계와 목표. 끝난 퀘스트는 단계 전부를 지난 것으로 적는다.
	TArray<FText> Lines;
	Lines.Add(Quest->Summary);
	const int32 DoneStages = Entry->Status == EVBQuestStatus::Completed ? Quest->Stages.Num() : Entry->StageIndex;
	for (int32 StageIndex = 0; StageIndex < DoneStages && Quest->Stages.IsValidIndex(StageIndex); ++StageIndex)
	{
		Lines.Add(FText::Format(LOCTEXT("DoneStage", "{0} (완료)"), Quest->Stages[StageIndex].Description));
	}
	if (Entry->Status == EVBQuestStatus::Active && Quest->Stages.IsValidIndex(Entry->StageIndex))
	{
		Lines.Add(Quest->Stages[Entry->StageIndex].Description);
		Lines.Add(VBQuestText::DescribeObjectives(*Entry));
	}
	TxtQuestDetail->SetText(FText::Join(FText::FromString(TEXT("\n\n")), Lines));
}

void UVBQuestJournalScreenWidget::HandleCloseInput()
{
	DeactivateWidget();
}

#undef LOCTEXT_NAMESPACE
