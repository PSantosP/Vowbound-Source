#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/Shop/VBShopDefinition.h"
#include "Inventory/Shop/VBShopRules.h"
#include "Inventory/Shop/VBShopStateSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "SaveSystem/VBSaveGame.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 상점 단계의 단위 시험. 설계 .sw/narrative 13절.
// 값·사기 판정은 월드 없는 순수 함수라 여기서 거절 이유를 전부 덮는다. 거래 컴포넌트(RPC·인벤토리·재고를 잇는 쪽)는 PIE 가 본다.
// 상점·아이템 태그(Shop.*·Item.*)는 ini 내용이라 네이티브 태그를 빌려 쓴다 - 연산은 태그 이름과 무관하다.

#if WITH_AUTOMATION_TESTS
namespace
{
	UVBItemDefinition* MakePricedItem(FGameplayTag Id, int32 BaseValue)
	{
		UVBItemDefinition* Item = NewObject<UVBItemDefinition>();
		Item->ItemId = Id;
		if (BaseValue > 0)
		{
			FVBItemFragment_Price Price;
			Price.BaseValue = BaseValue;
			Item->Fragments.Add(FInstancedStruct::Make(Price));
		}
		return Item;
	}

	FVBPriceRule MakeRule(float Multiplier, bool bWithFalseCondition)
	{
		FVBPriceRule Rule;
		Rule.Multiplier = Multiplier;
		if (bWithFalseCondition)
		{
			// 사실 저장소가 없는 문맥에서 사실 조건은 거짓이다(VBRules 규약 - 모르면 거짓).
			FVBCondition_Fact Fact;
			Fact.Fact = VBGameplayTags::Melee_Window;
			Rule.Conditions.Add(FInstancedStruct::Make(Fact));
		}
		return Rule;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBShopPricingTest, "Vowbound.Unit.Shop.Pricing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBShopPricingTest::RunTest(const FString& Parameters)
{
	const FVBRuleContext Context;
	TestEqual(TEXT("no rules = x1"), VBShop::PriceMultiplier({}, Context), 1.0f);

	const TArray<FVBPriceRule> BothTrue = {MakeRule(0.8f, false), MakeRule(1.25f, false)};
	TestEqual(TEXT("true rules multiply together"), VBShop::PriceMultiplier(BothTrue, Context), 1.0f);

	AddExpectedMessagePlain(TEXT("문맥에 사실 저장소가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	const TArray<FVBPriceRule> OneFalse = {MakeRule(0.5f, true), MakeRule(1.5f, false)};
	TestEqual(TEXT("false rule is skipped"), VBShop::PriceMultiplier(OneFalse, Context), 1.5f);

	TestEqual(TEXT("base x multiplier"), VBShop::UnitPrice(100, 0.8f), 80);
	TestEqual(TEXT("rounds to nearest"), VBShop::UnitPrice(3, 1.5f), 5);
	TestEqual(TEXT("a discount never makes it free"), VBShop::UnitPrice(10, 0.01f), 1);
	TestEqual(TEXT("no base value = not for sale"), VBShop::UnitPrice(0, 1.0f), 0);
	TestEqual(TEXT("negative multiplier is clamped, still not free"), VBShop::UnitPrice(10, -2.0f), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBShopPurchaseTest, "Vowbound.Unit.Shop.Purchase",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBShopPurchaseTest::RunTest(const FString& Parameters)
{
	FVBPurchaseQuote Ok;
	Ok.Count = 2;
	Ok.UnitPrice = 10;
	Ok.Remaining = 5;
	Ok.bAvailable = true;
	Ok.CurrencyHeld = 20;
	Ok.AddableCount = 2;
	TestEqual(TEXT("exact money, stock and room = ok"), VBShop::CheckPurchase(Ok), EVBPurchaseResult::Ok);

	FVBPurchaseQuote Q = Ok;
	Q.Count = 0;
	TestEqual(TEXT("count 0"), VBShop::CheckPurchase(Q), EVBPurchaseResult::InvalidRequest);
	Q = Ok;
	Q.UnitPrice = 0;
	TestEqual(TEXT("no price"), VBShop::CheckPurchase(Q), EVBPurchaseResult::InvalidRequest);
	Q = Ok;
	Q.bAvailable = false;
	TestEqual(TEXT("condition false"), VBShop::CheckPurchase(Q), EVBPurchaseResult::Unavailable);
	Q = Ok;
	Q.Remaining = 1;
	TestEqual(TEXT("not enough stock"), VBShop::CheckPurchase(Q), EVBPurchaseResult::OutOfStock);
	Q = Ok;
	Q.Remaining = INDEX_NONE;
	Q.Count = 2;
	TestEqual(TEXT("unlimited stock"), VBShop::CheckPurchase(Q), EVBPurchaseResult::Ok);
	Q = Ok;
	Q.CurrencyHeld = 19;
	TestEqual(TEXT("one short"), VBShop::CheckPurchase(Q), EVBPurchaseResult::NotEnoughCurrency);
	Q = Ok;
	Q.AddableCount = 1;
	TestEqual(TEXT("no room for the second"), VBShop::CheckPurchase(Q), EVBPurchaseResult::NoRoom);

	// int32 곱이 넘쳐 음수가 되면 «돈이 충분하다» 로 읽힐 수 있다 - 64비트로 세는지 본다.
	Q = Ok;
	Q.Count = 300000;
	Q.UnitPrice = 10000;
	Q.Remaining = INDEX_NONE;
	Q.CurrencyHeld = MAX_int32;
	Q.AddableCount = 300000;
	TestEqual(TEXT("overflowing total is not affordable"), VBShop::CheckPurchase(Q), EVBPurchaseResult::NotEnoughCurrency);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBShopStockTest, "Vowbound.Unit.Shop.Stock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBShopStockTest::RunTest(const FString& Parameters)
{
	FVBShopEntry Limited;
	Limited.Stock = 3;
	TestEqual(TEXT("nothing sold"), Limited.GetRemaining(0), 3);
	TestEqual(TEXT("some sold"), Limited.GetRemaining(2), 1);
	TestEqual(TEXT("oversold reads 0, not negative"), Limited.GetRemaining(5), 0);
	FVBShopEntry Unlimited;
	TestEqual(TEXT("unlimited"), Unlimited.GetRemaining(100), static_cast<int32>(INDEX_NONE));

	UVBItemDefinition* Herb = MakePricedItem(VBGameplayTags::Melee_Shape_Weapon, 7);
	FVBShopEntry Priced;
	Priced.Item = Herb;
	TestEqual(TEXT("base price from the item fragment"), Priced.GetBasePrice(), 7);
	Priced.PriceOverride = 12;
	TestEqual(TEXT("shop override wins"), Priced.GetBasePrice(), 12);

	const FGameplayTag Shop = VBGameplayTags::Melee_Window;
	const FGameplayTag ItemA = VBGameplayTags::Melee_Shape_Weapon;
	const FGameplayTag ItemB = VBGameplayTags::Melee_Shape_HandL;
	UVBShopStateSubsystem* State = NewObject<UVBShopStateSubsystem>();
	State->AddSold(Shop, ItemA, 2);
	State->AddSold(Shop, ItemA, 1);
	State->AddSold(Shop, ItemB, 4);
	TestEqual(TEXT("sales add up per (shop, item)"), State->GetSold(Shop, ItemA), 3);
	TestEqual(TEXT("other item kept apart"), State->GetSold(Shop, ItemB), 4);
	TestEqual(TEXT("unknown key reads 0"), State->GetSold(ItemA, Shop), 0);
	AddExpectedMessagePlain(TEXT("열쇠가 비었거나"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	State->AddSold(FGameplayTag(), ItemA, 1);

	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	const FVBSaveContext Context;
	State->WriteSave(*Save, Context);
	TestEqual(TEXT("one saved line per (shop, item)"), Save->Shops.Sales.Num(), 2);

	// 읽을 수 없는 기록 하나를 섞는다 - 버리고 소리 낸다.
	FVBSavedShopSale& Broken = Save->Shops.Sales.AddDefaulted_GetRef();
	Broken.ItemId = ItemA;
	Broken.Sold = 9;
	UVBShopStateSubsystem* Loaded = NewObject<UVBShopStateSubsystem>();
	Loaded->AddSold(Shop, ItemB, 100);
	AddExpectedMessagePlain(TEXT("읽을 수 없는 기록"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Loaded->ReadSave(*Save, Context);
	TestEqual(TEXT("round trip A"), Loaded->GetSold(Shop, ItemA), 3);
	TestEqual(TEXT("loading replaces, not adds"), Loaded->GetSold(Shop, ItemB), 4);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBShopValidateTest, "Vowbound.Unit.Shop.Validate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBShopValidateTest::RunTest(const FString& Parameters)
{
	UVBItemDefinition* Herb = MakePricedItem(VBGameplayTags::Melee_Shape_Weapon, 7);
	UVBItemDefinition* Unpriced = MakePricedItem(VBGameplayTags::Melee_Shape_HandL, 0);

	UVBShopDefinition* Good = NewObject<UVBShopDefinition>();
	Good->ShopId = VBGameplayTags::Melee_Window;
	Good->Entries.AddDefaulted_GetRef().Item = Herb;
	Good->PriceRules.Add(MakeRule(0.9f, false));
	{
		FDataValidationContext Context;
		TestEqual(TEXT("valid shop"), Good->IsDataValid(Context), EDataValidationResult::Valid);
	}

	// 대조: 같은 상점에 결함을 하나씩 넣으면 각각 무효가 된다.
	struct FCase { const TCHAR* Name; TFunction<void(UVBShopDefinition&)> Break; };
	const TArray<FCase> Cases = {
		{TEXT("no ShopId"), [](UVBShopDefinition& S) { S.ShopId = FGameplayTag(); }},
		{TEXT("empty item"), [](UVBShopDefinition& S) { S.Entries.AddDefaulted(); }},
		{TEXT("same item twice"), [Herb](UVBShopDefinition& S) { S.Entries.AddDefaulted_GetRef().Item = Herb; }},
		{TEXT("no price"), [Unpriced](UVBShopDefinition& S) { S.Entries.AddDefaulted_GetRef().Item = Unpriced; }},
		{TEXT("empty condition slot"), [](UVBShopDefinition& S) { S.Entries[0].Conditions.AddDefaulted(); }},
		{TEXT("zero multiplier"), [](UVBShopDefinition& S) { S.PriceRules[0].Multiplier = 0.0f; }},
	};
	for (const FCase& Case : Cases)
	{
		UVBShopDefinition* Broken = DuplicateObject(Good, GetTransientPackage());
		Case.Break(*Broken);
		FDataValidationContext Context;
		TestEqual(Case.Name, Broken->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	return true;
}
#endif
#endif
