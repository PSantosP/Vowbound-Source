// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "SaveSystem/VBSaveHold.h"

bool FVBSaveHoldSet::AdvanceGeneration(FGuid NewGeneration)
{
	if (!NewGeneration.IsValid() || NewGeneration == Generation) return false;
	Generation = NewGeneration;
	Holds.Reset();
	return true;
}

FVBSaveHoldToken FVBSaveHoldSet::Acquire(FName Reason)
{
	if (!Generation.IsValid() || Reason.IsNone()) return {};
	const FVBSaveHoldToken Token{FGuid::NewGuid(), Generation};
	if (!Token.IsValid() || Holds.Contains(Token.HoldId)) return {};
	Holds.Add(Token.HoldId, Reason);
	return Token;
}

bool FVBSaveHoldSet::Release(const FVBSaveHoldToken& Token)
{
	return Token.IsValid() && Token.Generation == Generation && Holds.Remove(Token.HoldId) > 0;
}

bool FVBSaveHoldSet::Contains(const FVBSaveHoldToken& Token) const
{
	return Token.IsValid() && Token.Generation == Generation && Holds.Contains(Token.HoldId);
}
