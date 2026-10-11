// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Story/VBStoryState.h"

bool FVBStoryState::HasFact(FGameplayTag Fact) const
{
	return Fact.IsValid() && Facts.HasTagExact(Fact);
}

bool FVBStoryState::SetFact(FGameplayTag Fact, bool bOn)
{
	if (!Fact.IsValid() || HasFact(Fact) == bOn)
	{
		return false;
	}
	if (bOn)
	{
		Facts.AddTag(Fact);
	}
	else
	{
		Facts.RemoveTag(Fact);
	}
	return true;
}

int32 FVBStoryState::GetCounter(FGameplayTag Counter) const
{
	const FVBStoryCounter* Found = Counters.FindByPredicate([Counter](const FVBStoryCounter& C) { return C.Tag == Counter; });
	return Found ? Found->Value : 0;
}

int32 FVBStoryState::AddToCounter(FGameplayTag Counter, int32 Delta)
{
	if (!Counter.IsValid() || Delta == 0)
	{
		return GetCounter(Counter);
	}
	const int32 Index = Counters.IndexOfByPredicate([Counter](const FVBStoryCounter& C) { return C.Tag == Counter; });
	const int32 NewValue = (Index == INDEX_NONE ? 0 : Counters[Index].Value) + Delta;
	if (NewValue == 0)
	{
		if (Index != INDEX_NONE)
		{
			Counters.RemoveAt(Index);
		}
	}
	else if (Index == INDEX_NONE)
	{
		FVBStoryCounter& Added = Counters.AddDefaulted_GetRef();
		Added.Tag = Counter;
		Added.Value = NewValue;
	}
	else
	{
		Counters[Index].Value = NewValue;
	}
	return NewValue;
}

bool FVBStoryState::operator==(const FVBStoryState& Other) const
{
	if (!Facts.HasAllExact(Other.Facts) || !Other.Facts.HasAllExact(Facts) || Counters.Num() != Other.Counters.Num())
	{
		return false;
	}
	for (const FVBStoryCounter& C : Counters)
	{
		if (Other.GetCounter(C.Tag) != C.Value)
		{
			return false;
		}
	}
	return true;
}
