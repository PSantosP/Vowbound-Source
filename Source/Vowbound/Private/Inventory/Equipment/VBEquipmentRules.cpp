// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Equipment/VBEquipmentRules.h"

#include "Inventory/VBItemDefinition.h"

bool VBEquipment::Fits(const UVBItemDefinition* Item, const FGameplayTag& SlotType)
{
	const FVBItemFragment_Equippable* Equippable = Item ? Item->FindFragment<FVBItemFragment_Equippable>() : nullptr;
	return Equippable && SlotType.IsValid() && Equippable->SlotType.MatchesTagExact(SlotType);
}

EVBEquipResult VBEquipment::Equip(const TArray<FGameplayTag>& Layout, TArray<const UVBItemDefinition*>& InOutEquipped, const UVBItemDefinition* Item,
	int32 RequestedSlot, int32& OutSlot, const UVBItemDefinition*& OutReplaced, int32& OutVacated)
{
	OutSlot = INDEX_NONE;
	OutReplaced = nullptr;
	OutVacated = INDEX_NONE;
	if (!Item || !Item->FindFragment<FVBItemFragment_Equippable>())
	{
		return EVBEquipResult::NotEquippable;
	}
	InOutEquipped.SetNum(Layout.Num());

	int32 Slot = INDEX_NONE;
	if (Layout.IsValidIndex(RequestedSlot))
	{
		Slot = Fits(Item, Layout[RequestedSlot]) ? RequestedSlot : INDEX_NONE;
	}
	else
	{
		// 이미 입은 칸이 있으면 그 칸 그대로다 - 자동 요청이 같은 부적을 다른 장신구 칸으로 옮기지 않게.
		const int32 Current = InOutEquipped.IndexOfByKey(Item);
		if (Current != INDEX_NONE)
		{
			OutSlot = Current;
			return EVBEquipResult::Unchanged;
		}
		for (int32 Index = 0; Index < Layout.Num() && Slot == INDEX_NONE; ++Index)
		{
			Slot = Fits(Item, Layout[Index]) && !InOutEquipped[Index] ? Index : INDEX_NONE;
		}
		for (int32 Index = 0; Index < Layout.Num() && Slot == INDEX_NONE; ++Index)
		{
			Slot = Fits(Item, Layout[Index]) ? Index : INDEX_NONE;
		}
	}
	if (Slot == INDEX_NONE)
	{
		return EVBEquipResult::NoFittingSlot;
	}
	OutSlot = Slot;
	if (InOutEquipped[Slot] == Item)
	{
		return EVBEquipResult::Unchanged;
	}
	const int32 Previous = InOutEquipped.IndexOfByKey(Item);
	if (Previous != INDEX_NONE)
	{
		InOutEquipped[Previous] = nullptr;
		OutVacated = Previous;
	}
	OutReplaced = InOutEquipped[Slot];
	InOutEquipped[Slot] = Item;
	return EVBEquipResult::Changed;
}

const TCHAR* VBEquipment::LexToString(EVBEquipResult Result)
{
	switch (Result)
	{
	case EVBEquipResult::Changed:       return TEXT("바뀜");
	case EVBEquipResult::Unchanged:     return TEXT("그대로");
	case EVBEquipResult::NotEquippable: return TEXT("장비가 아님");
	case EVBEquipResult::NoFittingSlot: return TEXT("맞는 칸 없음");
	}
	return TEXT("?");
}
