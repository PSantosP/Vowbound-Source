// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Loot/VBLootRules.h"

#include "GameFramework/PlayerState.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBWorldItem.h"
#include "Inventory/Loot/VBLootTable.h"
#include "Math/RandomStream.h"
#include "Vowbound/Vowbound.h"

TArray<FVBLootDrop> VBLoot::Roll(const UVBLootTable& Table, const FVBRuleContext& Context, FRandomStream& Stream)
{
	TArray<FVBLootDrop> Drops;
	for (const FVBLootEntry& Entry : Table.Entries)
	{
		if (!Entry.Item || !VBRules::EvaluateAll(Entry.Conditions, Context))
		{
			continue;
		}
		// FRand 는 [0, 1) - 확률 1 은 늘 나오고 0 은 한 번도 안 나온다.
		if (Stream.FRand() >= Entry.Chance)
		{
			continue;
		}
		// 범위가 뒤집힌 저작(IsDataValid 오류)도 실행 때는 최소값 하나로 굴러가게 한다 - 0 개나 음수가 나가지 않게.
		const int32 Low = FMath::Max(1, Entry.MinCount);
		const int32 Count = Stream.RandRange(Low, FMath::Max(Low, Entry.MaxCount));
		if (FVBLootDrop* Existing = Drops.FindByPredicate([&Entry](const FVBLootDrop& Drop) { return Drop.Item == Entry.Item; }))
		{
			Existing->Count += Count;
		}
		else
		{
			Drops.Add({Entry.Item.Get(), Count});
		}
	}
	return Drops;
}

int32 VBLoot::GiveStacks(TArray<FVBSavedItem>& InOutRemaining, TFunctionRef<int32(const FVBSavedItem&)> Give)
{
	// 앞 줄부터 넣는다 - 자리를 다투면(같은 상한을 나눠 쓰는 줄) 표에서 먼저 나온 줄이 먼저 들어간다. 지우기는 다 넣은 뒤 한 번에.
	int32 Total = 0;
	for (FVBSavedItem& Row : InOutRemaining)
	{
		if (Row.Count > 0)
		{
			const int32 Given = FMath::Clamp(Give(Row), 0, Row.Count);
			Row.Count -= Given;
			Total += Given;
		}
	}
	InOutRemaining.RemoveAll([](const FVBSavedItem& Row) { return Row.Count <= 0; });
	return Total;
}

void FVBEffect_GiveLoot::Apply(const FVBRuleContext& Context) const
{
	UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(Context.Instigator);
	if (!Table || !Inventory)
	{
		VB_LOG(Warning, "효과 보상표 주기(%s): %s", *GetNameSafe(Table), !Table ? TEXT("표가 비었다") : TEXT("부른 플레이어에게 인벤토리가 없다"));
		return;
	}

	FRandomStream Stream(FMath::Rand());
	const TArray<FVBLootDrop> Drops = VBLoot::Roll(*Table, Context, Stream);
	FString Summary;
	int32 DropIndex = 0;
	for (const FVBLootDrop& Drop : Drops)
	{
		// 상한에 걸리면 들어간 만큼만 들어간다(인벤토리 규칙) - 로그에 «넣은 수/나온 수» 로 남긴다.
		const int32 Added = Inventory->AddItem(Drop.Item, Drop.Count);
		Summary += FString::Printf(TEXT(" %s %d/%d"), *GetNameSafe(Drop.Item), Added, Drop.Count);
		const int32 Left = Drop.Count - Added;
		if (Left <= 0)
		{
			continue;
		}
		if (!Context.Counterpart)
		{
			VB_LOG(Warning, "보상표 %s: %s %d 개가 넘쳤는데 놓을 자리(상대)가 없다 - 버린다", *GetNameSafe(Table), *GetNameSafe(Drop.Item), Left);
			continue;
		}
		// 실패 이유(설정 빔·서버 아님)는 SpawnDrop 이 경고한다.
		AVBWorldItem::SpawnDrop(Context.Counterpart, Drop.Item, Left, DropIndex++);
	}
	VB_LOG(Log, "보상표 %s -> %s:%s", *GetNameSafe(Table), *GetNameSafe(Context.Instigator),
	       Drops.IsEmpty() ? TEXT(" 나온 것 없음") : *Summary);
}
