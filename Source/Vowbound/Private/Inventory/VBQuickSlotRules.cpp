// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBQuickSlotRules.h"

#include "Inventory/VBItemDefinition.h"

bool VBQuickSlot::CanAssign(const UVBItemDefinition* Item)
{
	return !Item || Item->FindFragment<FVBItemFragment_Usable>() != nullptr;
}

EVBQuickSlotAssignResult VBQuickSlot::Assign(TArray<TObjectPtr<const UVBItemDefinition>>& Slots, int32 SlotIndex, const UVBItemDefinition* Item)
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return EVBQuickSlotAssignResult::OutOfRange;
	}
	if (!CanAssign(Item))
	{
		return EVBQuickSlotAssignResult::NotUsable;
	}
	if (Slots[SlotIndex] == Item)
	{
		return EVBQuickSlotAssignResult::Unchanged;
	}
	// 다른 칸에 있던 같은 아이템을 먼저 비운다 - 칸 둘이 같은 아이템을 가리키면 «어느 키로 쓰나» 가 두 답이 된다.
	if (Item)
	{
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (Index != SlotIndex && Slots[Index] == Item)
			{
				Slots[Index] = nullptr;
			}
		}
	}
	Slots[SlotIndex] = Item;
	return EVBQuickSlotAssignResult::Changed;
}

const TCHAR* VBQuickSlot::LexToString(EVBQuickSlotAssignResult Result)
{
	switch (Result)
	{
	case EVBQuickSlotAssignResult::Changed:    return TEXT("Changed");
	case EVBQuickSlotAssignResult::Unchanged:  return TEXT("Unchanged");
	case EVBQuickSlotAssignResult::OutOfRange: return TEXT("OutOfRange");
	case EVBQuickSlotAssignResult::NotUsable:  return TEXT("NotUsable");
	}
	return TEXT("Unknown");
}
