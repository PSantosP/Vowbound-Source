// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Shop/VBShopDefinition.h"

#include "Inventory/VBItemDefinition.h"
#include "Inventory/Shop/VBEconomyConfig.h"
#include "Inventory/Shop/VBEconomySettings.h"
#include "Inventory/Shop/VBShopRules.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBShopDefinition"

int32 FVBShopEntry::GetBasePrice() const
{
	if (PriceOverride > 0)
	{
		return PriceOverride;
	}
	const FVBItemFragment_Price* Price = Item ? Item->FindFragment<FVBItemFragment_Price>() : nullptr;
	return Price ? Price->BaseValue : 0;
}

int32 FVBShopEntry::GetRemaining(int32 Sold) const
{
	return Stock < 0 ? INDEX_NONE : FMath::Max(0, Stock - Sold);
}

const FVBShopEntry* UVBShopDefinition::FindEntry(const UVBItemDefinition* Item) const
{
	return Item ? Entries.FindByPredicate([Item](const FVBShopEntry& Entry) { return Entry.Item == Item; }) : nullptr;
}

#if WITH_EDITOR
EDataValidationResult UVBShopDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!ShopId.IsValid())
	{
		Context.AddError(LOCTEXT("NoShopId", "ShopId 가 비었다 - 저장이 이 상점의 판 수를 기록할 수 없다."));
		Result = EDataValidationResult::Invalid;
	}

	// 돈을 파는 줄은 돈으로 돈을 사는 고리다. 경제 설정이 없으면 이 검사만 건너뛴다(설정 자체의 부재는 상점을 열 때 경고한다).
	const UVBEconomyConfig* Economy = UVBEconomySettings::LoadEconomy(/*bWarnIfMissing=*/false);
	const UVBItemDefinition* Currency = Economy ? Economy->Currency.Get() : nullptr;

	TSet<const UVBItemDefinition*> Seen;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FVBShopEntry& Entry = Entries[Index];
		if (!Entry.Item)
		{
			Context.AddError(FText::Format(LOCTEXT("NoItem", "{0} 번 줄: 아이템이 비었다."), Index));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		const FText ItemName = FText::FromString(Entry.Item->GetName());
		bool bAlreadySeen = false;
		Seen.Add(Entry.Item.Get(), &bAlreadySeen);
		if (bAlreadySeen)
		{
			// 같은 아이템 두 줄 = 재고 열쇠(ShopId, ItemId)가 겹치고, 사기 요청이 어느 줄인지 모른다.
			Context.AddError(FText::Format(LOCTEXT("DupItem", "{0} 번 줄: {1} 이 이미 다른 줄에 있다."), Index, ItemName));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.GetBasePrice() <= 0)
		{
			Context.AddError(FText::Format(LOCTEXT("NoPrice", "{0} 번 줄: {1} 에 값이 없다(가격 조각도 PriceOverride 도 없음)."), Index, ItemName));
			Result = EDataValidationResult::Invalid;
		}
		if (Currency && Entry.Item == Currency)
		{
			Context.AddError(FText::Format(LOCTEXT("SellsCurrency", "{0} 번 줄: 돈({1})을 판다."), Index, ItemName));
			Result = EDataValidationResult::Invalid;
		}
		if (!Entry.Item->ItemId.IsValid())
		{
			Context.AddError(FText::Format(LOCTEXT("NoItemId", "{0} 번 줄: {1} 에 ItemId 가 없다 - 판 수를 기록할 수 없다."), Index, ItemName));
			Result = EDataValidationResult::Invalid;
		}
		const FText ConditionsWhere = FText::Format(LOCTEXT("ConditionsWhere", "{0} 번 줄의 조건"), Index);
		if (!VBRules::ValidateStructArray(Entry.Conditions, FVBCondition::StaticStruct(), ConditionsWhere, Context))
		{
			Result = EDataValidationResult::Invalid;
		}
	}
	if (!VBShop::ValidatePriceRules(PriceRules, LOCTEXT("RulesWhere", "상점 값 규칙"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif

#undef LOCTEXT_NAMESPACE
