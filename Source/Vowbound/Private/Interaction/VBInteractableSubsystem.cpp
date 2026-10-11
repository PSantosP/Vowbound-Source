// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBInteractableSubsystem.h"

#include "Interaction/VBInteractableComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Vowbound/Vowbound.h"

void UVBInteractableSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 등록부가 먼저 서야 여기서 등록된다 - 월드 서브시스템끼리의 초기화 순서는 정해져 있지 않다.
	Collection.InitializeDependency<UVBSaveParticipantSubsystem>();
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBInteractableSubsystem::Deinitialize()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::Deinitialize();
}

void UVBInteractableSubsystem::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	CollectSavedStates(Save.Progress.InteractableStates);
}

void UVBInteractableSubsystem::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	ApplySavedStates(Save.Progress.InteractableStates);
}

void UVBInteractableSubsystem::Register(UVBInteractableComponent* Interactable)
{
	if (Interactable)
	{
		Interactables.AddUnique(Interactable);
		ApplyPendingState(Interactable);
	}
}

void UVBInteractableSubsystem::Unregister(const UVBInteractableComponent* Interactable)
{
	Interactables.RemoveAll([Interactable](const TWeakObjectPtr<UVBInteractableComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Interactable;
	});
}

const TArray<TWeakObjectPtr<UVBInteractableComponent>>& UVBInteractableSubsystem::GetInteractables()
{
	Interactables.RemoveAll([](const TWeakObjectPtr<UVBInteractableComponent>& Entry) { return !Entry.IsValid(); });
	return Interactables;
}

void UVBInteractableSubsystem::CollectSavedStates(TMap<FName, bool>& InOutStates)
{
	for (const TWeakObjectPtr<UVBInteractableComponent>& Entry : GetInteractables())
	{
		const FName Key = Entry->GetSaveKey();
		if (!Key.IsNone())
		{
			InOutStates.Add(Key, Entry->IsActivated());
		}
	}
}

void UVBInteractableSubsystem::ApplySavedStates(const TMap<FName, bool>& States)
{
	PendingSavedStates = States;
	int32 Applied = 0;
	for (const TWeakObjectPtr<UVBInteractableComponent>& Entry : GetInteractables())
	{
		if (PendingSavedStates.Contains(Entry->GetSaveKey()))
		{
			ApplyPendingState(Entry.Get());
			++Applied;
		}
	}
	VB_LOG(Log, "상호작용 상태 로드: 세이브 %d건, 지금 떠 있는 대상에 %d건 적용(나머지는 스트리밍으로 들어올 때)",
	       PendingSavedStates.Num(), Applied);
}

void UVBInteractableSubsystem::ApplyPendingState(UVBInteractableComponent* Interactable) const
{
	const FName Key = Interactable ? Interactable->GetSaveKey() : NAME_None;
	if (const bool* Saved = Key.IsNone() ? nullptr : PendingSavedStates.Find(Key))
	{
		// SetActivated 는 서버에서만 동작한다. 같은 값이면 아무 일도 없다.
		Interactable->SetActivated(*Saved, /*bInstant=*/true);
	}
}
