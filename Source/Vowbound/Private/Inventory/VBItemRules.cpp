// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBItemRules.h"

#include "GameFramework/PlayerState.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Vowbound/Vowbound.h"

namespace
{
	UVBInventoryComponent* FindInventory(const FVBRuleContext& Context, const UVBItemDefinition* Item, const TCHAR* What)
	{
		UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(Context.Instigator);
		if (!Inventory || !Item)
		{
			VB_LOG(Warning, "%s(%s): %s", What, *GetNameSafe(Item), !Item ? TEXT("아이템이 비었다") : TEXT("부른 플레이어에게 인벤토리가 없다"));
			return nullptr;
		}
		return Inventory;
	}
}

void FVBEffect_GiveItem::Apply(const FVBRuleContext& Context) const
{
	if (UVBInventoryComponent* Inventory = FindInventory(Context, Item, TEXT("효과 아이템 주기")))
	{
		Inventory->AddItem(Item, Count);
	}
}

void FVBEffect_TakeItem::Apply(const FVBRuleContext& Context) const
{
	if (UVBInventoryComponent* Inventory = FindInventory(Context, Item, TEXT("효과 아이템 가져가기")))
	{
		if (!Inventory->RemoveItem(Item, Count))
		{
			VB_LOG(Log, "효과 아이템 가져가기(%s x%d): 모자라서 아무것도 안 가져갔다", *GetNameSafe(Item), Count);
		}
	}
}

bool FVBCondition_ItemCount::Evaluate(const FVBRuleContext& Context) const
{
	const UVBInventoryComponent* Inventory = FindInventory(Context, Item, TEXT("조건 아이템 개수"));
	// 읽을 곳이 없으면 거짓 - 알 수 없는 것을 참으로 흘리지 않는다(VBRules 규약).
	return Inventory && VBRules::Compare(Compare, Inventory->GetCount(Item), Count);
}
