// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/Shop/VBShopScreenWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "Inventory/Shop/VBShopDefinition.h"
#include "UI/VBListRowWidget.h"
#include "Vowbound/Vowbound.h"

#define LOCTEXT_NAMESPACE "VBShopScreen"

UVBShopScreenWidget::UVBShopScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 뒤로 가기를 받아 «닫기 요청» 으로 돌린다(NativeOnHandleBackAction). 끄면 뒤로 가기로 상점을 벗어날 길이 없다.
	bIsBackHandler = true;
	bRequireCloseButton = true;
	PriceFormat = LOCTEXT("Price", "{0}");
	LimitedPriceFormat = LOCTEXT("LimitedPrice", "{0} (남은 {1})");
	// 돈의 이름은 돈 아이템(경제 설정)의 표시 이름 한 곳이 든다 - 화면 글에 이름을 또 적으면 이름을 바꾸는 날 두 곳을 고친다.
	CurrencyFormat = LOCTEXT("Currency", "{Name} {Count}");
	ResultTexts.Add(EVBPurchaseResult::Unavailable, LOCTEXT("Unavailable", "아직 살 수 없다"));
	ResultTexts.Add(EVBPurchaseResult::OutOfStock, LOCTEXT("OutOfStock", "다 팔렸다"));
	ResultTexts.Add(EVBPurchaseResult::NotEnoughCurrency, LOCTEXT("NotEnoughCurrency", "값을 치를 수 없다"));
	ResultTexts.Add(EVBPurchaseResult::NoRoom, LOCTEXT("NoRoom", "더 가질 수 없다"));
}

void UVBShopScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnBuy)
	{
		BtnBuy->OnClicked.AddUniqueDynamic(this, &UVBShopScreenWidget::HandleBuyClicked);
	}
}

void UVBShopScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	const APlayerController* PC = GetOwningPlayer();
	ShopComponent = PC ? PC->FindComponentByClass<UVBShopComponent>() : nullptr;
	Inventory = UVBInventoryComponent::FindFor(PC);
	if (UVBShopComponent* Shop = ShopComponent.Get())
	{
		ViewChangedHandle = Shop->OnViewChanged.AddUObject(this, &UVBShopScreenWidget::Rebuild);
	}
	else
	{
		VB_LOG(Warning, "상점 화면: 이 플레이어의 상점 컴포넌트를 찾지 못했다(PlayerController 에 UVBShopComponent)");
	}
	if (UVBInventoryComponent* Owned = Inventory.Get())
	{
		// 가진 돈·가진 수가 바뀌면 사기 버튼과 돈 글만 다시 본다.
		InventoryChangedHandle = Owned->OnInventoryChanged.AddUObject(this, &UVBShopScreenWidget::RefreshDetail);
	}
	SelectedEntryIndex = INDEX_NONE;
	Rebuild();
}

void UVBShopScreenWidget::NativeOnDeactivated()
{
	if (UVBShopComponent* Shop = ShopComponent.Get())
	{
		Shop->OnViewChanged.Remove(ViewChangedHandle);
	}
	if (UVBInventoryComponent* Owned = Inventory.Get())
	{
		Owned->OnInventoryChanged.Remove(InventoryChangedHandle);
	}
	ViewChangedHandle.Reset();
	InventoryChangedHandle.Reset();
	Super::NativeOnDeactivated();
}

bool UVBShopScreenWidget::NativeOnHandleBackAction()
{
	if (!bIsBackHandler)
	{
		return false;
	}
	RequestCloseShop();
	return true;
}

void UVBShopScreenWidget::RequestCloseShop()
{
	if (UVBShopComponent* Shop = ShopComponent.Get())
	{
		Shop->RequestClose();
	}
	else
	{
		// 거래의 주인을 못 찾았다 - 화면만이라도 내려 입력이 갇히지 않게 한다.
		DeactivateWidget();
	}
}

UWidget* UVBShopScreenWidget::NativeGetDesiredFocusTarget() const
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

void UVBShopScreenWidget::Rebuild()
{
	RowEntryIndices.Reset();
	if (EntryBox)
	{
		EntryBox->ClearChildren();
	}
	const UVBShopComponent* Shop = ShopComponent.Get();
	const FVBShopView* View = Shop ? &Shop->GetView() : nullptr;
	if (TxtShopName)
	{
		TxtShopName->SetText(View && View->Shop ? View->Shop->DisplayName : FText::GetEmpty());
	}
	const int32 EntryCount = View ? View->Entries.Num() : 0;
	if (!EntryWidgetClass && EntryCount > 0)
	{
		VB_LOG(Warning, "상점 화면: EntryWidgetClass 미지정 - 목록을 그릴 수 없다(WBP 클래스 기본값)");
	}
	bool bSelectedStillListed = false;
	for (int32 Index = 0; Index < EntryCount; ++Index)
	{
		const FVBShopViewEntry& ViewEntry = View->Entries[Index];
		const FVBShopEntry* Entry = View->GetEntry(ViewEntry);
		if (!Entry || !Entry->Item)
		{
			continue;
		}
		const int32 RowIndex = RowEntryIndices.Add(ViewEntry.EntryIndex);
		bSelectedStillListed |= ViewEntry.EntryIndex == SelectedEntryIndex;
		if (!EntryBox || !EntryWidgetClass)
		{
			continue;
		}
		const FText Detail = ViewEntry.Remaining == INDEX_NONE
			? FText::Format(PriceFormat, ViewEntry.UnitPrice)
			: FText::Format(LimitedPriceFormat, ViewEntry.UnitPrice, ViewEntry.Remaining);
		UVBListRowWidget* Row = CreateWidget<UVBListRowWidget>(this, EntryWidgetClass);
		// 잠긴 줄도 고를 수는 있다(설명을 읽게) - 사기 버튼이 까닭과 함께 꺼진다.
		Row->Setup(RowIndex, Entry->Item->DisplayName, Detail, /*bEnabled=*/true);
		Row->OnRowClicked.BindUObject(this, &UVBShopScreenWidget::Select);
		EntryBox->AddChild(Row);
	}
	if (TxtEmpty)
	{
		TxtEmpty->SetVisibility(RowEntryIndices.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!bSelectedStillListed)
	{
		SelectedEntryIndex = RowEntryIndices.IsEmpty() ? INDEX_NONE : RowEntryIndices[0];
	}
	RefreshDetail();
	// 초점이 있던 줄이 방금 지워졌다 - 새 첫 줄로 옮긴다(게임패드).
	RequestRefreshFocus();
}

void UVBShopScreenWidget::Select(int32 RowIndex)
{
	if (RowEntryIndices.IsValidIndex(RowIndex))
	{
		SelectedEntryIndex = RowEntryIndices[RowIndex];
		RefreshDetail();
	}
}

const FVBShopViewEntry* UVBShopScreenWidget::FindSelected() const
{
	const UVBShopComponent* Shop = ShopComponent.Get();
	if (!Shop || SelectedEntryIndex == INDEX_NONE)
	{
		return nullptr;
	}
	const int32 Selected = SelectedEntryIndex;
	return Shop->GetView().Entries.FindByPredicate([Selected](const FVBShopViewEntry& E) { return E.EntryIndex == Selected; });
}

void UVBShopScreenWidget::RefreshDetail()
{
	const UVBShopComponent* Shop = ShopComponent.Get();
	const FVBShopViewEntry* ViewEntry = FindSelected();
	const FVBShopEntry* Entry = Shop && ViewEntry ? Shop->GetView().GetEntry(*ViewEntry) : nullptr;
	const UVBItemDefinition* Item = Entry ? Entry->Item.Get() : nullptr;
	if (TxtItemName)
	{
		TxtItemName->SetText(Item ? Item->DisplayName : FText::GetEmpty());
	}
	if (TxtItemDescription)
	{
		TxtItemDescription->SetText(Item ? Item->Description : FText::GetEmpty());
	}
	if (TxtCurrency)
	{
		const UVBInventoryComponent* Owned = Inventory.Get();
		const UVBItemDefinition* Currency = UVBShopComponent::GetCurrency();
		TxtCurrency->SetText(FText::FormatNamed(CurrencyFormat,
			TEXT("Name"), Currency ? Currency->DisplayName : FText::GetEmpty(),
			TEXT("Count"), Owned && Currency ? Owned->GetCount(Currency) : 0));
	}
	// 버튼은 서버와 같은 판정으로 켠다(표시용). 고른 줄이 없으면 꺼진다.
	const EVBPurchaseResult Result = Shop && ViewEntry ? Shop->PreviewPurchase(*ViewEntry, /*Count=*/1) : EVBPurchaseResult::InvalidRequest;
	if (BtnBuy)
	{
		BtnBuy->SetIsEnabled(Result == EVBPurchaseResult::Ok);
	}
	if (TxtStatus)
	{
		const FText* Reason = ViewEntry ? ResultTexts.Find(Result) : nullptr;
		TxtStatus->SetText(Reason ? *Reason : FText::GetEmpty());
	}
}

void UVBShopScreenWidget::HandleBuyClicked()
{
	UVBShopComponent* Shop = ShopComponent.Get();
	const FVBShopViewEntry* ViewEntry = FindSelected();
	const FVBShopEntry* Entry = Shop && ViewEntry ? Shop->GetView().GetEntry(*ViewEntry) : nullptr;
	if (Entry && Entry->Item)
	{
		Shop->RequestBuy(Entry->Item, 1);
	}
}

#undef LOCTEXT_NAMESPACE
