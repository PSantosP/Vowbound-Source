// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Inventory/VBInventoryView.h"
#include "UI/VBScreenWidget.h"
#include "VBInventoryScreenWidget.generated.h"

class UButton;
class UImage;
class UInputAction;
class UPanelWidget;
class UTextBlock;
class UVBInventoryComponent;
class UVBListRowWidget;
class UVBItemDefinition;

/** 인벤토리 화면의 필터 탭 하나(.sw/remaining-features/a1-inventory.md). 태그가 비면 «전체». */
USTRUCT()
struct FVBInventoryFilter
{
	GENERATED_BODY()

	// 아이템 ID 의 부모 태그(Item.Medical 이면 Item.Medical.Gauze 가 걸린다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI", meta=(Categories="Item"))
	FGameplayTag Tag;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText Label;
};

/** 장비 능력치 한 줄의 이름표(.sw/remaining-features/b1-equipment.md). 퍼센트면 값에 100 을 곱해 % 로 보인다(받는 피해 배율 -0.05 -> -5%). */
USTRUCT()
struct FVBStatLabel
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText Label;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	bool bPercent = false;
};

/**
 * 인벤토리 화면(UI.Screen.Inventory). 설계 .sw/narrative 10.4절, 버리기·정렬·필터·그림 = .sw/remaining-features/a1-inventory.md, 장비 = b1-equipment.md.
 *
 * 표시는 아무것도 결정하지 않는다(설계 1절 규칙 3): 목록은 UVBInventoryComponent::OnInventoryChanged 를 듣고 다시 그리고,
 *   «사용» 은 요청만 보낸다(RequestUseItem). 쓸 수 있나의 최종 판단은 서버가 다시 한다 - 버튼을 켜고 끄는 것은 보기일 뿐이다.
 *   퀵슬롯 칸 버튼도 같다 - 누르면 RequestAssignQuickSlot 만 보내고, 칸이 바뀐 것은 OnQuickSlotsChanged 를 듣고 다시 그린다.
 * 여는 쪽은 HUD(InventoryInputAction). 닫기는 뒤로 가기(bIsBackHandler) 또는 같은 입력을 한 번 더.
 * 화면 위젯은 재사용되므로(UVBScreenWidget 주석) 켜질 때마다 처음부터 채운다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBInventoryScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	// 목록 한 줄의 WBP(공용 목록 줄 - 대화·일지·상점과 같다, 설계 13.1절 3번). 비면 목록이 비어 보이고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBListRowWidget> EntryWidgetClass;

	// 줄 옆 글(가진 수). {0} = 수.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText CountFormat = NSLOCTEXT("VBInventory", "EntryCount", "x{0}");

	// 화면이 떠 있는 동안 이 입력을 한 번 더 누르면 닫는다(HUD 가 여는 입력과 같은 IA 를 꽂는다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TObjectPtr<UInputAction> CloseInputAction;

	// 고른 아이템을 퀵슬롯에 넣는 칸 버튼(공용 목록 줄). 칸 수만큼 런타임에 짓는다 - 칸 수의 홈은 인벤토리(QuickSlotCount)라 WBP 에 칸을 그려 두지 않는다.
	//   비면 칸 버튼이 안 나오고 경고한다. 설계 .sw/quickslot-input 4.1절.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBListRowWidget> QuickSlotButtonClass;

	// 칸 버튼 글. {0} = 칸 번호(1부터 - 키 1·2·3·4 와 같은 수).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText QuickSlotLabelFormat = NSLOCTEXT("VBInventory", "QuickSlotLabel", "퀵슬롯 {0}");

	// 고른 아이템이 이미 든 칸의 옆 글 - 그 칸을 누르면 뺀다. 다른 아이템이 든 칸은 그 아이템 이름이 옆 글이다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText QuickSlotRemoveText = NSLOCTEXT("VBInventory", "QuickSlotRemove", "빼기");

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> EntryBox;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtEmpty;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemDescription;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnUse;

	// 칸 버튼이 들어갈 자리. 없으면 이 화면에서 퀵슬롯을 못 넣는다(선택 바인딩 - 칸이 없는 화면 변형도 열려야 한다).
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> QuickSlotBox;

	// 필터 탭(데이터). 비면 필터 줄 없이 전체를 보인다. 탭 버튼은 FilterBox 에 런타임으로 짓는다 - 퀵슬롯 칸 버튼과 같은 길.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TArray<FVBInventoryFilter> Filters;

	// 필터 탭 버튼의 WBP(공용 목록 줄). 비면 탭이 안 나오고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBListRowWidget> FilterButtonClass;

	// 지금 고른 필터 탭의 옆 글(나머지 탭은 비어 있다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText ActiveFilterMark = NSLOCTEXT("VBInventory", "ActiveFilter", "보는 중");

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> FilterBox;

	// 누를 때마다 다음 정렬(획득 순 -> 이름 -> 수량 -> 종류). 글은 TxtSort 가 보인다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnSort;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtSort;

	// 고른 아이템 하나를 버린다(발밑에 떨어진다). 버릴 수 없거나 가진 것이 없으면 꺼진다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnDiscard;

	// 상세의 아이템 그림. 그림이 없는 아이템이면 접는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ImgItemIcon;

	// ---- 장비(b1-equipment.md). 칸 줄은 EquipBox 에 칸 수만큼 런타임으로 짓는다(칸 배치의 홈은 UVBEquipmentComponent::SlotLayout).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBListRowWidget> EquipSlotRowClass;

	// 칸 종류 -> 칸 이름(Equipment.Slot.Head -> 머리). 없으면 태그 이름이 보인다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI", meta=(Categories="Equipment.Slot"))
	TMap<FGameplayTag, FText> SlotLabels;

	// 능력치 -> 이름표(Data.Stat.MaxHealth -> 최대 체력). 없으면 태그 이름이 보인다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI", meta=(Categories="Data.Stat"))
	TMap<FGameplayTag, FVBStatLabel> StatLabels;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText EmptySlotText = NSLOCTEXT("VBInventory", "EmptySlot", "비어 있음");

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText EquipText = NSLOCTEXT("VBInventory", "Equip", "장착");

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText UnequipText = NSLOCTEXT("VBInventory", "Unequip", "해제");

	// 능력치 한 줄. {0} = 이름표, {1} = 값, {2} = 같은 칸에 지금 입은 장비의 값.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText StatLineFormat = NSLOCTEXT("VBInventory", "StatLine", "{0} {1}");

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText StatCompareFormat = NSLOCTEXT("VBInventory", "StatCompare", "{0} {1} (지금 {2})");

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> EquipBox;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnEquip;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtEquip;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemStats;

private:
	void RefreshEquipment();
	void HandleEquipSlotClicked(int32 SlotIndex);
	FText FormatStat(const FGameplayTag& Stat, float Value) const;
	FText BuildStatText(const UVBItemDefinition* Item) const;

	UFUNCTION()
	void HandleEquipClicked();

	TWeakObjectPtr<class UVBEquipmentComponent> Equipment;
	FDelegateHandle EquipmentChangedHandle;

	void RebuildFilterButtons();
	void HandleFilterClicked(int32 FilterIndex);
	FText SortLabel() const;

	UFUNCTION()
	void HandleSortClicked();

	UFUNCTION()
	void HandleDiscardClicked();

	// 화면 상태 - 저장하지 않는다. 화면 위젯은 재사용되므로 다시 열어도 마지막 선택이 남는다.
	int32 ActiveFilterIndex = 0;
	EVBInventorySort ActiveSort = EVBInventorySort::Acquired;

	void Rebuild();
	void Select(int32 RowIndex);
	void RefreshDetail();
	void RefreshQuickSlotButtons();
	void HandleInventoryChanged();
	void HandleQuickSlotClicked(int32 SlotIndex);
	void HandleCloseInput();

	UFUNCTION()
	void HandleUseClicked();

	TWeakObjectPtr<UVBInventoryComponent> Inventory;
	TWeakObjectPtr<const UVBItemDefinition> Selected;
	// 줄 번호 -> 아이템(줄 위젯은 번호만 알린다).
	TArray<TWeakObjectPtr<const UVBItemDefinition>> RowItems;
	FDelegateHandle InventoryChangedHandle;
	FDelegateHandle QuickSlotsChangedHandle;
};
