// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Loot/VBLootContainerSubsystem.h"

#include "Engine/World.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Vowbound/Vowbound.h"

UVBLootContainerSubsystem* UVBLootContainerSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UVBLootContainerSubsystem>() : nullptr;
}

void UVBLootContainerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 등록부가 먼저 서야 여기서 등록된다 - 월드 서브시스템끼리의 초기화 순서는 정해져 있지 않다(UVBShopStateSubsystem 과 같다).
	Collection.InitializeDependency<UVBSaveParticipantSubsystem>();
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBLootContainerSubsystem::Deinitialize()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::Deinitialize();
}

const TArray<FVBSavedItem>* UVBLootContainerSubsystem::Find(FName Key) const
{
	return Remaining.Find(Key);
}

void UVBLootContainerSubsystem::SetRemaining(FName Key, const TArray<FVBSavedItem>& InRemaining)
{
	if (!HasWriteAuthority())
	{
		VB_LOG(Warning, "서랍장 남은 몫: 클라에서 %s 를 바꾸려 했다 - 무시한다(쓰기는 서버만)", *Key.ToString());
		return;
	}
	if (Key.IsNone())
	{
		VB_LOG(Warning, "서랍장 남은 몫: 키가 비었다 - 기록하지 않는다");
		return;
	}
	Remaining.Add(Key, InRemaining);
}

void UVBLootContainerSubsystem::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	// 덮어쓰지 않고 합친다 - 세이브 객체는 판 내내 같은 것이고(UVBSaveGameSubsystem::GetOrCreateActiveGameSave) 불러오기는 «이어하기» 때만 돈다.
	//   이어하기 뒤 다른 맵으로 넘어가 저장하면 이 월드의 목록에는 앞 맵 서랍장이 없다. 비우고 쓰면 그 기록이 지워져 돌아갔을 때 다시 굴린다(복제).
	//   상호작용 상태(CollectSavedStates)와 같은 판단이다. 기록은 지워지는 일이 없어서(굴린 뒤로는 늘 있다) 합치기만으로 맞다.
	TArray<FVBSavedLootContainer>& Out = Save.LootContainers.Containers;
	for (const TPair<FName, TArray<FVBSavedItem>>& Pair : Remaining)
	{
		FVBSavedLootContainer* Row = Out.FindByPredicate([&Pair](const FVBSavedLootContainer& Saved) { return Saved.Key == Pair.Key; });
		if (!Row)
		{
			Row = &Out.AddDefaulted_GetRef();
			Row->Key = Pair.Key;
		}
		Row->Remaining = Pair.Value;
	}
}

void UVBLootContainerSubsystem::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	Remaining.Reset();
	for (const FVBSavedLootContainer& Row : Save.LootContainers.Containers)
	{
		if (Row.Key.IsNone())
		{
			VB_LOG(Warning, "서랍장 남은 몫 불러오기: 키가 빈 기록 - 버린다(어느 서랍장의 것인지 알 수 없다)");
			continue;
		}
		// 빈 태그·0 개 줄만 거른다. 못 찾는 태그는 지킨다 - 되찾기는 줄 때 하고, 못 찾은 줄은 남는다(VBLoot::GiveStacks).
		TArray<FVBSavedItem>& Rows = Remaining.Add(Row.Key);
		for (const FVBSavedItem& Item : Row.Remaining)
		{
			if (Item.ItemId.IsValid() && Item.Count > 0)
			{
				Rows.Add(Item);
			}
			else
			{
				VB_LOG(Warning, "서랍장 남은 몫 불러오기: %s 의 읽을 수 없는 줄(%s x%d) - 버린다", *Row.Key.ToString(), *Item.ItemId.ToString(), Item.Count);
			}
		}
	}
	VB_LOG(Log, "서랍장 남은 몫 불러오기: 서랍장 %d 곳", Remaining.Num());
	OnContentsLoaded.Broadcast();
}

bool UVBLootContainerSubsystem::HasWriteAuthority() const
{
	// 월드가 없으면(단위 시험) 쓴다 - 규칙 문맥(FVBRuleContext::CanApplyEffects)과 같은 기준.
	const UWorld* World = GetWorld();
	return !World || World->GetNetMode() != NM_Client;
}
