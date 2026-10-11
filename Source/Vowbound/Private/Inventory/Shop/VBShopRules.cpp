// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Shop/VBShopRules.h"

#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "Inventory/Shop/VBShopDefinition.h"
#include "Vowbound/Vowbound.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

float VBShop::PriceMultiplier(const TArray<FVBPriceRule>& Rules, const FVBRuleContext& Context)
{
	float Multiplier = 1.0f;
	for (const FVBPriceRule& Rule : Rules)
	{
		if (VBRules::EvaluateAll(Rule.Conditions, Context))
		{
			Multiplier *= Rule.Multiplier;
		}
	}
	return Multiplier;
}

int32 VBShop::UnitPrice(int32 BasePrice, float Multiplier)
{
	if (BasePrice <= 0)
	{
		return 0;
	}
	// 할인이 커도 공짜가 되지 않는다 - 공짜 거래는 값 규칙의 실수로 본다.
	return FMath::Max(1, FMath::RoundToInt(BasePrice * FMath::Max(Multiplier, 0.0f)));
}

EVBPurchaseResult VBShop::CheckPurchase(const FVBPurchaseQuote& Quote)
{
	if (Quote.Count < 1 || Quote.UnitPrice < 1)
	{
		return EVBPurchaseResult::InvalidRequest;
	}
	if (!Quote.bAvailable)
	{
		return EVBPurchaseResult::Unavailable;
	}
	if (Quote.Remaining != INDEX_NONE && Quote.Remaining < Quote.Count)
	{
		return EVBPurchaseResult::OutOfStock;
	}
	// 곱이 int32 를 넘을 수 있다(큰 수량 x 큰 단가) - 넘친 음수가 «돈이 충분하다» 로 읽히지 않게 64비트로 센다.
	if (static_cast<int64>(Quote.UnitPrice) * Quote.Count > Quote.CurrencyHeld)
	{
		return EVBPurchaseResult::NotEnoughCurrency;
	}
	if (Quote.AddableCount < Quote.Count)
	{
		return EVBPurchaseResult::NoRoom;
	}
	return EVBPurchaseResult::Ok;
}

#if WITH_EDITOR
bool VBShop::ValidatePriceRules(const TArray<FVBPriceRule>& Rules, const FText& Where, FDataValidationContext& Context)
{
	bool bValid = true;
	for (int32 Index = 0; Index < Rules.Num(); ++Index)
	{
		const FText RuleWhere = FText::Format(NSLOCTEXT("VBShop", "RuleWhere", "{0} {1} 번"), Where, Index);
		bValid &= VBRules::ValidateStructArray(Rules[Index].Conditions, FVBCondition::StaticStruct(), RuleWhere, Context);
		if (Rules[Index].Multiplier <= 0.0f)
		{
			Context.AddError(FText::Format(NSLOCTEXT("VBShop", "BadMultiplier", "{0}: 배율이 0 이하다."), RuleWhere));
			bValid = false;
		}
	}
	return bValid;
}
#endif

void FVBEffect_OpenShop::Apply(const FVBRuleContext& Context) const
{
	const AController* Controller = Context.Instigator ? Context.Instigator->GetOwningController() : nullptr;
	UVBShopComponent* ShopComponent = Controller ? Controller->FindComponentByClass<UVBShopComponent>() : nullptr;
	if (!ShopComponent || !Shop)
	{
		VB_LOG(Warning, "효과(상점 열기 %s): %s", *GetNameSafe(Shop),
		       !Shop ? TEXT("상점이 비었다") : TEXT("부른 플레이어의 PC 에 상점 컴포넌트가 없다"));
		return;
	}
	ShopComponent->OpenShop(Shop, Context.Counterpart);
}
