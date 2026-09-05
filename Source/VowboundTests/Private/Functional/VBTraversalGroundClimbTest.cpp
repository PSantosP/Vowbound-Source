// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "VBTraversalGroundClimbTest.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Traversal/VBTraversalComponent.h"

AVBTraversalGroundClimbTest::AVBTraversalGroundClimbTest()
{
	// 클라임 몽타주 ~4s + 웜업 재시도 여유 — 베이스 30s 한도 내 충분.
}

void AVBTraversalGroundClimbTest::StartTest()
{
	Super::StartTest();

	// 스코프 캡처는 텔레포트 전에 무장 — 트리거 시점 텔레메트리를 놓치지 않기 위해.
	LogCapture = MakeUnique<FVBScopedLogCapture>(FName(TEXT("LogVB")), TEXT("Traversal GROUND"));

	if (!TeleportPlayer(StartLocation, StartYaw))
	{
		FinishTest(EFunctionalTestResult::Failed, TEXT("player pawn not found (GameMode/spawn chain broken?)"));
		return;
	}

	AttemptTraversal();
}

void AVBTraversalGroundClimbTest::AttemptTraversal()
{
	APawn* Pawn = GetPlayerPawn();
	UVBTraversalComponent* Trav = Pawn ? Pawn->FindComponentByClass<UVBTraversalComponent>() : nullptr;
	if (!Trav)
	{
		FinishTest(EFunctionalTestResult::Failed, TEXT("UVBTraversalComponent not found on player pawn"));
		return;
	}

	if (Trav->TryTraversalAction(false))
	{
		// 트리거 성공 — 완료 폴링으로 전환.
		GetWorldTimerManager().SetTimer(PollHandle, this, &AVBTraversalGroundClimbTest::PollCompletion, 0.5f, true);
		return;
	}

	if (!bRetried)
	{
		// MM 인덱스 웜업 retry-once (proven rule) — 에디터/프로세스 재시작 직후 1차 query 는 null 일 수 있다.
		bRetried = true;
		GetWorldTimerManager().SetTimer(RetryHandle, this, &AVBTraversalGroundClimbTest::AttemptTraversal, RetryDelaySec, false);
		return;
	}

	FinishTest(EFunctionalTestResult::Failed, TEXT("TryTraversalAction returned false twice (warm-up excuse spent)"));
}

void AVBTraversalGroundClimbTest::PollCompletion()
{
	ACharacter* Char = Cast<ACharacter>(GetPlayerPawn());
	if (!Char)
	{
		FinishTest(EFunctionalTestResult::Failed, TEXT("player pawn lost during test"));
		return;
	}

	UAnimInstance* Anim = Char->GetMesh() ? Char->GetMesh()->GetAnimInstance() : nullptr;
	const bool bMontageDone = Anim && Anim->GetCurrentActiveMontage() == nullptr;
	const double Z = Char->GetActorLocation().Z;

	// 완료 조건 미충족이면 계속 폴링 — 행은 베이스 TimeLimit(30s, Failed)이 차단.
	if (!bMontageDone || Z <= MinTopZ)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(PollHandle);

	const int32 TelemetryLines = LogCapture ? LogCapture->Count() : 0;
	if (TelemetryLines >= 1)
	{
		FinishTest(EFunctionalTestResult::Succeeded,
			FString::Printf(TEXT("z=%.1f, montage done, GROUND telemetry x%d"), Z, TelemetryLines));
	}
	else
	{
		// z 도달했는데 텔레메트리 0 = 다른 경로로 올라간 것 — 트래버설 검증으로는 무효.
		FinishTest(EFunctionalTestResult::Failed,
			FString::Printf(TEXT("reached z=%.1f but no 'Traversal GROUND' telemetry line"), Z));
	}
}
