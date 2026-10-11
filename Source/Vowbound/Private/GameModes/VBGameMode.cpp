// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "GameModes/VBGameMode.h"
#include "TimerManager.h"
#include "GameModes/VBGameState.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "Narrative/Cinematic/VBCinematicStartupPolicy.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Vowbound/Vowbound.h"

AVBGameMode::AVBGameMode()
{
	// 기본 클래스 지정 BP에서 오버라이드 가능하게
}

void AVBGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 게임 시작 -> 바로 Warmup Phase 진입
	StartPhase(EVBMatchPhase::Warmup, WarmupDurationSec);
}

void AVBGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// 기본 스폰(PlayerStart 배치 + Possess)을 먼저 수행 — 이 시점에 폰이 존재하고 PossessedBy 의 GAS 초기화가 끝난다.
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	// Continue(세이브 로드) 로 진입한 경우에만 적용. New Game / 무세이브면 플래그가 없어 기본 상태를 유지한다.
	UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this);
	const FVBGameStartContext Start = Save ? Save->GetGameStartContext() : FVBGameStartContext();
	const TWeakObjectPtr<APawn> StartingPawn = IsValid(NewPlayer) ? NewPlayer->GetPawn() : nullptr;
	const TWeakObjectPtr<UVBPlayerReadinessComponent> Readiness = StartingPawn.IsValid()
		? StartingPawn->FindComponentByClass<UVBPlayerReadinessComponent>() : nullptr;
	const auto CompletePreparation = [&](bool bContinueDispatched)
	{
		if (Readiness.IsValid() && StartingPawn.IsValid() && IsValid(NewPlayer)
			&& NewPlayer->GetPawn() == StartingPawn.Get() && StartingPawn->GetController() == NewPlayer
			&& StartingPawn->GetWorld() == GetWorld())
		{
			Readiness->CompleteInitialization(Start.AttemptId, bContinueDispatched);
		}
	};
	if (Readiness.IsValid()) Readiness->BeginInitialization(Start.AttemptId, Start.Intent);
	if (!Save || !Save->ConsumePendingContinueApply())
	{
		if (bPlayInitialNewGameCinematic && Save && Start.Intent == EVBGameStartIntent::NewGame
			&& Save->ConsumeInitialNewGameStart(Start.AttemptId))
		{
			if (UVBCinematicStartupPolicy* Policy = GetWorld()->GetSubsystem<UVBCinematicStartupPolicy>())
			{
				Policy->BeginNewGameCinematic(NewPlayer, InitialNewGameCinematic, Start.AttemptId);
			}
			// 시작 거절도 완료가 아니다. 이 최초 시도의 초기화는 정책만 끝낸다.
			return;
		}
		CompletePreparation(false);
		return;
	}
	if (!IsValid(NewPlayer))
	{
		return;
	}
	// Initialization notifications may synchronously replace the pawn or start a different game.
	if (Save->GetGameStartContext().AttemptId != Start.AttemptId || NewPlayer->GetPawn() != StartingPawn.Get()) return;

	// 참여자가 각자 자기 구역을 적용한다 - 빌드(AVBPlayerState), 문·스위치(UVBInteractableSubsystem, 스트리밍으로 들어올 것은 등록될 때),
	//   위치(UVBPlayerRestoreComponent, WP 스트리밍을 기다려 늦게), 사실(UVBStoryStateComponent). 설계 .sw/narrative 9.3절.
	// 이 자리가 Super(스폰·빙의 = GE_InitStats) 뒤라는 것이 빌드 복원의 전제다(AVBPlayerState::ClampToCap).
	FVBSaveContext Context;
	Context.World = GetWorld();
	Context.PlayerController = NewPlayer;
	Context.Pawn = NewPlayer->GetPawn();
	Context.PlayerState = NewPlayer->PlayerState;
	const int32 Applied = Save->ApplyActiveSave(Context);
	// «적용 완료» 라고 쓰지 않는다 - 수는 건넨 참여자 수다. 적용 거절은 참여자가 각자 경고한다(UVBSaveParticipantSubsystem::ReadAll).
	VB_LOG(Log, "AVBGameMode: Continue 세이브를 참여자 %d 에게 건넸다", Applied);
	CompletePreparation(Applied > 0);
}

void AVBGameMode::StartPhase(EVBMatchPhase NewPhase, int32 DurationSec)
{
	// 1. GameState 가져오기
	AVBGameState* GS = GetVBGameState();
	if (!GS)
	{
		return;
	}

	// 2. 이전 Phase 저장 (K2 이벤트에 넘기기 위해)
	const EVBMatchPhase OldPhase = GS->GetCurrentMatchPhase();

	// 3. GameState에 새 Phase와 시간 세팅
	GS->SetCurrentMatchPhase(NewPhase);
	GS->SetRemainingTimeSec(FMath::Max(0, DurationSec));

	// 4. BP 이벤트 발동 (사운드, UI 전환 등)
	K2_OnMatchPhaseStarted(NewPhase, OldPhase);

	// 5. 기존 타이머 정리
	GetWorldTimerManager().ClearTimer(MatchPhaseTimerHandle);

	// 6. 시간이 있으면 타이머 시작, 없으면 즉시 다음 Phase
	if (DurationSec > 0)
	{
		GetWorldTimerManager().SetTimer(
		                                MatchPhaseTimerHandle,
		                                this,
		                                &AVBGameMode::HandlePhaseTimerTick,
		                                1.0f,
		                                true);
	}
	else
	{
		AdvancePhase();
	}
}

void AVBGameMode::HandlePhaseTimerTick()
{
	AVBGameState* GS = GetVBGameState();
	if (!GS)
	{
		// GameState 없으면 타이머 의미가 없음
		GetWorldTimerManager().ClearTimer(MatchPhaseTimerHandle);
		return;
	}

	// 남은 시간 1초 감소 (음수 방지)
	const int32 NewTimeSec = FMath::Max(0, GS->GetRemainingTimeSec() - 1);
	GS->SetRemainingTimeSec(NewTimeSec);

	// 시간 끝 -> 다음 Phase로
	if (NewTimeSec <= 0)
	{
		GetWorldTimerManager().ClearTimer(MatchPhaseTimerHandle);
		AdvancePhase();
	}
}

void AVBGameMode::AdvancePhase()
{
	AVBGameState* GS = GetVBGameState();
	if (!GS)
	{
		return;
	}

	switch (GS->GetCurrentMatchPhase())
	{
		case EVBMatchPhase::Warmup:
			StartPhase(EVBMatchPhase::InProgress, MatchDurationSec);
			break;
		case EVBMatchPhase::InProgress:
			StartPhase(EVBMatchPhase::PostMatch, PostMatchDurationSec);
			break;
		case EVBMatchPhase::PostMatch:
			StartPhase(EVBMatchPhase::None, 0);
			break;
		case EVBMatchPhase::None:
		default:
			break;
	}
}

AVBGameState* AVBGameMode::GetVBGameState() const
{
	return GetGameState<AVBGameState>();
}
