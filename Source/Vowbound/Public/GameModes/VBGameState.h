// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CoreLoop/VBMatchState.h"
#include "VBGameState.generated.h"

class UVBStoryStateComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVBOnMatchPhaseChanged, EVBMatchPhase, NewPhase, EVBMatchPhase, OldPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVBOnRemainingTimeChanged, int32, NewTimeSec, int32, OldTimeSec);

/**
 * Vowbound GameState. 매치 시간, 전역 상태를 관리하고 복제한다.
 */
UCLASS()
class VOWBOUND_API AVBGameState : public AGameStateBase
{
	GENERATED_BODY()
	
public:
	AVBGameState();
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	EVBMatchPhase GetCurrentMatchPhase() const { return CurrentMatchPhase; }
	int32         GetRemainingTimeSec() const { return RemainingTimeSec; }
	
	void SetCurrentMatchPhase(EVBMatchPhase NewPhase);
	void SetRemainingTimeSec(int32 NewTimeSec);

	UPROPERTY(BlueprintAssignable, Category="Vowbound|CoreLoop")
	FVBOnMatchPhaseChanged OnMatchPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category="Vowbound|CoreLoop")
	FVBOnRemainingTimeChanged OnRemainingTimeChanged;
	
protected:
	UPROPERTY(ReplicatedUsing=OnRep_CurrentMatchPhase, BlueprintReadOnly, Category="Vowbound|CoreLoop")
	EVBMatchPhase CurrentMatchPhase = EVBMatchPhase::None;
	
	UPROPERTY(ReplicatedUsing=OnRep_RemainingTimeSec, BlueprintReadOnly, Category="Vowbound|CoreLoop")
	int32 RemainingTimeSec = 0;
	
	UFUNCTION()
	void OnRep_CurrentMatchPhase(EVBMatchPhase OldPhase);
	
	UFUNCTION()
	void OnRep_RemainingTimeSec(int32 OldTimeSec);

	// 세계 사실·카운터의 주인(설계 .sw/narrative 2.1절). 읽기는 UVBStoryStateComponent::Get(월드 문맥) - 이 클래스를 몰라도 된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	TObjectPtr<UVBStoryStateComponent> StoryState;
};
