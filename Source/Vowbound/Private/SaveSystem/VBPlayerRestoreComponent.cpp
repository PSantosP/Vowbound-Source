// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "GameFramework/Controller.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Interaction/VBGASPSmartObjectComponent.h"

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

void UVBPlayerRestoreComponent::OnRegister()
{
	Super::OnRegister();
	bEnding = false;
	// 에디터 월드(BP 미리보기 포함)에도 등록되지만 그 월드에서는 저장이 돌지 않는다. 같은 컴포넌트가 다시 등록돼도 한 번만 든다(AddUnique).
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBPlayerRestoreComponent::OnUnregister()
{
	bEnding = true;
	CancelRestoreReceipt();
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::OnUnregister();
}

void UVBPlayerRestoreComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnding = true;
	CancelRestoreReceipt();
	Super::EndPlay(EndPlayReason);
}

void UVBPlayerRestoreComponent::ArmInitialization(FGuid InitializationId)
{
	if (bEnding) return;
	RestoreReceipt = {};
	RestoreReceipt.InitializationId = InitializationId;
	if (bRestoring) RestoreReceipt.Outcome = EVBPlayerRestoreOutcome::StartFailed;
	OnRestoreStateChanged.Broadcast();
}

void UVBPlayerRestoreComponent::MarkRestoreNotRequired(FGuid InitializationId)
{
	if (bEnding || !InitializationId.IsValid() || RestoreReceipt.InitializationId != InitializationId || bRestoring) return;
	RestoreReceipt.Outcome = EVBPlayerRestoreOutcome::NotRequired;
	OnRestoreStateChanged.Broadcast();
}

void UVBPlayerRestoreComponent::SetRestoreOutcome(EVBPlayerRestoreOutcome Outcome)
{
	if (RestoreReceipt.InitializationId == RunningInitializationId && RestoreReceipt.OperationId == RunningOperationId)
	{
		RestoreReceipt.Outcome = Outcome;
		RestoreReceipt.bStreamingTimedOut = bStreamingTimedOut;
	}
	OnRestoreStateChanged.Broadcast();
}

void UVBPlayerRestoreComponent::CancelRestoreReceipt()
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(PollTimerHandle);
	if (!bRestoring)
	{
		RestoreReceipt.Outcome = EVBPlayerRestoreOutcome::Cancelled;
		OnRestoreStateChanged.Broadcast();
		return;
	}
	if (UWorld* World = GetWorld(); World && !World->bIsTearingDown) FreezePawn(false);
	bRestoring = false;
	SetRestoreOutcome(EVBPlayerRestoreOutcome::Cancelled);
}

void UVBPlayerRestoreComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.Pawn && Context.Pawn == GetOwner())
	{
		// 로드 때 WP 스트리밍-안전 복원(BeginRestore)의 목표가 된다. 체크포인트면 지정한 자리(설계 .sw/prologue-opening 3.6a).
		Save.Progress.SavedPlayerTransform = Context.RestoreTransformOverride.IsSet()
			? Context.RestoreTransformOverride.GetValue()
			: Context.Pawn->GetActorTransform();
		Save.Progress.bHasSavedTransform = true;
	}
}

void UVBPlayerRestoreComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (bEnding) return;
	// 위치가 없는 세이브(새 게임 직후·옛 슬롯)는 PlayerStart 스폰을 그대로 둔다.
	if (Context.Pawn && Context.Pawn == GetOwner())
	{
		if (Save.Progress.bHasSavedTransform) BeginRestore(Save.Progress.SavedPlayerTransform);
		else MarkRestoreNotRequired(RestoreReceipt.InitializationId);
	}
}

void UVBPlayerRestoreComponent::BeginRestore(const FTransform& Target)
{
	ACharacter* Pawn = Cast<ACharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (bEnding || !IsRegistered() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsValid(World) || World->bIsTearingDown)
	{
		RestoreReceipt.Outcome = EVBPlayerRestoreOutcome::StartFailed;
		OnRestoreStateChanged.Broadcast();
		VB_LOG(Warning, "VBPlayerRestoreComponent: 소유 액터가 ACharacter 가 아니거나 월드 없음 -> 복원 스킵");
		return;
	}
	if (bRestoring)
	{
		if (RestoreReceipt.InitializationId != RunningInitializationId)
		{
			RestoreReceipt.Outcome = EVBPlayerRestoreOutcome::StartFailed;
			OnRestoreStateChanged.Broadcast();
		}
		return; // 진행 중이면 무시(재진입 방지).
	}
	bRestoring = true;
	RunningInitializationId = RestoreReceipt.InitializationId;
	RunningOperationId = FGuid::NewGuid();
	RestoreReceipt.OperationId = RunningOperationId;
	RestoreReceipt.bGroundFound = false;
	bStreamingTimedOut = false;
	RestoreReceipt.bStreamingTimedOut = false;
	PendingTarget = Target;
	if (UVBPlayerRagdollComponent* Ragdoll = Pawn->FindComponentByClass<UVBPlayerRagdollComponent>())
		Ragdoll->CancelRagdoll();
	if (UVBGASPSmartObjectComponent* SmartObject = Pawn->FindComponentByClass<UVBGASPSmartObjectComponent>())
		SmartObject->AbortInteraction();

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
	SetRestoreOutcome(EVBPlayerRestoreOutcome::InProgress);
}

void UVBPlayerRestoreComponent::PollStreaming()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		bRestoring = false;
		SetRestoreOutcome(EVBPlayerRestoreOutcome::Cancelled);
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
		bStreamingTimedOut = true;
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
		SetRestoreOutcome(EVBPlayerRestoreOutcome::Cancelled);
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
	const bool bGroundFound = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params);
	if (bGroundFound)
	{
		FinalLoc = FVector(TargetLoc.X, TargetLoc.Y, Hit.ImpactPoint.Z + HalfHeight);
	}
	Pawn->SetActorLocation(FinalLoc, /*bSweep=*/false);

	// 5. 동결 해제 후 완료 통지.
	FreezePawn(false);
	bRestoring = false;
	if (RestoreReceipt.InitializationId == RunningInitializationId && RestoreReceipt.OperationId == RunningOperationId)
	{
		RestoreReceipt.bGroundFound = bGroundFound;
	}
	SetRestoreOutcome(bStreamingTimedOut ? EVBPlayerRestoreOutcome::StreamingTimedOut
		: bGroundFound ? EVBPlayerRestoreOutcome::Completed : EVBPlayerRestoreOutcome::GroundNotFound);
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
