// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Shop/VBEconomyConfig.h"

#include "Inventory/Shop/VBShopRules.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBEconomyConfig"

#if WITH_EDITOR
EDataValidationResult UVBEconomyConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!Currency)
	{
		Context.AddError(LOCTEXT("NoCurrency", "Currency 가 비었다 - 상점이 값을 치를 수 없다."));
		Result = EDataValidationResult::Invalid;
	}
	if (!VBShop::ValidatePriceRules(PriceRules, LOCTEXT("RulesWhere", "전역 값 규칙"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif

#undef LOCTEXT_NAMESPACE
