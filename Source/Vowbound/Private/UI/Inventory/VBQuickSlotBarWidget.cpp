// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/Inventory/VBQuickSlotBarWidget.h"

#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Vowbound/Vowbound.h"

// ---------------------------------------------------------------- 한 칸

void UVBQuickSlotWidget::SetSlot(const UVBItemDefinition* Item, int32 Count, const FText& KeyText)
{
	const bool bHasIcon = Item && !Item->Icon.IsNull() && ImgIcon;
	if (ImgIcon)
	{
		if (bHasIcon)
		{
			// 소프트 참조라 비동기로 불러 그린다 - HUD 가 칸을 그리느라 멈추지 않는다.
			ImgIcon->SetBrushFromSoftTexture(Item->Icon);
		}
		ImgIcon->SetVisibility(bHasIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (TxtName)
	{
		// 그림이 없으면 이름이 그 자리를 맡는다(헤더 주석 - 지금 아이템은 전부 그림이 없다).
		TxtName->SetText(Item ? Item->DisplayName : FText::GetEmpty());
		TxtName->SetVisibility(Item && !bHasIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (TxtCount)
	{
		TxtCount->SetText(Item ? FText::AsNumber(Count) : FText::GetEmpty());
		TxtCount->SetVisibility(Item ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (TxtKey)
	{
		TxtKey->SetText(KeyText);
		TxtKey->SetVisibility(KeyText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	SetRenderOpacity(Item && Count > 0 ? 1.0f : DimmedOpacity);
}

// ---------------------------------------------------------------- 칸 줄

void UVBQuickSlotBarWidget::SetInventory(UVBInventoryComponent* InInventory)
{
	Unbind();
	Inventory = InInventory;
	if (InInventory)
	{
		InventoryChangedHandle = InInventory->OnInventoryChanged.AddUObject(this, &UVBQuickSlotBarWidget::Refresh);
		QuickSlotsChangedHandle = InInventory->OnQuickSlotsChanged.AddUObject(this, &UVBQuickSlotBarWidget::Refresh);
	}
	Refresh();
}

void UVBQuickSlotBarWidget::SetKeyTexts(const TArray<FText>& InKeyTexts)
{
	KeyTexts = InKeyTexts;
	Refresh();
}

void UVBQuickSlotBarWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UVBQuickSlotBarWidget::Refresh()
{
	if (!SlotBox)
	{
		return;
	}
	const UVBInventoryComponent* Owned = Inventory.Get();
	const int32 SlotCount = Owned ? Owned->GetQuickSlots().Num() : 0;
	if (SlotCount > 0 && !SlotWidgetClass)
	{
		VB_LOG(Warning, "퀵슬롯 칸 줄: SlotWidgetClass 미지정 - 칸을 그릴 수 없다(WBP 클래스 기본값)");
		return;
	}
	// 칸 수가 바뀔 때만 다시 짓는다 - 가진 수·키 이름이 바뀌는 매번 위젯을 새로 만들지 않는다.
	if (SlotBox->GetChildrenCount() != SlotCount)
	{
		SlotBox->ClearChildren();
		for (int32 Index = 0; Index < SlotCount; ++Index)
		{
			SlotBox->AddChild(CreateWidget<UVBQuickSlotWidget>(this, SlotWidgetClass));
		}
	}
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		if (UVBQuickSlotWidget* SlotWidget = Cast<UVBQuickSlotWidget>(SlotBox->GetChildAt(Index)))
		{
			const UVBItemDefinition* Item = Owned->GetQuickSlotItem(Index);
			SlotWidget->SetSlot(Item, Item ? Owned->GetCount(Item) : 0, KeyTexts.IsValidIndex(Index) ? KeyTexts[Index] : FText::GetEmpty());
		}
	}
}

void UVBQuickSlotBarWidget::Unbind()
{
	if (UVBInventoryComponent* Owned = Inventory.Get())
	{
		Owned->OnInventoryChanged.Remove(InventoryChangedHandle);
		Owned->OnQuickSlotsChanged.Remove(QuickSlotsChangedHandle);
	}
	InventoryChangedHandle.Reset();
	QuickSlotsChangedHandle.Reset();
	Inventory.Reset();
}
