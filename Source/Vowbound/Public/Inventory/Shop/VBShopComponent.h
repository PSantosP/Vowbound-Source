// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/Shop/VBShopRules.h"
#include "VBShopComponent.generated.h"

class UVBEconomyConfig;
class UVBItemDefinition;
class UVBShopDefinition;
struct FVBShopEntry;

/** 보기의 한 줄. 서버가 계산한 것 그대로다 - 클라가 값을 계산하지 않는다. */
USTRUCT()
struct VOWBOUND_API FVBShopViewEntry
{
	GENERATED_BODY()

	// 상점 정의의 Entries 안 번호(아이템·이름·설명은 거기서 읽는다).
	UPROPERTY()
	int32 EntryIndex = INDEX_NONE;

	UPROPERTY()
	int32 UnitPrice = 0;

	// INDEX_NONE = 무한.
	UPROPERTY()
	int32 Remaining = INDEX_NONE;

	// 줄의 조건. 거짓인데 보이는 줄은 bShowWhenUnavailable 인 것뿐이다(숨는 줄은 보기에 없다).
	UPROPERTY()
	bool bAvailable = true;
};

/** 주인 클라가 그릴 지금의 거래. 열려 있지 않으면 IsActive() 가 거짓이다. */
USTRUCT()
struct VOWBOUND_API FVBShopView
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UVBShopDefinition> Shop = nullptr;

	UPROPERTY()
	TArray<FVBShopViewEntry> Entries;

	bool IsActive() const { return Shop != nullptr; }
	const FVBShopEntry* GetEntry(const FVBShopViewEntry& ViewEntry) const;
};

DECLARE_MULTICAST_DELEGATE(FVBOnShopViewChanged);

/**
 * 거래 진행의 주인. AVBPlayerController 에 붙는다. 설계 .sw/narrative 3.4절·13.4절. 대화 컴포넌트(UVBDialogueComponent)와 같은 모양이다.
 *
 * 서버가 «열린 상점·상인» 을 든다. 주인 클라는 ClientShowShop 으로 받은 보기를 그리고, 버튼은 요청(RequestBuy·RequestClose)만 보낸다.
 * 사기는 ServerBuy 한 곳에서: 열린 상점과 같은가 -> 상인이 아직 있나 -> 그 아이템 줄 -> 조건 -> 값 -> VBShop::CheckPurchase -> 바꾸기.
 *   바꾸기 = 돈 빼기 -> 아이템 넣기 -> 판 수 기록. 다 확인한 뒤에 바꾸므로 반쪽 거래가 없다.
 * 요청에 상점을 함께 보내는 이유: 닫고 다른 상점을 연 사이에 도착한 요청을 버리려고(대화가 노드 이름을 함께 보내는 것과 같다).
 * 가진 돈은 보기에 없다 - 주인 클라에 인벤토리가 이미 복제된다. 같은 값을 두 길로 보내지 않는다.
 * 화면: 이 컴포넌트는 화면을 모른다. OnViewChanged 만 알리고, AVBPlayerController 가 UI.Screen.Shop 을 열고 내린다(설계 13.1절 1번).
 */
UCLASS(ClassGroup=(Vowbound))
class VOWBOUND_API UVBShopComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBShopComponent();

	// 서버에서만. 이미 열려 있거나 경제 설정이 없으면 false. Merchant 는 상인(사라지면 거래가 닫힌다) - 없어도 된다.
	bool OpenShop(const UVBShopDefinition* Shop, AActor* Merchant);

	// 서버에서만. 열려 있지 않으면 아무것도 안 한다.
	void CloseShop();

	bool IsShopOpen() const { return ServerShop != nullptr; }

	// 표시가 부른다. 판단은 서버가 다시 한다.
	void RequestBuy(const UVBItemDefinition* Item, int32 Count);
	void RequestClose();

	// 주인 클라에서 그릴 것.
	const FVBShopView& GetView() const { return View; }

	// 주인 클라에서 View 가 바뀌었을 때(열림·거래 뒤·닫힘). PC 는 이것으로 화면을 여닫고, 화면은 이것으로 다시 그린다.
	FVBOnShopViewChanged OnViewChanged;

	// 화면의 사기 버튼이 쓴다(표시용 - 결정은 ServerBuy). 이 주인 클라에서 보이는 수로 판정을 만든다.
	EVBPurchaseResult PreviewPurchase(const FVBShopViewEntry& ViewEntry, int32 Count) const;

	// 돈으로 쓰는 아이템(경제 설정). 화면이 가진 돈을 보이는 데 쓴다. 설정이 없으면 nullptr.
	static const UVBItemDefinition* GetCurrency();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION(Server, Reliable)
	void ServerBuy(const UVBShopDefinition* Shop, const UVBItemDefinition* Item, int32 Count);

	UFUNCTION(Server, Reliable)
	void ServerClose();

	UFUNCTION(Client, Reliable)
	void ClientShowShop(const FVBShopView& InView);

	UFUNCTION(Client, Reliable)
	void ClientCloseShop();

	// 서버: 지금 상점의 보기를 계산해 주인 클라에 보낸다.
	void SendView();

	// 서버: 요청이 지금 열린 상점에 대한 것이고 상인이 아직 있나. 아니면 버린다(상인이 사라졌으면 닫는다).
	bool ValidateRequest(const UVBShopDefinition* Shop, const TCHAR* What);

	// 서버: 지금 상점에서 이 플레이어에게 걸리는 배율(경제 전역 규칙 x 상점 규칙). 줄마다 같으므로 한 번 센다.
	float ComputeMultiplier(const UVBEconomyConfig& Economy, const FVBRuleContext& Context) const;

	FVBRuleContext MakeRuleContext() const;

	UPROPERTY()
	TObjectPtr<const UVBShopDefinition> ServerShop;

	TWeakObjectPtr<AActor> ServerMerchant;

	// 주인 클라의 그릴 것. 리슨 서버 호스트에서는 서버 상태와 한 객체에 같이 산다 - 그래서 둘을 따로 둔다.
	UPROPERTY()
	FVBShopView View;
};
