// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "Inventory/Shop/VBShopRules.h"
#include "VBShopScreenWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class UVBInventoryComponent;
class UVBListRowWidget;
class UVBShopComponent;
struct FVBShopViewEntry;

/**
 * 상점 화면(UI.Screen.Shop). 설계 .sw/narrative 13.5절. 대화 화면과 같은 모양이다.
 *
 * 여닫는 주인은 PC 다(상점 컴포넌트의 보기가 살아 있으면 열고, 닫히면 내린다). 이 화면은 그 컴포넌트의 OnViewChanged 와
 *   인벤토리의 OnInventoryChanged(가진 돈이 바뀌면 사기 버튼이 바뀐다)만 듣고 그린다.
 * 표시는 아무것도 결정하지 않는다: 값·남은 수·조건은 서버가 보낸 보기이고, 사기 버튼은 같은 판정(VBShop::CheckPurchase)으로 켜고 끌 뿐이다.
 * 뒤로 가기·닫기 버튼 = 닫아 달라는 요청. 바로 내리지 않는다 - 서버가 닫음을 보내면 PC 가 내린다(거래의 주인이 하나).
 */
UCLASS(Abstract)
class VOWBOUND_API UVBShopScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBShopScreenWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual bool NativeOnHandleBackAction() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }

	// 목록 한 줄의 WBP(공용 목록 줄). 비면 목록이 비어 보이고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBListRowWidget> EntryWidgetClass;

	// 줄 옆 글 - 재고 무한일 때. {0} = 단가.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText PriceFormat;

	// 줄 옆 글 - 재고가 정해졌을 때. {0} = 단가, {1} = 남은 수.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText LimitedPriceFormat;

	// 가진 돈. {Name} = 돈 아이템의 표시 이름, {Count} = 가진 수.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText CurrencyFormat;

	// 사기를 못 하는 까닭(Ok 는 비운다). 빠진 것은 까닭 칸을 비운다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TMap<EVBPurchaseResult, FText> ResultTexts;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtShopName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> EntryBox;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtEmpty;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemDescription;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtCurrency;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtStatus;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnBuy;

	// 닫기 버튼(BtnClose)은 기반 UVBScreenWidget 이 든다(2026-10-07). 누르면 뒤로 가기와 같은 길이라 NativeOnHandleBackAction 의
	//   «닫아 달라는 요청» 이 그대로 받는다. 필수(생성자의 bRequireCloseButton): 에디터는 Esc 를 PIE 종료로 쓰므로 PIE 에서는
	//   이 버튼이 상점을 벗어나는 유일한 길이다(2026-10-02 user PIE). 빠지면 WBP 컴파일이 실패한다.

private:
	// 뒤로 가기와 닫기 버튼이 같이 쓰는 닫기 요청.
	void RequestCloseShop();

	void Rebuild();
	void Select(int32 RowIndex);
	void RefreshDetail();
	const FVBShopViewEntry* FindSelected() const;

	UFUNCTION()
	void HandleBuyClicked();

	TWeakObjectPtr<UVBShopComponent> ShopComponent;
	TWeakObjectPtr<UVBInventoryComponent> Inventory;
	// 고른 줄 = 상점 정의 안의 번호(거래 뒤 보기를 다시 받아도 같은 줄을 가리킨다).
	int32 SelectedEntryIndex = INDEX_NONE;
	// 목록 줄 번호 -> 상점 정의 안의 번호(줄 위젯은 자기 번호만 알린다).
	TArray<int32> RowEntryIndices;
	FDelegateHandle ViewChangedHandle;
	FDelegateHandle InventoryChangedHandle;
};
