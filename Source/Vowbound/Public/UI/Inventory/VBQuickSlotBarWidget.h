// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBQuickSlotBarWidget.generated.h"

class UImage;
class UPanelWidget;
class UTextBlock;
class UVBInventoryComponent;
class UVBItemDefinition;

/**
 * 퀵슬롯 한 칸의 생김새: 아이콘(없으면 이름)·가진 수·키 이름. 칸 줄(UVBQuickSlotBarWidget)이 만들고 채운다 - 혼자서는 아무것도 듣지 않는다.
 * 아이콘이 없을 때 이름을 보이는 이유: 2026-10-02 에 잰 아이템 자산 셋(거즈·크리아·병동 열쇠) 모두 Icon 이 비어 있다. 아이콘만 그리면 빈 칸처럼 보인다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBQuickSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Item = nullptr 이면 빈 칸. Count 가 0 이면 흐리게(칸은 남는다 - 다시 주우면 돌아온다). KeyText 가 비면 키 칸을 접는다.
	void SetSlot(const UVBItemDefinition* Item, int32 Count, const FText& KeyText);

protected:
	// 가진 것이 0 인 칸·빈 칸의 불투명도.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DimmedOpacity = 0.35f;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	// 이름·수·키는 칸의 본체라 필수다 - 이름이 틀리면 WBP 컴파일에서 멈춘다(공용 목록 줄과 같은 판단). 아이콘은 그림이 없는 칸 변형이 있어 선택이다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ImgIcon;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtName;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtCount;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtKey;
};

/**
 * HUD 의 퀵슬롯 칸 줄. 설계 .sw/quickslot-input 4.1절.
 *
 * 표시는 아무것도 결정하지 않는다: 칸은 인벤토리의 OnQuickSlotsChanged, 가진 수는 OnInventoryChanged 를 듣고 다시 그린다.
 * 칸 수는 인벤토리가 정한다(QuickSlotCount) - 칸 위젯을 그 수만큼 런타임에 짓는다. 키 이름은 HUD 가 넣는다(SetKeyTexts) -
 *   지금 장치·입력 모드를 아는 것이 HUD 라서다(상호작용 프롬프트의 키 이름과 같은 길).
 * HUD 가 런타임에 루트 캔버스에 붙인다(VBHUDWidget 의 화면 밖 화살표·상호작용 프롬프트와 같은 이유 - WBP_HUD 트리는 저작 도구가 통째로만 덮는다).
 */
UCLASS(Abstract)
class VOWBOUND_API UVBQuickSlotBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 그릴 인벤토리. HUD 가 조종 캐릭터를 얻은 뒤 한 번 부른다(인벤토리는 PlayerState 에 있어 폰이 바뀌어도 같다).
	void SetInventory(UVBInventoryComponent* InInventory);

	// 칸 번호 -> 키 이름. 모자란 칸은 키 이름이 빈다.
	void SetKeyTexts(const TArray<FText>& InKeyTexts);

protected:
	virtual void NativeDestruct() override;

	// 한 칸의 WBP. 비면 칸이 안 그려지고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	TSubclassOf<UVBQuickSlotWidget> SlotWidgetClass;

	// 칸들이 들어갈 자리(가로 상자 등). 칸 줄의 본체라 필수다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> SlotBox;

private:
	void Refresh();
	void Unbind();

	TWeakObjectPtr<UVBInventoryComponent> Inventory;
	FDelegateHandle InventoryChangedHandle;
	FDelegateHandle QuickSlotsChangedHandle;
	TArray<FText> KeyTexts;
};
