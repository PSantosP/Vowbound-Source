// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "CharacterCommon/VBDeathSignalSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

void UVBDeathSignalSubsystem::BroadcastActorDied(AActor* DeadActor)
{
	if (!DeadActor)
	{
		return;
	}
	UWorld* World = DeadActor->GetWorld();
	// 서버에서만 의미가 있다. AI 블랙보드는 서버 권위이고 클라의 BB 를 비워 봐야 다음 복제에 덮인다.
	//   지금 부르는 자리(HandleDeath)는 서버 전용이지만, 그 규율에만 기대지 않는다 - 종전 NotifyActorDied 와 같은 판단.
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	if (UVBDeathSignalSubsystem* Signals = World->GetSubsystem<UVBDeathSignalSubsystem>())
	{
		Signals->ActorDied.Broadcast(DeadActor);
	}
}

FDelegateHandle UVBDeathSignalSubsystem::AddListener(const UObject* Owner, TFunction<void(AActor* DeadActor)> Callback)
{
	if (!Owner || !Callback)
	{
		return FDelegateHandle();
	}
	return ActorDied.AddWeakLambda(Owner, MoveTemp(Callback));
}

void UVBDeathSignalSubsystem::RemoveListener(FDelegateHandle Handle)
{
	if (Handle.IsValid())
	{
		ActorDied.Remove(Handle);
	}
}

void UVBDeathSignalSubsystem::Deinitialize()
{
	// 월드가 내려간다. 구독자가 EndPlay 에서 못 뗀 것이 있어도 여기서 끝난다.
	ActorDied.Clear();
	Super::Deinitialize();
}
