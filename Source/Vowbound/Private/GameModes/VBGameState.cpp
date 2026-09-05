// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "GameModes/VBGameState.h"
#include "Net/UnrealNetwork.h"

AVBGameState::AVBGameState()
{
	bReplicates = true;
}

void AVBGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME_CONDITION_NOTIFY(AVBGameState, CurrentMatchPhase, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AVBGameState, RemainingTimeSec, COND_None, REPNOTIFY_Always);
}

void AVBGameState::SetCurrentMatchPhase(EVBMatchPhase NewPhase)
{
	if (!HasAuthority()) { return; }
	
	if (CurrentMatchPhase == NewPhase)
	{
		return;
	}
	
	const EVBMatchPhase OldPhase = CurrentMatchPhase;
	CurrentMatchPhase = NewPhase;
	
	// 서버에서도 동일 경로로 처리하고 싶을 때 사용
	OnRep_CurrentMatchPhase(OldPhase);
}

void AVBGameState::SetRemainingTimeSec(int32 NewTimeSec)
{
	if (!HasAuthority()) { return; }
	
	if (RemainingTimeSec == NewTimeSec)
	{
		return;
	}
	
	const int32 OldTimeSec = RemainingTimeSec;
	RemainingTimeSec = NewTimeSec;
	
	// 서버에서도 동일 경로로 처리하고 싶을 때 사용
	OnRep_RemainingTimeSec(OldTimeSec);
}

void AVBGameState::OnRep_CurrentMatchPhase(EVBMatchPhase OldPhase)
{
	OnMatchPhaseChanged.Broadcast(CurrentMatchPhase, OldPhase);
}

void AVBGameState::OnRep_RemainingTimeSec(int32 OldTimeSec)
{
	OnRemainingTimeChanged.Broadcast(RemainingTimeSec, OldTimeSec);
}
