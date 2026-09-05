// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/VBSaveSlotListWidget.h"
#include "VBMenuScreenWidget.generated.h"

/**
 * 스위처로 하위 화면을 갈아 끼우는 메뉴의 공통 토대.
 * 이유: 메인 메뉴와 일시정지 메뉴가 "설정 화면을 품고 전환한다"는 동일한 일을 한다.
 *       두 벌로 두면 한쪽만 고쳐지는 순간 갈라지므로(진입 시 값 채우기 같은 규칙이 특히 그렇다)
 *       전환 규약을 여기 하나에 둔다. 각 메뉴는 자기만의 페이지를 2번부터 덧붙인다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBMenuScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 설정 화면으로 전환. 전환할 때마다 저장된 값을 다시 채운다 -
	// 스위처는 페이지를 파괴하지 않으므로 두 번째 진입부터 하위 위젯의 Construct 가 불리지 않는다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ShowSettingsPage();

	// 슬롯 화면으로 전환하며 용도를 지정한다. 모드 지정이 곧 목록 재구성이라
	// 저장/삭제로 슬롯이 바뀐 뒤 다시 들어와도 옛 목록이 남지 않는다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ShowSlotPage(EVBSlotListMode InMode);

	// 메뉴 첫 화면으로 복귀. 하위 화면의 Back 버튼이 부른다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ShowMainPage();

protected:
	// 페이지 인덱스 규약: 0=첫 화면, 1=설정, 2=슬롯 목록.
	// 이 상수가 인덱스의 유일한 홈이다 - BP 배선에 숫자를 적지 않는다.
	// 슬롯 페이지를 여기 둔 이유: 메인 메뉴(불러오기)와 일시정지 메뉴(저장)가 같은 화면을 같은 방식으로
	//   연다. 각자 갖게 두면 한쪽만 고쳐지는 순간 갈라진다 - 설정 페이지를 올린 것과 같은 이유다.
	static constexpr int32 PageIndex_Main     = 0;
	static constexpr int32 PageIndex_Settings = 1;
	static constexpr int32 PageIndex_Slots    = 2;

	// 공통 전환. 스위처가 없으면 경고 후 false - 조용히 아무 일도 없으면 UI 버그로 오진한다.
	bool SwitchToPage(int32 PageIndex);

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UWidgetSwitcher> MenuSwitcher;

	// 스위처 1번 페이지에 심은 설정 화면. 전환 시 값 채우기를 지시할 대상이다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UVBSettingsWidget> SettingsView;

	// 스위처 2번 페이지에 심은 슬롯 목록. 전환 시 모드 지정 + 목록 재구성을 지시할 대상이다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UVBSaveSlotListWidget> SlotListView;
};
