// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Story/VBGameEventSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Vowbound/Vowbound.h"

UVBGameEventSubsystem* UVBGameEventSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UVBGameEventSubsystem>() : nullptr;
}

void UVBGameEventSubsystem::Broadcast(const FVBGameEvent& Event)
{
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		VB_LOG(Warning, "사건 %s 을 클라에서 방송하려 했다 - 버린다(사건은 서버만)", *Event.EventTag.ToString());
		return;
	}
	if (!Event.EventTag.IsValid())
	{
		VB_LOG(Warning, "태그 없는 사건을 방송하려 했다 - 버린다");
		return;
	}
	VB_LOG(Verbose, "사건 %s 대상 %s x%d", *Event.EventTag.ToString(), *Event.TargetTags.ToStringSimple(), Event.Count);
	OnGameEvent.Broadcast(Event);
}
