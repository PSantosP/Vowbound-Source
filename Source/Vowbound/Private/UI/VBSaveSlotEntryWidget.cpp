// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBSaveSlotEntryWidget.h"

#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Vowbound/Vowbound.h"

void UVBSaveSlotEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// AddDynamic 은 중복 등록을 막지 않는다. 위젯이 재구성되면 NativeConstruct 가 다시 도는데,
	// 그때 두 번 붙으면 한 번 눌러도 요청이 두 번 나간다(삭제가 두 번이면 다음 슬롯까지 지운다).
	if (BtnSelect)
	{
		BtnSelect->OnClicked.RemoveDynamic(this, &UVBSaveSlotEntryWidget::HandleSelectClicked);
		BtnSelect->OnClicked.AddDynamic(this, &UVBSaveSlotEntryWidget::HandleSelectClicked);
	}
	if (BtnDelete)
	{
		BtnDelete->OnClicked.RemoveDynamic(this, &UVBSaveSlotEntryWidget::HandleDeleteClicked);
		BtnDelete->OnClicked.AddDynamic(this, &UVBSaveSlotEntryWidget::HandleDeleteClicked);
	}

	// 생성 시점에 이미 메타를 받아 둔 경우가 있다(목록이 SetupFromMeta 를 먼저 부른다).
	RefreshDisplay();
}

void UVBSaveSlotEntryWidget::SetupFromMeta(const FVBSaveSlotMeta& InMeta)
{
	Meta = InMeta;
	RefreshDisplay();
	OnMetaUpdated(Meta); // BP 가 덧붙일 표시가 있으면 여기서
}

void UVBSaveSlotEntryWidget::RefreshDisplay()
{
	if (TxtSlotName)
	{
		// 빈 슬롯도 한 줄을 차지한다 - 목록에서 자리가 비어 보이면 몇 번 슬롯인지 알 수 없다.
		const FString Name = Meta.bExists
			? FString::Printf(TEXT("%d. %s"), Meta.SlotIndex, *Meta.DisplayName)
			: FString::Printf(TEXT("%d. 비어 있음"), Meta.SlotIndex);
		TxtSlotName->SetText(FText::FromString(Name));
	}

	if (TxtTimestamp)
	{
		TxtTimestamp->SetText(Meta.bExists
			? FText::FromString(Meta.Timestamp.ToString(TEXT("%Y-%m-%d %H:%M")))
			: FText::GetEmpty());
	}

	if (TxtPlayTime)
	{
		TxtPlayTime->SetText(Meta.bExists
			? FText::FromString(FormatPlayTime(Meta.PlayTimeSeconds))
			: FText::GetEmpty());
	}

	// 빈 슬롯에는 지울 것이 없다. 버튼을 남겨 두면 눌리고도 아무 일이 없어 고장으로 읽힌다.
	if (BtnDelete)
	{
		BtnDelete->SetVisibility(Meta.bExists ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}
}

void UVBSaveSlotEntryWidget::HandleSelectClicked()
{
	OnSelectRequested.Broadcast(Meta.SlotIndex);
}

void UVBSaveSlotEntryWidget::HandleDeleteClicked()
{
	OnDeleteRequested.Broadcast(Meta.SlotIndex);
}

FString UVBSaveSlotEntryWidget::FormatPlayTime(double Seconds)
{
	// 음수 방지 후 시/분/초로 분해.
	const int32 Total = FMath::Max(0, FMath::FloorToInt(Seconds));
	const int32 Hours = Total / 3600;
	const int32 Minutes = (Total % 3600) / 60;
	const int32 Secs = Total % 60;
	return FString::Printf(TEXT("%02d:%02d:%02d"), Hours, Minutes, Secs);
}
