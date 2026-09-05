// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CoreLoop/VBMatchState.h"
#include "VBGameMode.generated.h"

class AVBGameState;

/**
 * Vowbound 기본 GameMode. 매치 규칙과 스폰 정책을 관리한다.
 */
UCLASS()
class VOWBOUND_API AVBGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
public:
	AVBGameMode();
	
protected:
	virtual void BeginPlay() override;

	// 폰 스폰 직후(서버) 호출. Continue 로드면 세이브의 빌드/위치를 새 폰에 적용한다.
	// 왜 여기: OpenLevel 이 월드를 파괴하므로 GI 수명 세이브 서브시스템의 '적용 대기' 플래그를
	//          새 레벨에서 처음 폰이 생기는 이 지점이 소비한다. New Game 은 플래그가 없어 기본 상태 유지.
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	
	// 음수 방지를 위해 ClampMin을 0으로
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|CoreLoop", meta=(ClampMin = "0"))
	int32 WarmupDurationSec = 10;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|CoreLoop", meta=(ClampMin = "0"))
	int32 MatchDurationSec = 300;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|CoreLoop", meta=(ClampMin = "0"))
	int32 PostMatchDurationSec = 10;
	
	// K2_ : C++에서 호출하되 구현은 BP에서 하는 함수 UE 규약
	// BlueprintImplementableEvent : 
	// C++에서 본체 없이 선언만, BP에서 이벤트 그래프로 구현
	UFUNCTION(BlueprintImplementableEvent, Category="Vowbound|CoreLoop")
	void K2_OnMatchPhaseStarted(EVBMatchPhase NewPhase, EVBMatchPhase OldPhase);
	
private:
	// Phase 전환 실행 : Gamet에 값 세팅 + 타이머 시작
	void StartPhase(EVBMatchPhase NewPhase, int32 DurationSec);
	
	// 매 초 호출 : 남은 시간 -1, 0이면 다음 Phase로
	void HandlePhaseTimerTick();
	
	// 현재 Phase -> 다음 Phase 결정
	void AdvancePhase();
	
	// GameState 캐스팅 헬퍼
	AVBGameState* GetVBGameState() const;
	
	// 타이머 핸들 (Clear/Set에 사용)
	FTimerHandle MatchPhaseTimerHandle;
};
