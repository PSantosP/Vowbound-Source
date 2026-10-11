// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBShopRules.generated.h"

class UVBShopDefinition;
struct FVBPriceRule;

/** 사기 판정의 답. 화면은 이것으로 사기 버튼을 켜고 끄고 까닭을 보인다. */
UENUM(BlueprintType)
enum class EVBPurchaseResult : uint8
{
	Ok,
	// 수가 1 보다 작거나 단가가 없다 - 정상 화면에서는 나오지 않는 요청.
	InvalidRequest,
	// 줄의 조건이 거짓.
	Unavailable,
	OutOfStock,
	NotEnoughCurrency,
	// 아이템 상한에 걸려 다 넣을 수 없다. 돈만 빠지는 반쪽 거래를 막으려고 아예 안 판다.
	NoRoom,
};

/**
 * 사기 판정에 드는 수 전부. 월드 없이 채울 수 있다 - 서버(결정)와 화면(버튼)이 같은 판정을 쓰고, 단위 시험이 모든 거절 이유를 덮는다.
 * 수를 모으는 쪽: 서버 = UVBShopComponent::ServerBuy, 화면 = UVBShopScreenWidget. 판정 = VBShop::CheckPurchase 하나.
 */
struct FVBPurchaseQuote
{
	int32 Count = 1;
	int32 UnitPrice = 0;
	// 남은 재고. INDEX_NONE = 무한.
	int32 Remaining = INDEX_NONE;
	bool bAvailable = true;
	int32 CurrencyHeld = 0;
	// 지금 가진 수에서 Count 개를 넣으려 할 때 실제로 들어갈 수(UVBItemDefinition::ClampToMax).
	int32 AddableCount = 0;
};

/** 값과 사기 판정. 설계 .sw/narrative 13.3절. 되팔기를 붙이는 날은 방향만 반대인 판정이 여기 더해진다. */
namespace VBShop
{
	// 조건이 참인 규칙의 배율을 모두 곱한다. 빈 목록 = 1.
	VOWBOUND_API float PriceMultiplier(const TArray<FVBPriceRule>& Rules, const FVBRuleContext& Context);

	// 기준 값 x 배율, 반올림, 최소 1. 기준 값이 없으면(0 이하) 0 = 팔 수 없다.
	VOWBOUND_API int32 UnitPrice(int32 BasePrice, float Multiplier);

	VOWBOUND_API EVBPurchaseResult CheckPurchase(const FVBPurchaseQuote& Quote);

#if WITH_EDITOR
	// 값 규칙 배열 검증(경제 설정·상점이 같이 쓴다): 조건 칸, 배율 > 0.
	VOWBOUND_API bool ValidatePriceRules(const TArray<FVBPriceRule>& Rules, const FText& Where, class FDataValidationContext& Context);
#endif
}

/**
 * 상점을 연다 - 대화의 «상점 열기». 부른 플레이어의 PC 에 있는 상점 컴포넌트를 열고, 문맥의 상대(말을 건 NPC)를 상인으로 넘긴다.
 * 상인이 사라지면 거래가 닫힌다. 상대가 없는 자리(퀘스트 보상 등)에서 부르면 상인 없이 열린다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="상점 열기"))
struct VOWBOUND_API FVBEffect_OpenShop : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Shop")
	TObjectPtr<const UVBShopDefinition> Shop;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
