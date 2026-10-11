// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "SaveSystem/VBSaveParticipantSubsystem.h"

#include "Engine/World.h"

void UVBSaveParticipantSubsystem::Register(IVBSaveParticipant* Participant)
{
	if (UVBSaveParticipantSubsystem* Registry = FindFor(Participant))
	{
		Registry->Participants.AddUnique(TWeakInterfacePtr<IVBSaveParticipant>(Participant));
	}
}

void UVBSaveParticipantSubsystem::Unregister(IVBSaveParticipant* Participant)
{
	if (UVBSaveParticipantSubsystem* Registry = FindFor(Participant))
	{
		const UObject* Object = Cast<UObject>(Participant);
		Registry->Participants.RemoveAll([Object](const TWeakInterfacePtr<IVBSaveParticipant>& Entry)
		{
			return !Entry.IsValid() || Entry.GetObject() == Object;
		});
	}
}

TArray<IVBSaveParticipant*> UVBSaveParticipantSubsystem::GetParticipants()
{
	Participants.RemoveAll([](const TWeakInterfacePtr<IVBSaveParticipant>& Entry) { return !Entry.IsValid(); });
	TArray<IVBSaveParticipant*> Result;
	Result.Reserve(Participants.Num());
	for (const TWeakInterfacePtr<IVBSaveParticipant>& Entry : Participants)
	{
		Result.Add(Entry.Get());
	}
	return Result;
}

int32 UVBSaveParticipantSubsystem::ReadAll(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	const TArray<IVBSaveParticipant*> Readers = GetParticipants();
	{
		TGuardValue<bool> Applying(bApplyingSave, true);
		for (IVBSaveParticipant* Participant : Readers)
		{
			Participant->ReadSave(Save, Context);
		}
	}
	OnSaveApplied.Broadcast();
	return Readers.Num();
}

UVBSaveParticipantSubsystem* UVBSaveParticipantSubsystem::FindFor(IVBSaveParticipant* Participant)
{
	const UObject* Object = Cast<UObject>(Participant);
	const UWorld* World = Object ? Object->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr;
}
