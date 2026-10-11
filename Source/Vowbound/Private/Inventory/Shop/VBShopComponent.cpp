// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Shop/VBShopComponent.h"

#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/Shop/VBEconomyConfig.h"
#include "Inventory/Shop/VBEconomySettings.h"
#include "Inventory/Shop/VBShopDefinition.h"
#include "Inventory/Shop/VBShopStateSubsystem.h"
#include "Vowbound/Vowbound.h"

const FVBShopEntry* FVBShopView::GetEntry(const FVBShopViewEntry& ViewEntry) const
{
	return Shop && Shop->Entries.IsValidIndex(ViewEntry.EntryIndex) ? &Shop->Entries[ViewEntry.EntryIndex] : nullptr;
}

UVBShopComponent::UVBShopComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// 서버 -> 주인 클라 RPC 를 쓰려면 컴포넌트가 복제돼야 한다. 복제하는 속성은 없다(대화 컴포넌트와 같다).
	SetIsReplicatedByDefault(true);
}

bool UVBShopComponent::OpenShop(const UVBShopDefinition* Shop, AActor* Merchant)
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		VB_LOG(Warning, "상점 열기를 권한 없는 쪽에서 불렀다 - 무시한다(거래는 서버만)");
		return false;
	}
	if (!Shop)
	{
		VB_LOG(Warning, "상점 열기: 상점 정의가 비었다(%s)", *GetNameSafe(Merchant));
		return false;
	}
	if (IsShopOpen())
	{
		VB_LOG(Log, "상점 열기 거절: 이미 %s 가 열려 있다", *GetNameSafe(ServerShop));
		return false;
	}
	const UVBEconomyConfig* Economy = UVBEconomySettings::LoadEconomy();
	if (!Economy || !Economy->Currency)
	{
		// 돈을 모르면 값도 거래도 판정할 수 없다. 열어 두면 사기 버튼이 전부 꺼진 상점이 되므로 아예 열지 않는다.
		VB_LOG(Warning, "상점 열기 거절(%s): 경제 설정에 돈 아이템이 없다", *GetNameSafe(Shop));
		return false;
	}
	ServerShop = Shop;
	ServerMerchant = Merchant;
	VB_LOG(Log, "상점 열림: %s (상인 %s)", *GetNameSafe(Shop), *GetNameSafe(Merchant));
	SendView();
	return true;
}

void UVBShopComponent::CloseShop()
{
	if (!IsShopOpen())
	{
		return;
	}
	VB_LOG(Log, "상점 닫힘: %s", *GetNameSafe(ServerShop));
	ServerShop = nullptr;
	ServerMerchant.Reset();
	ClientCloseShop();
}

void UVBShopComponent::RequestBuy(const UVBItemDefinition* Item, int32 Count)
{
	if (View.IsActive())
	{
		ServerBuy(View.Shop, Item, Count);
	}
}

void UVBShopComponent::RequestClose()
{
	if (View.IsActive())
	{
		ServerClose();
	}
}

EVBPurchaseResult UVBShopComponent::PreviewPurchase(const FVBShopViewEntry& ViewEntry, int32 Count) const
{
	const FVBShopEntry* Entry = View.GetEntry(ViewEntry);
	const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	const UVBItemDefinition* Currency = GetCurrency();
	if (!Entry || !Entry->Item || !Inventory || !Currency)
	{
		return EVBPurchaseResult::InvalidRequest;
	}
	FVBPurchaseQuote Quote;
	Quote.Count = Count;
	Quote.UnitPrice = ViewEntry.UnitPrice;
	Quote.Remaining = ViewEntry.Remaining;
	Quote.bAvailable = ViewEntry.bAvailable;
	Quote.CurrencyHeld = Inventory->GetCount(Currency);
	Quote.AddableCount = Entry->Item->ClampToMax(Inventory->GetCount(Entry->Item), Count);
	return VBShop::CheckPurchase(Quote);
}

const UVBItemDefinition* UVBShopComponent::GetCurrency()
{
	const UVBEconomyConfig* Economy = UVBEconomySettings::LoadEconomy();
	return Economy ? Economy->Currency.Get() : nullptr;
}

void UVBShopComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 레벨을 떠나거나 PC 가 사라진다. 주인 클라에 보낼 곳이 없으니 상태만 비운다 - 화면은 레벨과 함께 내려간다.
	ServerShop = nullptr;
	ServerMerchant.Reset();
	View = FVBShopView();
	Super::EndPlay(EndPlayReason);
}

void UVBShopComponent::ServerBuy_Implementation(const UVBShopDefinition* Shop, const UVBItemDefinition* Item, int32 Count)
{
	if (!ValidateRequest(Shop, TEXT("사기")))
	{
		return;
	}
	const FVBShopEntry* Entry = ServerShop->FindEntry(Item);
	if (!Entry)
	{
		VB_LOG(Warning, "상점 사기 거절: %s 에 %s 줄이 없다", *GetNameSafe(ServerShop), *GetNameSafe(Item));
		return;
	}
	const UVBEconomyConfig* Economy = UVBEconomySettings::LoadEconomy();
	UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	UVBShopStateSubsystem* State = UVBShopStateSubsystem::Get(this);
	if (!Economy || !Economy->Currency || !Inventory || !State)
	{
		// 판 수를 셀 곳이 없으면 재고 한정 줄이 무한이 된다 - 모르는 상태를 통과로 떨어뜨리지 않는다.
		VB_LOG(Warning, "상점 사기 거절: %s", !Inventory ? TEXT("이 플레이어의 인벤토리가 없다")
		       : !State ? TEXT("재고 서브시스템이 없다") : TEXT("경제 설정에 돈 아이템이 없다"));
		return;
	}
	const UVBItemDefinition* Currency = Economy->Currency;
	const FVBRuleContext Context = MakeRuleContext();

	// 화면이 켠 버튼을 믿지 않는다 - 조건·값·재고·돈·자리를 여기서 다시 본다.
	FVBPurchaseQuote Quote;
	Quote.Count = Count;
	Quote.UnitPrice = VBShop::UnitPrice(Entry->GetBasePrice(), ComputeMultiplier(*Economy, Context));
	Quote.Remaining = Entry->GetRemaining(State->GetSold(ServerShop->ShopId, Item->ItemId));
	Quote.bAvailable = VBRules::EvaluateAll(Entry->Conditions, Context);
	Quote.CurrencyHeld = Inventory->GetCount(Currency);
	Quote.AddableCount = Item->ClampToMax(Inventory->GetCount(Item), Count);
	const EVBPurchaseResult Result = VBShop::CheckPurchase(Quote);
	if (Result != EVBPurchaseResult::Ok)
	{
		VB_LOG(Log, "상점 사기 거절: %s x%d - %s", *GetNameSafe(Item), Count, *UEnum::GetValueAsString(Result));
		// 화면이 본 값이 낡았을 수 있다(그 사이 재고·조건이 바뀜) - 지금 값으로 다시 그리게 한다.
		SendView();
		return;
	}

	// 바꾸기. 위 판정이 돈(가진 수 >= 합)과 자리(넣을 수 == Count)를 확인했으므로 둘 다 실패하지 않아야 한다.
	const int32 Total = Quote.UnitPrice * Count;
	if (!Inventory->RemoveItem(Currency, Total))
	{
		VB_LOG(Error, "상점 사기: 판정은 통과했는데 돈 %d 를 빼지 못했다 - 거래를 하지 않는다", Total);
		return;
	}
	const int32 Added = Inventory->AddItem(Item, Count);
	if (Added < Count)
	{
		// 판정과 넣기 사이에 상한이 바뀐 경우뿐이다. 못 넣은 만큼 돈을 돌려준다 - 돈만 빠진 거래를 남기지 않는다.
		VB_LOG(Error, "상점 사기: %s 를 %d 개 중 %d 개만 넣었다 - 나머지 값 %d 를 돌려준다",
		       *GetNameSafe(Item), Count, Added, (Count - Added) * Quote.UnitPrice);
		Inventory->AddItem(Currency, (Count - Added) * Quote.UnitPrice);
	}
	if (Added > 0)
	{
		State->AddSold(ServerShop->ShopId, Item->ItemId, Added);
	}
	VB_LOG(Log, "상점 사기: %s x%d, 단가 %d (%s)", *GetNameSafe(Item), Added, Quote.UnitPrice, *GetNameSafe(ServerShop));
	SendView();
}

void UVBShopComponent::ServerClose_Implementation()
{
	CloseShop();
}

void UVBShopComponent::ClientShowShop_Implementation(const FVBShopView& InView)
{
	View = InView;
	if (!View.IsActive())
	{
		VB_LOG(Warning, "상점 표시: 상점이 빈 보기를 받았다 - 빈 화면이 된다");
	}
	// 화면을 여는 것은 이 컴포넌트가 아니라 PC 다(AVBPlayerController 가 이 알림을 듣는다).
	OnViewChanged.Broadcast();
}

void UVBShopComponent::ClientCloseShop_Implementation()
{
	View = FVBShopView();
	OnViewChanged.Broadcast();
}

void UVBShopComponent::SendView()
{
	const UVBEconomyConfig* Economy = UVBEconomySettings::LoadEconomy();
	const UVBShopStateSubsystem* State = UVBShopStateSubsystem::Get(this);
	if (!ServerShop || !Economy || !State)
	{
		VB_LOG(Warning, "상점 보기: 상점·경제 설정·재고 중 없는 것이 있다 - 거래를 닫는다");
		CloseShop();
		return;
	}
	const FVBRuleContext Context = MakeRuleContext();
	const float Multiplier = ComputeMultiplier(*Economy, Context);

	FVBShopView NewView;
	NewView.Shop = ServerShop;
	for (int32 Index = 0; Index < ServerShop->Entries.Num(); ++Index)
	{
		const FVBShopEntry& Entry = ServerShop->Entries[Index];
		if (!Entry.Item)
		{
			continue;
		}
		const bool bAvailable = VBRules::EvaluateAll(Entry.Conditions, Context);
		if (!bAvailable && !Entry.bShowWhenUnavailable)
		{
			continue;
		}
		FVBShopViewEntry& ViewEntry = NewView.Entries.AddDefaulted_GetRef();
		ViewEntry.EntryIndex = Index;
		ViewEntry.UnitPrice = VBShop::UnitPrice(Entry.GetBasePrice(), Multiplier);
		ViewEntry.Remaining = Entry.GetRemaining(State->GetSold(ServerShop->ShopId, Entry.Item->ItemId));
		ViewEntry.bAvailable = bAvailable;
	}
	ClientShowShop(NewView);
}

bool UVBShopComponent::ValidateRequest(const UVBShopDefinition* Shop, const TCHAR* What)
{
	if (!IsShopOpen())
	{
		// 닫힌 직후에 도착한 요청(닫기 버튼을 두 번 누름). 정상이라 조용히 버린다.
		VB_LOG(Verbose, "상점 %s 요청: 열린 상점이 없다 - 버린다", What);
		return false;
	}
	if (ServerMerchant.IsStale())
	{
		VB_LOG(Log, "상점 %s 요청: 상인이 사라졌다 - 거래를 닫는다", What);
		CloseShop();
		return false;
	}
	if (Shop != ServerShop)
	{
		VB_LOG(Log, "상점 %s 요청: 지난 상점 %s 에 대한 요청(지금 %s) - 버린다", What, *GetNameSafe(Shop), *GetNameSafe(ServerShop));
		return false;
	}
	return true;
}

float UVBShopComponent::ComputeMultiplier(const UVBEconomyConfig& Economy, const FVBRuleContext& Context) const
{
	return VBShop::PriceMultiplier(Economy.PriceRules, Context) * (ServerShop ? VBShop::PriceMultiplier(ServerShop->PriceRules, Context) : 1.0f);
}

FVBRuleContext UVBShopComponent::MakeRuleContext() const
{
	const AController* Controller = Cast<AController>(GetOwner());
	FVBRuleContext Context = FVBRuleContext::Make(this, Controller ? Controller->PlayerState.Get() : nullptr);
	Context.Counterpart = ServerMerchant.Get();
	return Context;
}
