// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "VBAirMantleInjectionTest.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AVBAirMantleInjectionTest::AVBAirMantleInjectionTest()
{
	// 풀패스 디폴트 — 동명 IA(NiagaraExamples) 오로드 방지. 맵에서 EditAnywhere 로 교체 가능.
	JumpAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/Vowbound/Input/Actions/IA_Jump.IA_Jump")));
}

void AVBAirMantleInjectionTest::StartTest()
{
	Super::StartTest();

	LogCapture = MakeUnique<FVBScopedLogCapture>(FName(TEXT("LogVB")), TEXT("Traversal AIR"));

	if (!TeleportPlayer(StartLocation, StartYaw))
	{
		FinishTest(EFunctionalTestResult::Failed, TEXT("player pawn not found"));
		return;
	}

	ResolvedAction = JumpAction.LoadSynchronous();
	if (!ResolvedAction)
	{
		FinishTest(EFunctionalTestResult::Failed,
			FString::Printf(TEXT("InputAction load failed: %s"), *JumpAction.ToString()));
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsys = LP ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP) : nullptr;
	if (!Subsys)
	{
		FinishTest(EFunctionalTestResult::Failed, TEXT("EnhancedInputLocalPlayerSubsystem unavailable (PC/LocalPlayer null?)"));
		return;
	}

	// 연속 주입 시작 = Started 발화(실바인딩) — HandleJumpPressed 경로 그대로.
	// Modifiers/Triggers 빈 배열: raw value 직결 (simulate_enhanced_input 설계와 동일, YAGNI).
	Subsys->StartContinuousInputInjectionForAction(ResolvedAction, FInputActionValue(true), {}, {});

	// HoldSeconds 후 정지 = Completed 발화 → bJumpInputHeld 해제 → 재발동 없음 검증의 전제.
	GetWorldTimerManager().SetTimer(StopHandle, this, &AVBAirMantleInjectionTest::StopInjection, HoldSeconds, false);

	// 완료 폴링은 점프 호 + 맨틀 몽타주 여유를 두고 시작 (피크 표본화는 Tick 담당이라 폴링 지연 무해).
	GetWorldTimerManager().SetTimer(PollHandle, this, &AVBAirMantleInjectionTest::PollCompletion, 0.5f, true, HoldSeconds + 2.0f);

	bDriving = true;
}

void AVBAirMantleInjectionTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bDriving)
	{
		return;
	}

	Elapsed += DeltaSeconds;

	ACharacter* Char = Cast<ACharacter>(GetPlayerPawn());
	if (!Char)
	{
		return;
	}

	MaxZ = FMath::Max(MaxZ, Char->GetActorLocation().Z);

	// 공중에서만 벽 방향(-X) 전진 — 지상 드라이브는 GROUND 트래버설 선점(속도비례 트레이스) 함정.
	UCharacterMovementComponent* CMC = Char->GetCharacterMovement();
	if (CMC && !CMC->IsMovingOnGround())
	{
		Char->AddMovementInput(FVector(-1.0, 0.0, 0.0), 1.0f);
		++DroveFrames;
	}
}

void AVBAirMantleInjectionTest::StopInjection()
{
	if (bInjectionStopped)
	{
		return;
	}
	bInjectionStopped = true;

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsys = LP ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP) : nullptr;
	if (Subsys && ResolvedAction)
	{
		Subsys->StopContinuousInputInjectionForAction(ResolvedAction);
	}
}

void AVBAirMantleInjectionTest::PollCompletion()
{
	ACharacter* Char = Cast<ACharacter>(GetPlayerPawn());
	if (!Char)
	{
		FinishTest(EFunctionalTestResult::Failed, TEXT("player pawn lost during test"));
		return;
	}

	// 완료 조건: 착지(벽 위 포함) + 충분한 경과. 미충족 시 계속 폴링 — TimeLimit 이 행 차단.
	UCharacterMovementComponent* CMC = Char->GetCharacterMovement();
	const bool bGrounded = CMC && CMC->IsMovingOnGround();
	if (!bGrounded || Elapsed < HoldSeconds + 2.0)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(PollHandle);
	bDriving = false;
	StopInjection(); // 폴링 도달 전 타이머 누락 등 엣지 방어 — 멱등.

	const int32 AirLines = LogCapture ? LogCapture->Count() : 0;

	if (DroveFrames == 0)
	{
		// 공중 드라이브가 한 번도 안 돎 = 점프 자체가 발생 안 함 (주입→Started→지상게이트 체인 단절).
		FinishTest(EFunctionalTestResult::Failed,
			TEXT("air-drive never fired - injection did not produce a jump (key path broken)"));
		return;
	}

	if (MaxZ >= MinPeakZ && AirLines == 1)
	{
		FinishTest(EFunctionalTestResult::Succeeded,
			FString::Printf(TEXT("maxz=%.1f, drove=%d frames, 'Traversal AIR' x1 (no re-trigger after release)"), MaxZ, DroveFrames));
	}
	else
	{
		FinishTest(EFunctionalTestResult::Failed,
			FString::Printf(TEXT("maxz=%.1f (need >=%.1f), AIR lines=%d (need exactly 1), drove=%d"),
				MaxZ, MinPeakZ, AirLines, DroveFrames));
	}
}
