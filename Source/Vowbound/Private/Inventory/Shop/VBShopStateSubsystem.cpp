// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Shop/VBShopStateSubsystem.h"

#include "Engine/World.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Vowbound/Vowbound.h"

UVBShopStateSubsystem* UVBShopStateSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UVBShopStateSubsystem>() : nullptr;
}

void UVBShopStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 등록부가 먼저 서야 여기서 등록된다 - 월드 서브시스템끼리의 초기화 순서는 정해져 있지 않다(UVBInteractableSubsystem 과 같다).
	Collection.InitializeDependency<UVBSaveParticipantSubsystem>();
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBShopStateSubsystem::Deinitialize()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::Deinitialize();
}

int32 UVBShopStateSubsystem::GetSold(FGameplayTag ShopId, FGameplayTag ItemId) const
{
	const int32* Found = Sold.Find(FStockKey{ShopId, ItemId});
	return Found ? *Found : 0;
}

void UVBShopStateSubsystem::AddSold(FGameplayTag ShopId, FGameplayTag ItemId, int32 Count)
{
	if (!HasWriteAuthority())
	{
		VB_LOG(Warning, "상점 재고: 클라에서 판 수를 바꾸려 했다(%s / %s) - 무시한다(쓰기는 서버만)", *ShopId.ToString(), *ItemId.ToString());
		return;
	}
	if (!ShopId.IsValid() || !ItemId.IsValid() || Count <= 0)
	{
		VB_LOG(Warning, "상점 재고: 열쇠가 비었거나 수가 0 이하다(%s / %s x%d) - 기록하지 않는다", *ShopId.ToString(), *ItemId.ToString(), Count);
		return;
	}
	Sold.FindOrAdd(FStockKey{ShopId, ItemId}) += Count;
}

void UVBShopStateSubsystem::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	Save.Shops.Sales.Reset(Sold.Num());
	for (const TPair<FStockKey, int32>& Pair : Sold)
	{
		FVBSavedShopSale& Sale = Save.Shops.Sales.AddDefaulted_GetRef();
		Sale.ShopId = Pair.Key.ShopId;
		Sale.ItemId = Pair.Key.ItemId;
		Sale.Sold = Pair.Value;
	}
}

void UVBShopStateSubsystem::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	Sold.Reset();
	for (const FVBSavedShopSale& Sale : Save.Shops.Sales)
	{
		// 태그가 지워졌으면(리다이렉트 없이 이름이 바뀐 상점·아이템) 그 기록은 갈 곳이 없다 - 소리 내고 버린다.
		if (!Sale.ShopId.IsValid() || !Sale.ItemId.IsValid() || Sale.Sold <= 0)
		{
			VB_LOG(Warning, "상점 재고 불러오기: 읽을 수 없는 기록(%s / %s x%d) - 버린다", *Sale.ShopId.ToString(), *Sale.ItemId.ToString(), Sale.Sold);
			continue;
		}
		Sold.FindOrAdd(FStockKey{Sale.ShopId, Sale.ItemId}) += Sale.Sold;
	}
}

bool UVBShopStateSubsystem::HasWriteAuthority() const
{
	// 월드가 없으면(단위 시험) 쓴다 - 규칙 문맥(FVBRuleContext::CanApplyEffects)과 같은 기준.
	const UWorld* World = GetWorld();
	return !World || World->GetNetMode() != NM_Client;
}
