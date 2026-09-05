// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBSettingsWidget.h"

#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Vowbound/Vowbound.h"

FVBSettingsSaveData UVBSettingsWidget::GetCurrentSettings() const
{
	// 읽기 홈은 서브시스템 하나다. 여기서 다시 조회 사슬을 쓰면 폴백이 두 벌이 된다.
	// BP 노출용이라 값 복사 - 위젯이 슬라이더에 담아 편집하고 ApplyAndSave 로 되돌려준다.
	return UVBSaveGameSubsystem::GetSettingsData(this);
}

void UVBSettingsWidget::ApplyAndSave(const FVBSettingsSaveData& NewSettings)
{
	if (UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this))
	{
		Subsystem->UpdateSettings(NewSettings);
	}
}

void UVBSettingsWidget::SyncWidgetsFromSettings()
{
	const FVBSettingsSaveData Current = GetCurrentSettings();

	// 바인딩이 끊기면 조용히 아무 일도 일어나지 않는 대신 알린다 - 화면은 떠 있는데 값이 안 채워지는
	// 증상은 원인이 안 보여서 UI 버그로 오진하기 쉽다.
	if (SliderSens)
	{
		SliderSens->SetValue(Current.LookSensitivity);
	}
	else
	{
		VB_LOG(Warning, "SettingsWidget: SliderSens 미바인딩 - WBP 위젯 이름이 바뀌었는지 확인");
	}

	if (ChkInvertY)
	{
		ChkInvertY->SetIsChecked(Current.bInvertYAxis);
	}
	else
	{
		VB_LOG(Warning, "SettingsWidget: ChkInvertY 미바인딩 - WBP 위젯 이름이 바뀌었는지 확인");
	}
}

void UVBSettingsWidget::ApplyFromWidgets()
{
	// 바인딩이 없으면 저장된 값을 그대로 되쓴다. 0 이나 false 로 덮어써서 사용자 설정을 날리지 않는다.
	const FVBSettingsSaveData Current = GetCurrentSettings();
	const float NewSensitivity = SliderSens ? SliderSens->GetValue()   : Current.LookSensitivity;
	const bool  bNewInvertY    = ChkInvertY ? ChkInvertY->IsChecked() : Current.bInvertYAxis;

	ApplyLookSettings(NewSensitivity, bNewInvertY);
}

void UVBSettingsWidget::ApplyLookSettings(float InLookSensitivity, bool bInInvertYAxis)
{
	// 현재 값을 먼저 읽고 두 항목만 덮는다. 새 구조체를 만들지 않는 것이 요점이다 -
	// 만들면 볼륨 3종과 언어가 기본값으로 초기화돼 사용자가 저장해 둔 값이 사라진다.
	FVBSettingsSaveData Settings = GetCurrentSettings();
	Settings.LookSensitivity = InLookSensitivity;
	Settings.bInvertYAxis    = bInInvertYAxis;
	ApplyAndSave(Settings);
}
