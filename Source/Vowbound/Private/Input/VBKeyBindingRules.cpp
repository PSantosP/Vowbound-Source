// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Input/VBKeyBindingRules.h"

EVBKeyDevice VBKeyBinding::DeviceOf(const FKey& Key)
{
	return Key.IsGamepadKey() ? EVBKeyDevice::Gamepad : EVBKeyDevice::KeyboardMouse;
}

bool VBKeyBinding::CanBind(const FKey& Key, EVBKeyDevice RowDevice)
{
	if (!Key.IsValid() || Key.IsTouch() || DeviceOf(Key) != RowDevice)
	{
		return false;
	}
	// 2D 축과 그 한 축은 시점·이동이다. 1D 축 중 트리거는 버튼처럼 쓰므로 남긴다 - 그래서 축 종류가 아니라 이름으로 막는다.
	static const FKey NotBindable[] = {
		EKeys::Escape,
		EKeys::MouseX, EKeys::MouseY,
		EKeys::Gamepad_LeftX, EKeys::Gamepad_LeftY, EKeys::Gamepad_RightX, EKeys::Gamepad_RightY,
	};
	if (Key.IsAxis2D() || Key.IsAxis3D())
	{
		return false;
	}
	for (const FKey& Blocked : NotBindable)
	{
		if (Key == Blocked)
		{
			return false;
		}
	}
	return true;
}

FKey VBKeyBinding::NormalizeKey(const FKey& Key)
{
	if (Key == EKeys::Gamepad_LeftTrigger)
	{
		return EKeys::Gamepad_LeftTriggerAxis;
	}
	if (Key == EKeys::Gamepad_RightTrigger)
	{
		return EKeys::Gamepad_RightTriggerAxis;
	}
	return Key;
}

TArray<FName> VBKeyBinding::FindConflicts(const TArray<FVBKeyBindingRow>& Rows, FName Editing, EVBKeyDevice Device, const FKey& NewKey)
{
	TArray<FName> Conflicts;
	const FKey Wanted = NormalizeKey(NewKey);
	if (!Wanted.IsValid())
	{
		return Conflicts;
	}
	const FVBKeyBindingRow* EditingRow = Rows.FindByPredicate([Editing, Device](const FVBKeyBindingRow& Row)
	{
		return Row.MappingName == Editing && Row.Device == Device;
	});
	const bool bBackToSharedDefault = EditingRow && NormalizeKey(EditingRow->DefaultKey) == Wanted;
	for (const FVBKeyBindingRow& Row : Rows)
	{
		if (Row.Device != Device || Row.MappingName == Editing || NormalizeKey(Row.Key) != Wanted)
		{
			continue;
		}
		if (bBackToSharedDefault && NormalizeKey(Row.DefaultKey) == Wanted)
		{
			continue;
		}
		Conflicts.AddUnique(Row.MappingName);
	}
	return Conflicts;
}

void VBKeyBinding::OrderByCategory(TArray<FVBKeyBindingRow>& Rows)
{
	TArray<FString> CategoryOrder;
	for (const FVBKeyBindingRow& Row : Rows)
	{
		CategoryOrder.AddUnique(Row.DisplayCategory.ToString());
	}
	Rows.StableSort([&CategoryOrder](const FVBKeyBindingRow& A, const FVBKeyBindingRow& B)
	{
		return CategoryOrder.IndexOfByKey(A.DisplayCategory.ToString()) < CategoryOrder.IndexOfByKey(B.DisplayCategory.ToString());
	});
}
