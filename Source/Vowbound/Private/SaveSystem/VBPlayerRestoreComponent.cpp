// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "SaveSystem/VBPlayerRestoreComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"      // EWorldPartitionRuntimeCellState (Activated 값)
#include "WorldPartition/WorldPartitionStreamingSource.h"  // FWorldPartitionStreamingQuerySource
#include "Vowbound/Vowbound.h"

UVBPlayerRestoreComponent::UVBPlayerRestoreComponent()
{
	// 이 컴포넌트는 이벤트/타이머 구동이라 매 프레임 Tick 불필요.
	PrimaryComponentTick.bCanEverTick = false;
}

void UVBPlayerRestoreComponent::BeginRestore(const FTransform& Target)
{
	ACharacter* Pawn = Cast<ACharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		VB_LOG(Warning, "VBPlayerRestoreComponent: 소유 액터가 ACharacter 가 아니거나 월드 없음 -> 복원 스킵");
		return;
	}
	if (bRestoring)
	{
		return; // 진행 중이면 무시(재진입 방지).
	}
	bRestoring = true;
	PendingTarget = Target;

	// 1. 저장 XY 로 텔레포트 → 폰이 PlayerController 뷰타겟이라 그 위치 스트리밍이 자동 요청된다.
	//    Z 는 목표값으로 두되(대략), 실제 지면 정렬은 4단계 라인트레이스가 확정한다.
	Pawn->SetActorLocation(Target.GetLocation(), /*bSweep=*/false);
	Pawn->SetActorRotation(Target.GetRotation());

	// 2. 동결 — 스트리밍 전 낙하/관통 방지.
	FreezePawn(true);

	// 3. 스트리밍 완료 폴링 시작(타임아웃은 PollStreaming 에서 판정).
	StreamStartTime = World->GetTimeSeconds();
	World->GetTimerManager().SetTimer(PollTimerHandle, this, &UVBPlayerRestoreComponent::PollStreaming,
	                                  FMath::Max(0.02f, PollIntervalSeconds), /*bLoop=*/true);
}

void UVBPlayerRestoreComponent::PollStreaming()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bool bDone = true; // WP 미사용 맵이면 대기 없이 즉시 완료.
	if (UWorldPartitionSubsystem* WP = World->GetSubsystem<UWorldPartitionSubsystem>())
	{
		TArray<FWorldPartitionStreamingQuerySource> Sources;
		Sources.Emplace(FWorldPartitionStreamingQuerySource(PendingTarget.GetLocation()));
		// Activated = 셀이 월드에 추가되어 콜리전이 존재하는 상태(지면 트레이스가 유효해지는 시점).
		bDone = WP->IsStreamingCompleted(EWorldPartitionRuntimeCellState::Activated, Sources, /*bExactState=*/false);
	}

	const bool bTimedOut = (World->GetTimeSeconds() - StreamStartTime) >= StreamingTimeoutSeconds;
	if (!bDone && !bTimedOut)
	{
		return; // 아직 대기.
	}
	if (bTimedOut && !bDone)
	{
		VB_LOG(Warning, "VBPlayerRestoreComponent: 스트리밍 완료 대기 타임아웃(%.1fs) -> 강제 지면 스냅", StreamingTimeoutSeconds);
	}

	World->GetTimerManager().ClearTimer(PollTimerHandle);
	SnapToGroundAndFinish();
}

void UVBPlayerRestoreComponent::SnapToGroundAndFinish()
{
	ACharacter* Pawn = Cast<ACharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		bRestoring = false;
		return;
	}

	const FVector TargetLoc = PendingTarget.GetLocation();
	const float HalfHeight = Pawn->GetCapsuleComponent() ? Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : FallbackCapsuleHalfHeight;

	// 목표 XY 에서 아래로 트레이스해 실제 지면 Z 를 얻는다(스트리밍된 콜리전 대상). WorldStatic 채널.
	const FVector TraceStart = TargetLoc + FVector(0, 0, GroundSnapTraceUpDistance);
	const FVector TraceEnd   = TargetLoc - FVector(0, 0, GroundSnapTraceDownDistance);
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Pawn);
	FVector FinalLoc = TargetLoc;
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params))
	{
		FinalLoc = FVector(TargetLoc.X, TargetLoc.Y, Hit.ImpactPoint.Z + HalfHeight);
	}
	Pawn->SetActorLocation(FinalLoc, /*bSweep=*/false);

	// 5. 동결 해제 후 완료 통지.
	FreezePawn(false);
	bRestoring = false;
	VB_LOG(Log, "VBPlayerRestoreComponent: 복원 완료 @ %s", *FinalLoc.ToString());
	OnRestoreCompleted.Broadcast();
}

void UVBPlayerRestoreComponent::FreezePawn(bool bFreeze)
{
	ACharacter* Pawn = Cast<ACharacter>(GetOwner());
	if (!Pawn)
	{
		return;
	}
	UCharacterMovementComponent* CMC = Pawn->GetCharacterMovement();

	if (bFreeze)
	{
		if (CMC)
		{
			SavedGravityScale = CMC->GravityScale;
			CMC->StopMovementImmediately();
			CMC->GravityScale = 0.0f;
			CMC->SetMovementMode(MOVE_None);
		}
		Pawn->SetActorEnableCollision(false); // 스트리밍 중 지형 관통/충돌 방지.
	}
	else
	{
		Pawn->SetActorEnableCollision(true);
		if (CMC)
		{
			CMC->GravityScale = SavedGravityScale;
			CMC->SetMovementMode(MOVE_Walking);
		}
	}
}
