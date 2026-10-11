// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FVBSaveHoldToken
{
	FGuid HoldId;
	FGuid Generation;

	bool IsValid() const { return HoldId.IsValid() && Generation.IsValid(); }
};

// GI 주인이 새 게임/성공 로드에서만 세대를 바꾼다. World/Pawn 수명과 자동으로 묶지 않는다.
struct VOWBOUND_API FVBSaveHoldSet
{
	bool AdvanceGeneration(FGuid NewGeneration);
	FVBSaveHoldToken Acquire(FName Reason);
	bool Release(const FVBSaveHoldToken& Token);
	bool Contains(const FVBSaveHoldToken& Token) const;

	bool IsHeld() const { return !Holds.IsEmpty(); }
	int32 Num() const { return Holds.Num(); }
	FGuid GetGeneration() const { return Generation; }

private:
	FGuid Generation;
	TMap<FGuid, FName> Holds;
};
