// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBMenuScreenWidget.h"

#include "UI/VBSettingsWidget.h"
#include "Components/WidgetSwitcher.h"
#include "Vowbound/Vowbound.h"

bool UVBMenuScreenWidget::SwitchToPage(int32 PageIndex)
{
	if (!MenuSwitcher)
	{
		VB_LOG(Warning, "%s: MenuSwitcher 미바인딩 - 화면 전환 불가. WBP 위젯 이름 확인", *GetName());
		return false;
	}
	MenuSwitcher->SetActiveWidgetIndex(PageIndex);
	return true;
}

void UVBMenuScreenWidget::ShowSettingsPage()
{
	if (!SwitchToPage(PageIndex_Settings))
	{
		return;
	}

	// 진입할 때마다 채운다. 스위처는 페이지를 파괴하지 않으므로 두 번째 진입부터는
	// 설정 위젯의 Construct 가 다시 불리지 않는다 - 그것만 믿으면 두 번째부터 옛 값이 남는다.
	if (SettingsView)
	{
		SettingsView->SyncWidgetsFromSettings();
	}
	else
	{
		VB_LOG(Warning, "%s: SettingsView 미바인딩 - 설정 화면이 빈 값으로 열린다", *GetName());
	}
}

void UVBMenuScreenWidget::ShowSlotPage(EVBSlotListMode InMode)
{
	if (!SwitchToPage(PageIndex_Slots))
	{
		return;
	}

	if (SlotListView)
	{
		// SetListMode 안에서 목록을 다시 만든다. 저장/삭제로 슬롯이 바뀌었을 수 있으므로
		// 재진입 때마다 새로 그려야 한다 - 스위처는 페이지를 파괴하지 않는다.
		SlotListView->SetListMode(InMode);
	}
	else
	{
		VB_LOG(Warning, "%s: SlotListView 미바인딩 - 슬롯 화면이 빈 채로 열린다", *GetName());
	}
}

void UVBMenuScreenWidget::ShowMainPage()
{
	SwitchToPage(PageIndex_Main);
}
