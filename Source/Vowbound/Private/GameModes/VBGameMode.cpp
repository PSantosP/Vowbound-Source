// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "GameModes/VBGameMode.h"
#include "TimerManager.h"
#include "GameModes/VBGameState.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "Player/VBPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
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
	if (!Save || !Save->ConsumePendingContinueApply())
	{
		return;
	}
	UVBSaveGame* Game = Save->GetActiveGameSave();
	if (!Game || !NewPlayer)
	{
		return;
	}

	// 빌드/스탯 적용(즉시) — 어트리뷰트 base 값 세팅 + 무기 재소환.
	if (AVBPlayerState* PS = NewPlayer->GetPlayerState<AVBPlayerState>())
	{
		PS->ApplyBuild(Game->PlayerBuild);
	}

	// 위치 복원(WP 스트리밍-안전, 지연) — 저장된 Transform 이 있을 때만.
	if (Game->Progress.bHasSavedTransform)
	{
		if (APawn* Pawn = NewPlayer->GetPawn())
		{
			if (UVBPlayerRestoreComponent* Restore = Pawn->FindComponentByClass<UVBPlayerRestoreComponent>())
			{
				Restore->BeginRestore(Game->Progress.SavedPlayerTransform);
			}
		}
	}
	VB_LOG(Log, "AVBGameMode: Continue 적용 완료(bHasSavedTransform=%d)", Game->Progress.bHasSavedTransform ? 1 : 0);
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
