// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBSaveSlotListWidget.h"

#include "SaveSystem/VBSaveGameSubsystem.h"
#include "UI/VBSaveSlotEntryWidget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Vowbound/Vowbound.h"

UVBSaveSlotListWidget::UVBSaveSlotListWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 생성자는 안전 기본값의 자리다. WBP 가 덮어쓰면 그쪽이 이긴다.
	ModeTitles.Add(EVBSlotListMode::NewGame, FText::FromString(TEXT("새 게임 - 슬롯 선택")));
	ModeTitles.Add(EVBSlotListMode::Load,    FText::FromString(TEXT("불러오기")));
	ModeTitles.Add(EVBSlotListMode::Save,    FText::FromString(TEXT("저장 - 슬롯 선택")));
}

void UVBSaveSlotListWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshTitle();
	RebuildEntries();
}

void UVBSaveSlotListWidget::SetListMode(EVBSlotListMode InMode)
{
	ListMode = InMode;
	RefreshTitle();

	// 모드가 바뀌면 목록도 다시 만든다. 화면을 스위처로 재사용하면 이 위젯이 파괴되지 않아
	// NativeConstruct 가 다시 돌지 않는다 - 모드만 바꾸고 목록을 그대로 두면 옛 화면이 남는다.
	RebuildEntries();
}

void UVBSaveSlotListWidget::RefreshTitle()
{
	if (!TxtTitle)
	{
		return;
	}

	// 문구가 비어 있으면 제목 없이 두지 않고 경고한다 - 빈 제목은 어느 모드인지 알 수 없게 만든다.
	if (const FText* Title = ModeTitles.Find(ListMode))
	{
		TxtTitle->SetText(*Title);
		return;
	}
	VB_LOG(Warning, "SaveSlotList: 모드 %d 의 제목이 ModeTitles 에 없다 - WBP 기본값 확인", static_cast<int32>(ListMode));
	TxtTitle->SetText(FText::GetEmpty());
}

void UVBSaveSlotListWidget::RebuildEntries()
{
	if (!EntryBox)
	{
		VB_LOG(Warning, "SaveSlotList: EntryBox 미바인딩 - 목록을 그릴 자리가 없다. WBP 위젯 이름 확인");
		return;
	}
	if (!EntryWidgetClass)
	{
		VB_LOG(Warning, "SaveSlotList: EntryWidgetClass 미지정 - 항목을 만들 수 없다. WBP 기본값 확인");
		return;
	}

	EntryBox->ClearChildren();

	for (const FVBSaveSlotMeta& Meta : RefreshSlots())
	{
		UVBSaveSlotEntryWidget* Entry = CreateWidget<UVBSaveSlotEntryWidget>(this, EntryWidgetClass);
		if (!Entry)
		{
			continue;
		}
		// 구독을 먼저 건다. SetupFromMeta 가 표시를 갱신하며 BP 훅까지 부르므로,
		// 그 안에서 곧바로 요청이 나가는 구성이라도 놓치지 않는다.
		Entry->OnSelectRequested.AddDynamic(this, &UVBSaveSlotListWidget::HandleEntrySelect);
		Entry->OnDeleteRequested.AddDynamic(this, &UVBSaveSlotListWidget::HandleEntryDelete);
		Entry->SetupFromMeta(Meta);

		// 간격은 슬롯에 걸린다. 세로 목록이 아니면 걸 자리가 없으므로 조용히 넘어가지 않고 알린다 -
		// 컨테이너를 바꾼 사람이 "왜 간격이 안 먹지"로 헤매는 것이 이 경고 한 줄보다 비싸다.
		UPanelSlot* AddedSlot = EntryBox->AddChild(Entry);
		if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(AddedSlot))
		{
			BoxSlot->SetPadding(EntrySpacing);
		}
		else if (!bEntrySpacingWarned)
		{
			bEntrySpacingWarned = true;
			VB_LOG(Warning, "SaveSlotList: EntryBox 가 VerticalBox 가 아니라 EntrySpacing 이 적용되지 않는다");
		}
	}
}

void UVBSaveSlotListWidget::HandleEntrySelect(int32 SlotIndex)
{
	SelectSlot(SlotIndex);
}

void UVBSaveSlotListWidget::HandleEntryDelete(int32 SlotIndex)
{
	DeleteSlot(SlotIndex);
}

TArray<FVBSaveSlotMeta> UVBSaveSlotListWidget::RefreshSlots() const
{
	if (UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this))
	{
		return Subsystem->GetAllSlotMetadata();
	}
	return {};
}

void UVBSaveSlotListWidget::SelectSlot(int32 SlotIndex)
{
	UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}

	switch (ListMode)
	{
	case EVBSlotListMode::NewGame:
		// 새 게임은 활성 세이브를 즉시 메모리에 만들므로 로드 대기 없이 레벨을 연다.
		// (기존 슬롯 덮어쓰기 확인은 BP 다이얼로그 책임 — C++ 는 실행만.)
		// 슬롯 지정 진입점: 사용자가 이 슬롯을 직접 골랐으므로 즉시 점유한다.
		// (메인메뉴의 New Game 은 슬롯을 잡지 않는 별도 경로다 — StartNewGame())
		Subsystem->StartNewGameInSlot(SlotIndex);
		Subsystem->TravelToGameplayLevel();
		return;

	case EVBSlotListMode::Save:
		// 저장은 레벨을 열지 않는다 - 이미 플레이 중이고 메뉴만 닫으면 된다.
		// 구독을 저장 성공 뒤에 거는 이유: 폰 null/범위 밖이면 완료 통지가 영영 오지 않아 구독이 남는다.
		// 비동기 완료 콜백은 빨라야 다음 틱이므로, 이 사이에 끼어들어 놓치는 일은 없다.
		if (Subsystem->SaveCurrentGameToSlot(GetOwningPlayerPawn(), SlotIndex))
		{
			Subsystem->OnGameSaveCompleted.AddDynamic(this, &UVBSaveSlotListWidget::HandleSaveCompleted);
		}
		return;

	case EVBSlotListMode::Load:
		// 빈 슬롯은 무시. 로드 완료 후 레벨 오픈.
		if (!Subsystem->DoesGameSaveExist(SlotIndex))
		{
			return;
		}
		Subsystem->OnGameLoadCompleted.AddDynamic(this, &UVBSaveSlotListWidget::HandleLoadCompleted);
		Subsystem->LoadGameSlot(SlotIndex);
		return;
	}
}

void UVBSaveSlotListWidget::DeleteSlot(int32 SlotIndex)
{
	if (UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this))
	{
		Subsystem->DeleteGameSave(SlotIndex);
		RebuildEntries();  // 지운 결과를 즉시 반영
		OnSlotsChanged();  // BP 가 덧붙일 갱신이 있으면
	}
}

void UVBSaveSlotListWidget::HandleSaveCompleted(int32 SlotIndex, bool bSuccess)
{
	if (UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this))
	{
		Subsystem->OnGameSaveCompleted.RemoveDynamic(this, &UVBSaveSlotListWidget::HandleSaveCompleted);
	}

	if (!bSuccess)
	{
		VB_LOG(Warning, "SaveSlotList: 슬롯 %d 저장 실패 - 목록을 갱신하지 않는다", SlotIndex);
		return;
	}

	// 방금 쓴 타임스탬프/존재 여부를 목록에 반영한다. 갱신이 없으면 저장이 됐는지 화면으로 알 수 없다.
	RebuildEntries();
	OnSlotsChanged();
}

void UVBSaveSlotListWidget::HandleLoadCompleted(int32 SlotIndex, bool bSuccess)
{
	UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this);
	if (Subsystem)
	{
		Subsystem->OnGameLoadCompleted.RemoveDynamic(this, &UVBSaveSlotListWidget::HandleLoadCompleted);
	}

	if (bSuccess && Subsystem)
	{
		Subsystem->TravelToGameplayLevel();
	}
}
