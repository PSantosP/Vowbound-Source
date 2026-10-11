// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#include "Character/VBTargetLockComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBTargetable.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Traversal/VBTraversalComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Input/VBInputDeviceManager.h"
#include "Data/VBTargetLockConfig.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Player/VBPlayerState.h"
#include "Vowbound/Vowbound.h"

UVBTargetLockComponent::UVBTargetLockComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.SetTickFunctionEnable(false);
	SetIsReplicatedByDefault(true);
}

const UVBTargetLockConfig* UVBTargetLockComponent::GetTargetLockConfig() const
{
	return LockConfig ? LockConfig : GetDefault<UVBTargetLockConfig>();
}

void UVBTargetLockComponent::RefreshOwner()
{
	OwnerCharacter = Cast<AVBCharacter>(GetOwner());
	APlayerController* Controller = OwnerCharacter.IsValid() ? OwnerCharacter->GetController<APlayerController>() : nullptr;
	if (OwnerController.Get() != Controller)
	{
		OwnerController = Controller;
		CancelHitRecenter();
		HitRecenterWindow = {};
		LastReceivedHitTime = -DBL_MAX;
		ResetCameraInput();
		ResetSwitchInput();
		NextRequestId = PendingRequestId = LastServerRequestId = 0;
		bSuppressCameraUntilResponse = false;
		bDeathReacquirePending = false;
		if (GetOwner()->HasAuthority() && bGameplayEffectsApplied)
			SetLockOn(nullptr, EVBLockReason::OwnerChanged);
	}
}

bool UVBTargetLockComponent::CanOwnerLock() const
{
	return OwnerCharacter.IsValid() && OwnerController.IsValid() && !UVBAbilitySystemStatics::IsActorDead(OwnerCharacter.Get());
}

AActor* UVBTargetLockComponent::GetLockedTarget() const { return ConfirmedState.Target.Get(); }
bool UVBTargetLockComponent::IsLockedOn() const { return IsValid(GetLockedTarget()); }

void UVBTargetLockComponent::ToggleLockOn()
{
	RefreshOwner();
	CancelHitRecenter(true); // 수동 락 버튼도 진행 중인 명중 보정보다 우선한다.
	if (!CanOwnerLock()) return;
	if (IsLockedOn() || PendingRequestId != 0)
		RequestLock(nullptr, EVBLockReason::Manual);
	else if (AActor* Target = FindBestTarget())
		RequestLock(Target, EVBLockReason::Manual);
}

void UVBTargetLockComponent::SwitchTarget(float AxisValue)
{
	RefreshOwner();
	if (!IsLockedOn() || PendingRequestId || !FMath::IsFinite(AxisValue) || FMath::IsNearlyZero(AxisValue)) return;
	if (AActor* Target = FindSideTarget(AxisValue))
		RequestLock(Target, EVBLockReason::Switch);
}

void UVBTargetLockComponent::HandleSwitchInput(float AxisValue, bool bGamepad)
{
	if (bGamepad)
	{
		const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
		const int32 Direction = SwitchLatch.Update(AxisValue, Cfg->StickSwitchPressThreshold, Cfg->StickSwitchReleaseThreshold);
		if (Direction != 0 && CanTrackCamera()) SwitchTarget(float(Direction));
	}
	else if (CanTrackCamera())
	{
		SwitchTarget(AxisValue);
	}
}

void UVBTargetLockComponent::ResetSwitchInput() { SwitchLatch.Reset(); }

void UVBTargetLockComponent::OnInputDeviceChanged(ECommonInputType /*NewDeviceType*/)
{
	CancelHitRecenter(true);
	ResetSwitchInput();
	ResetCameraInput();
}

void UVBTargetLockComponent::RequestLock(AActor* Target, EVBLockReason Reason)
{
	if (!OwnerCharacter.IsValid() || !OwnerCharacter->IsLocallyControlled()) return;
	PendingRequestId = ++NextRequestId;
	if (!Target)
	{
		bSuppressCameraUntilResponse = true;
		bDeathReacquirePending = false;
	}
	SetComponentTickEnabled(true);
	ServerRequestLock(Target, Reason, PendingRequestId, ConfirmedState.Revision);
}

void UVBTargetLockComponent::ServerRequestLock_Implementation(AActor* Target, EVBLockReason Reason, uint32 RequestId, uint32 ExpectedRevision)
{
	RefreshOwner();
	if (RequestId > LastServerRequestId)
	{
		LastServerRequestId = RequestId;
		if (!Target)
		{
			// 입력 해제만 재획득 억제한다. 질주/거리/가림은 별도 해제 사유다.
			if (Reason == EVBLockReason::Manual)
				SuppressHitUntil = GetWorld()->GetTimeSeconds() + GetTargetLockConfig()->ManualUnlockHitDelay;
			if (Reason == EVBLockReason::Manual || Reason == EVBLockReason::Occluded || Reason == EVBLockReason::Gameplay)
			{
				// 수동 해제는 최신 선택도 해제할 수 있다. 옛 가림 보고는 최신 대상을 해제하지 않는다.
				if (Reason != EVBLockReason::Occluded || ExpectedRevision == LockState.Revision)
					SetLockOn(nullptr, Reason);
			}
		}
		else
		{
			const bool bAllowedReason = Reason == EVBLockReason::Manual || Reason == EVBLockReason::Switch ||
				Reason == EVBLockReason::Guard || Reason == EVBLockReason::Reacquire;
			const bool bReasonStateValid = (Reason != EVBLockReason::Switch || IsValid(LockState.Target)) &&
				(Reason != EVBLockReason::Guard || !IsValid(LockState.Target)) &&
				(Reason != EVBLockReason::Reacquire || (!IsValid(LockState.Target) &&
				 LockState.Reason == EVBLockReason::TargetDied && GetTargetLockConfig()->bAutoRetargetOnDeath));
			if (bAllowedReason && bReasonStateValid && CanOwnerLock() && ExpectedRevision == LockState.Revision &&
				IsTargetValid(Target) && VBTargetLockPolicy::IsWithinDistance(
					FVector::DistSquared(OwnerCharacter->GetActorLocation(), Target->GetActorLocation()), GetTargetLockConfig()->TargetSearchRadius) &&
				HasLineOfSight(Target, OwnerCharacter->GetPawnViewLocation()))
				SetLockOn(Target, Reason);
		}
	}
	// 거절도 응답하여 클라 대기 상태가 영구히 남지 않게 한다.
	ClientLockResponse(RequestId, LockState);
}

void UVBTargetLockComponent::ClientLockResponse_Implementation(uint32 RequestId, FVBTargetLockState State)
{
	if (RequestId == PendingRequestId)
	{
		PendingRequestId = 0;
		bSuppressCameraUntilResponse = false;
	}
	AcceptState(State);
}

void UVBTargetLockComponent::SetLockOn(AActor* Target, EVBLockReason Reason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (bGameplayEffectsApplied)
	{
		RemoveLockOnGameplayEffects();
		bGameplayEffectsApplied = false;
	}
	LockState.Target = Target;
	LockState.Reason = Reason;
	++LockState.Revision;
	if (IsValid(Target))
	{
		ApplyLockOnGameplayEffects();
		bGameplayEffectsApplied = true;
	}
	VB_LOG(Log, "[LockOn] revision=%u reason=%d target=%s", LockState.Revision, int32(Reason), *GetNameSafe(Target));
	AcceptState(LockState);
	GetOwner()->ForceNetUpdate();
}

void UVBTargetLockComponent::AcceptState(const FVBTargetLockState& State)
{
	if (State.Revision < ConfirmedState.Revision) return;
	// 같은 revision의 NetGUID가 나중에 해석될 수 있다. 이미 유효한 상태를 null로 되돌리지는 않는다.
	if (State.Revision == ConfirmedState.Revision &&
		(State.Target == ConfirmedState.Target || !IsValid(State.Target) || IsValid(ConfirmedState.Target))) return;
	const bool bHadTarget = IsLockedOn();
	ConfirmedState = State;
	CancelHitRecenter(true); // 가드/패링/해제 전환 뒤 옛 명중 보정이 재개되지 않게 한다.
	ResetCameraInput();
	OccludedSeconds = 0.0f;
	VisibilityCheckElapsed = GetTargetLockConfig()->VisibilityCheckInterval;
	bTargetVisible = true;
	bHasVisibleAimPoint = false;
	if (IsLockedOn())
	{
		CameraTransitionTimeline.Play();
		OnTargetLocked.Broadcast(GetLockedTarget());
	}
	else
	{
		ResetSwitchInput();
		CameraTransitionTimeline.Reverse();
		OnTargetUnlocked.Broadcast(nullptr);
	}
	// 서버가 확인한 사망만 재선택한다. 파괴/거리/수동 해제는 제외한다.
	bDeathReacquirePending = State.Reason == EVBLockReason::TargetDied &&
		GetTargetLockConfig()->bAutoRetargetOnDeath && !bSuppressCameraUntilResponse &&
		OwnerCharacter.IsValid() && OwnerCharacter->IsLocallyControlled();
	SetComponentTickEnabled(IsLockedOn() || bHadTarget || bDeathReacquirePending || PendingRequestId != 0 || CameraTransitionTimeline.IsPlaying());
}

void UVBTargetLockComponent::OnRep_LockState() { RefreshOwner(); AcceptState(LockState); }

void UVBTargetLockComponent::ClearLockOn()
{
	RefreshOwner();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (bGameplayEffectsApplied || LockState.Target) SetLockOn(nullptr, EVBLockReason::Gameplay);
	}
	else RequestLock(nullptr, EVBLockReason::Gameplay);
}

void UVBTargetLockComponent::TryAutoLockOnTarget(AActor* Target)
{
	RefreshOwner();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !CanOwnerLock() || IsLockedOn()) return;
	if (!GetTargetLockConfig()->bAutoLockOnHit || GetWorld()->GetTimeSeconds() < SuppressHitUntil) return;
	if (IsTargetValid(Target)) SetLockOn(Target, EVBLockReason::Hit);
}

void UVBTargetLockComponent::NotifyMeleeHitForCamera(AActor* Target)
{
	RefreshOwner();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !CanOwnerLock() || IsLockedOn() ||
		!GetTargetLockConfig()->bRecenterCameraOnHit || !IsTargetValid(Target) ||
		UVBAbilitySystemStatics::IsActorDead(Target)) return;
	if (const AGameStateBase* GameState = GetWorld()->GetGameState())
		ClientRecenterOnHit(Target, OwnerController.Get(), GameState->GetServerWorldTimeSeconds());
}

void UVBTargetLockComponent::CancelHitRecenter(bool bSuppressNew)
{
	HitRecenterWindow.Cancel();
	if (bSuppressNew && GetWorld()) HitRecenterWindow.RecordInput(GetWorld()->GetTimeSeconds());
	HitRecenterTarget.Reset();
}

void UVBTargetLockComponent::NotifyManualCameraInput(float Yaw, float Pitch)
{
	if (!GetWorld() || !FMath::IsFinite(Yaw) || !FMath::IsFinite(Pitch) ||
		(FMath::IsNearlyZero(Yaw) && FMath::IsNearlyZero(Pitch))) return;
	CancelHitRecenter(true);
}

void UVBTargetLockComponent::ClientRecenterOnHit_Implementation(AActor* Target, APlayerController* HitController, double ServerHitTime)
{
	RefreshOwner();
	if (!OwnerController.IsValid() || HitController != OwnerController.Get() || !OwnerController->IsLocalController()) return;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	if (!GameState || ServerHitTime <= LastReceivedHitTime ||
		!VBTargetLockPolicy::IsHitNotificationFresh(GameState->GetServerWorldTimeSeconds(), ServerHitTime, Cfg->HitRecenterMaxNotificationAge)) return;
	LastReceivedHitTime = ServerHitTime;
	// 거절된 명중도 연타 간격에 포함한다. 화면 중앙에서 시작한 콤보가 중간부터 시점을 빼앗지 않는다.
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bCanStart = HitRecenterWindow.RegisterHit(Now, Cfg->HitRecenterQuietSeconds, Cfg->HitRecenterInputGraceSeconds);
	if (!bCanStart || !Cfg->bRecenterCameraOnHit || !CanUseCombatCamera() || IsLockedOn() || PendingRequestId ||
		bSuppressCameraUntilResponse || OwnerController->IsLookInputIgnored() || !IsTargetValid(Target) ||
		UVBAbilitySystemStatics::IsActorDead(Target) || !OwnerController->PlayerCameraManager) return;
	FVector2D Screen;
	// 카메라 뒤의 적으로 급회전하지 않는다. 전방 화면 밖 적은 짧은 각도 상한 안에서만 보정한다.
	if (!ProjectTarget(Target, Screen, false) || VBTargetLockPolicy::IsInRecenterSafeFrame(Screen, Cfg->HitRecenterSafeHalfExtent)) return;
	const FVector CameraLocation = OwnerController->PlayerCameraManager->GetCameraLocation();
	if (!HasLineOfSight(Target, CameraLocation)) return;
	const FVector AimDirection = GetAimPoint(Target) - CameraLocation;
	if (AimDirection.IsNearlyZero()) return;
	HitRecenterStartRotation = OwnerController->GetControlRotation();
	const FRotator Error = (AimDirection.Rotation() - OwnerController->PlayerCameraManager->GetCameraRotation()).GetNormalized();
	HitRecenterDelta = FRotator(
		FMath::Clamp(Error.Pitch, -double(FMath::Max(0.0f, Cfg->HitRecenterMaxPitch)), double(FMath::Max(0.0f, Cfg->HitRecenterMaxPitch))),
		FMath::Clamp(Error.Yaw, -double(FMath::Max(0.0f, Cfg->HitRecenterMaxYaw)), double(FMath::Max(0.0f, Cfg->HitRecenterMaxYaw))), 0.0);
	const double EndPitch = FMath::ClampAngle(HitRecenterStartRotation.Pitch + HitRecenterDelta.Pitch, Cfg->MinCameraPitch, Cfg->MaxCameraPitch);
	// 이미 수동 시점이 pitch 범위 밖이면 범위 안으로 강제로 끌어오지 않는다.
	const double ClampedPitchDelta = FMath::FindDeltaAngleDegrees(HitRecenterStartRotation.Pitch, EndPitch);
	if (ClampedPitchDelta * HitRecenterDelta.Pitch <= 0.0 || FMath::Abs(ClampedPitchDelta) > FMath::Abs(HitRecenterDelta.Pitch))
		HitRecenterDelta.Pitch = 0.0;
	else
		HitRecenterDelta.Pitch = ClampedPitchDelta;
	HitRecenterTarget = Target;
	HitRecenterWindow.Start(Now, Cfg->HitRecenterDuration);
}

void UVBTargetLockComponent::UpdateHitRecenter()
{
	RefreshOwner();
	if (!HitRecenterWindow.bActive) return;
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	AActor* Target = HitRecenterTarget.Get();
	if (!Cfg->bRecenterCameraOnHit || !CanUseCombatCamera() || IsLockedOn() || PendingRequestId ||
		bSuppressCameraUntilResponse || OwnerController->IsLookInputIgnored() || !IsTargetValid(Target) ||
		UVBAbilitySystemStatics::IsActorDead(Target) || !OwnerController->PlayerCameraManager)
	{
		CancelHitRecenter();
		return;
	}
	FVector2D Screen;
	if (!ProjectTarget(Target, Screen, false) || VBTargetLockPolicy::IsInRecenterSafeFrame(Screen, Cfg->HitRecenterSafeHalfExtent) ||
		!HasLineOfSight(Target, OwnerController->PlayerCameraManager->GetCameraLocation()))
	{
		CancelHitRecenter();
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// 목표 회전은 명중 때 한 번만 확정한다. 움직이는 적을 지속 추적하지 않는다.
	// 긴 프레임 지연 뒤에도 끝 각도로 순간 이동하지 않는다.
	if (Now >= HitRecenterWindow.EndTime)
	{
		CancelHitRecenter();
		return;
	}
	const double Alpha = HitRecenterWindow.GetAlpha(Now);
	OwnerController->SetControlRotation((HitRecenterStartRotation + HitRecenterDelta * Alpha).GetNormalized());
}

void UVBTargetLockComponent::TryAutoLockOnParryTarget(AActor* Target)
{
	RefreshOwner();
	// 패링 설정은 서버 패링 성공 호출자가 검사한다. 명중 설정을 공유하지 않는다.
	if (!GetOwner() || !GetOwner()->HasAuthority() || !CanOwnerLock() || IsLockedOn()) return;
	if (IsTargetValid(Target)) SetLockOn(Target, EVBLockReason::Parry);
}

void UVBTargetLockComponent::TryLockOnFromLocalView()
{
	RefreshOwner();
	if (!CanOwnerLock() || !OwnerCharacter->IsLocallyControlled() || IsLockedOn() || PendingRequestId) return;
	if (AActor* Target = FindBestTarget()) RequestLock(Target, EVBLockReason::Guard);
}

void UVBTargetLockComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshOwner();
	CameraTransitionTimeline.TickTimeline(DeltaTime);
	if (!OwnerCharacter.IsValid()) return;

	if (GetOwner()->HasAuthority() && bGameplayEffectsApplied)
	{
		AActor* Target = LockState.Target.Get();
		if (!CanOwnerLock()) SetLockOn(nullptr, EVBLockReason::Gameplay);
		else if (IsValid(Target) && UVBAbilitySystemStatics::IsActorDead(Target)) SetLockOn(nullptr, EVBLockReason::TargetDied);
		else if (!IsTargetEligible(Target)) SetLockOn(nullptr, EVBLockReason::InvalidTarget);
		else if (!VBTargetLockPolicy::IsWithinDistance(FVector::DistSquared(OwnerCharacter->GetActorLocation(), Target->GetActorLocation()),
			GetTargetLockConfig()->LockBreakDistance)) SetLockOn(nullptr, EVBLockReason::OutOfRange);
		else OwnerCharacter->NotifyCombatStimulus();
	}
	if (bDeathReacquirePending && PendingRequestId == 0)
	{
		bDeathReacquirePending = false;
		if (CanOwnerLock())
			if (AActor* Target = FindBestTarget()) RequestLock(Target, EVBLockReason::Reacquire);
	}
	if (IsLockedOn() && OwnerCharacter->IsLocallyControlled())
	{
		if (!CanTrackCamera())
		{
			OccludedSeconds = 0.0f;
			if (bWasCameraTracking) ResetCameraInput();
			bWasCameraTracking = false;
		}
		else
		{
			bWasCameraTracking = true;
			const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
			VisibilityCheckElapsed += DeltaTime;
			if (VisibilityCheckElapsed >= FMath::Max(0.01f, Cfg->VisibilityCheckInterval))
			{
				VisibilityCheckElapsed = 0.0f;
				const FVector Start = OwnerController->PlayerCameraManager ?
					OwnerController->PlayerCameraManager->GetCameraLocation() : OwnerCharacter->GetPawnViewLocation();
				bTargetVisible = HasLineOfSight(GetLockedTarget(), Start);
			}
			if (bTargetVisible)
			{
				OccludedSeconds = 0.0f;
				LastVisibleAimPoint = GetAimPoint(GetLockedTarget());
				bHasVisibleAimPoint = true;
			}
			else
			{
				OccludedSeconds += DeltaTime;
				if (OccludedSeconds >= Cfg->OcclusionGraceSeconds && PendingRequestId == 0)
					RequestLock(nullptr, EVBLockReason::Occluded);
			}
		}
	}
	if (!IsLockedOn() && !bGameplayEffectsApplied && !PendingRequestId && !bDeathReacquirePending && !CameraTransitionTimeline.IsPlaying())
		SetComponentTickEnabled(false);
}

bool UVBTargetLockComponent::CanTrackCamera() const
{
	return !bSuppressCameraUntilResponse && IsTargetEligible(GetLockedTarget()) && CanUseCombatCamera();
}

bool UVBTargetLockComponent::CanUseCombatCamera() const
{
	if (!CanOwnerLock() || !OwnerCharacter->IsLocallyControlled() ||
		OwnerCharacter->WantsToAim() || OwnerController->GetViewTarget() != OwnerCharacter.Get() ||
		UVBFinisherComponent::IsActorInScene(OwnerCharacter.Get())) return false;
	if (const auto* Traversal = OwnerCharacter->GetTraversalComponent(); Traversal && Traversal->OwnsCapsuleFacing()) return false;
	if (const auto* Ragdoll = OwnerCharacter->GetPlayerRagdollComponent(); Ragdoll && Ragdoll->IsRagdollActive()) return false;
	if (const auto* Smart = OwnerCharacter->FindComponentByClass<UVBGASPSmartObjectComponent>(); Smart && Smart->OwnsCapsuleFacing()) return false;
	if (const auto* Interactor = OwnerCharacter->FindComponentByClass<UVBInteractorComponent>(); Interactor && Interactor->OwnsCapsuleFacing()) return false;
	return true;
}

void UVBTargetLockComponent::ResetCameraInput()
{
	LookOffset = FVector2D::ZeroVector;
	LastLookInputTime = -1.0;
	bWasCameraTracking = false;
}

void UVBTargetLockComponent::AddLookInput(float Yaw, float Pitch, bool bGamepad)
{
	if (!CanTrackCamera() || !FMath::IsFinite(Yaw) || !FMath::IsFinite(Pitch)) return;
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	const float AppliedYaw = bGamepad ? 0.0f : Yaw;
	if (FMath::IsNearlyZero(AppliedYaw) && FMath::IsNearlyZero(Pitch)) return;
	LookOffset.X = FMath::Clamp(LookOffset.X + AppliedYaw, -double(Cfg->MaxLookYawOffset), double(Cfg->MaxLookYawOffset));
	LookOffset.Y = FMath::Clamp(LookOffset.Y + Pitch, -double(Cfg->MaxLookPitchOffset), double(Cfg->MaxLookPitchOffset));
	LastLookInputTime = GetWorld()->GetTimeSeconds();
}

void UVBTargetLockComponent::UpdateCameraRotation(float DeltaTime)
{
	RefreshOwner();
	if (!CanTrackCamera()) return;
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	if (GetWorld()->GetTimeSeconds() - LastLookInputTime > Cfg->LookOffsetReturnDelay)
		LookOffset = FMath::Vector2DInterpTo(LookOffset, FVector2D::ZeroVector, DeltaTime, Cfg->LookOffsetReturnSpeed);
	const FVector Start = OwnerController->PlayerCameraManager ?
		OwnerController->PlayerCameraManager->GetCameraLocation() : OwnerCharacter->GetPawnViewLocation();
	if (!bTargetVisible && !bHasVisibleAimPoint) return;
	const FVector Aim = bTargetVisible ? GetAimPoint(GetLockedTarget()) : LastVisibleAimPoint;
	if ((Aim - Start).IsNearlyZero()) return;
	FRotator Desired = (Aim - Start).Rotation();
	Desired.Yaw += LookOffset.X;
	Desired.Pitch = FMath::ClampAngle(Desired.Pitch + LookOffset.Y, Cfg->MinCameraPitch, Cfg->MaxCameraPitch);
	const FRotator Current = OwnerController->GetControlRotation();
	FRotator Delta = (Desired - Current).GetNormalized();
	if (FMath::Abs(Delta.Yaw) < Cfg->TrackingDeadZoneDegrees) Delta.Yaw = 0.0;
	if (FMath::Abs(Delta.Pitch) < Cfg->TrackingDeadZoneDegrees) Delta.Pitch = 0.0;
	const double Alpha = 1.0 - FMath::Exp(-FMath::Max(0.0f, Cfg->InterpSpeed) * DeltaTime);
	const double MaxStep = FMath::Max(0.0f, Cfg->MaxTrackingDegreesPerSecond) * DeltaTime;
	FRotator Next(Current.Pitch + FMath::Clamp(Delta.Pitch * Alpha, -MaxStep, MaxStep),
		Current.Yaw + FMath::Clamp(Delta.Yaw * Alpha, -MaxStep, MaxStep), 0.0);
	OwnerController->SetControlRotation(Next.GetNormalized());
}

bool UVBTargetLockComponent::IsTargetEligible(AActor* Actor) const
{
	if (!IsValid(Actor) || Actor == GetOwner()) return false;
	const IVBTargetable* Targetable = Cast<IVBTargetable>(Actor);
	return Targetable && Targetable->IsTargetable();
}

bool UVBTargetLockComponent::IsTargetValid(AActor* Actor) const
{
	return OwnerCharacter.IsValid() && IsTargetEligible(Actor) &&
		VBTargetLockPolicy::IsWithinDistance(FVector::DistSquared(OwnerCharacter->GetActorLocation(), Actor->GetActorLocation()),
			GetTargetLockConfig()->MaxLockDistance);
}

FVector UVBTargetLockComponent::GetAimPoint(AActor* Target) const
{
	if (const IVBTargetable* Targetable = Cast<IVBTargetable>(Target)) return Targetable->GetTargetMarkerLocation();
	return IsValid(Target) ? Target->GetActorLocation() : FVector::ZeroVector;
}

bool UVBTargetLockComponent::HasLineOfSight(AActor* Target, const FVector& Start) const
{
	if (!IsValid(Target) || !GetWorld()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBLockVisibility), false, GetOwner());
	TArray<AActor*> Attached;
	GetOwner()->GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);
	// 마커 하나가 가려져도 몸통이 보이는 큰 적은 후보로 유지한다.
	for (const FVector& End : {GetAimPoint(Target), Target->GetActorLocation()})
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, GetTargetLockConfig()->VisibilityTraceChannel, Params) ||
			Hit.GetActor() == Target) return true;
	}
	return false;
}

bool UVBTargetLockComponent::ProjectTarget(AActor* Target, FVector2D& OutScreen, bool bRequireOnScreen) const
{
	if (!IsValid(Target) || !OwnerController.IsValid() || !OwnerController->PlayerCameraManager) return false;
	const ULocalPlayer* LP = OwnerController->GetLocalPlayer();
	if (!LP || !LP->ViewportClient || !LP->ViewportClient->Viewport) return false;
	FSceneViewProjectionData Projection;
	if (!LP->GetProjectionData(LP->ViewportClient->Viewport, Projection)) return false;
	const FIntRect Rect = Projection.GetConstrainedViewRect();
	if (Rect.Width() <= 0 || Rect.Height() <= 0) return false;
	const FVector Aim = GetAimPoint(Target);
	const FVector Direction = Aim - OwnerController->PlayerCameraManager->GetCameraLocation();
	if (FVector::DotProduct(Direction, OwnerController->PlayerCameraManager->GetCameraRotation().Vector()) <= 0.0) return false;
	if (!OwnerController->ProjectWorldLocationToScreen(Aim, OutScreen, true)) return false;
	OutScreen /= FVector2D(Rect.Width(), Rect.Height());
	return !OutScreen.ContainsNaN() && (!bRequireOnScreen ||
		(OutScreen.X >= 0.0 && OutScreen.X <= 1.0 && OutScreen.Y >= 0.0 && OutScreen.Y <= 1.0));
}

AActor* UVBTargetLockComponent::FindScreenTarget(float Direction)
{
	if (!CanOwnerLock() || !OwnerController->PlayerCameraManager) return nullptr;
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	TArray<AActor*> Candidates;
	TArray<TEnumAsByte<EObjectTypeQuery>> Types;
	Types.Add(UEngineTypes::ConvertToObjectType(Cfg->SearchTraceChannel));
	TArray<AActor*> Ignore{OwnerCharacter.Get()};
	if (!FMath::IsNearlyZero(Direction)) Ignore.Add(GetLockedTarget());
	UKismetSystemLibrary::SphereOverlapActors(this, OwnerCharacter->GetActorLocation(), Cfg->TargetSearchRadius, Types, nullptr, Ignore, Candidates);
	FVector2D Origin(0.5, 0.5);
	if (!FMath::IsNearlyZero(Direction))
	{
		FVector2D Current;
		if (ProjectTarget(GetLockedTarget(), Current, false)) Origin = Current;
	}
	AActor* Best = nullptr;
	double BestScore = TNumericLimits<double>::Max();
	double BestDistance = TNumericLimits<double>::Max();
	const FVector Camera = OwnerController->PlayerCameraManager->GetCameraLocation();
	for (AActor* Candidate : Candidates)
	{
		FVector2D Screen;
		if (!IsTargetValid(Candidate) || !ProjectTarget(Candidate, Screen, true)) continue;
		if (!FMath::IsNearlyZero(Direction) && !VBTargetLockPolicy::IsOnSide(Origin, Screen, Direction, Cfg->MinSwitchHorizontalDelta)) continue;
		if (!HasLineOfSight(Candidate, Camera)) continue;
		const double Score = VBTargetLockPolicy::ScreenScore(Origin, Screen, Cfg->ScreenVerticalWeight);
		const double Distance = FVector::DistSquared(OwnerCharacter->GetActorLocation(), Candidate->GetActorLocation());
		const bool bTie = FMath::IsNearlyEqual(Score, BestScore, 1.e-6);
		if (Score < BestScore - 1.e-6 || (bTie && (Distance < BestDistance ||
			(Distance == BestDistance && Best && Candidate->GetPathName() < Best->GetPathName()))))
		{
			Best = Candidate;
			BestScore = Score;
			BestDistance = Distance;
		}
	}
	return Best;
}
AActor* UVBTargetLockComponent::FindBestTarget() { return FindScreenTarget(0.0f); }
AActor* UVBTargetLockComponent::FindSideTarget(float AxisValue) { return FindScreenTarget(AxisValue); }

void UVBTargetLockComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshOwner();
	if (OwnerCharacter.IsValid() && OwnerCharacter->GetInputDeviceManager())
		OwnerCharacter->GetInputDeviceManager()->OnInputDeviceChanged.AddDynamic(this, &ThisClass::OnInputDeviceChanged);

	// OwnerCharacter, OwnerController 캐싱
	if (AVBCharacter* Owner = Cast<AVBCharacter>(GetOwner()))
	{
		OwnerCharacter  = Owner;
		OwnerController = OwnerCharacter->GetController<APlayerController>();
	}

	if (OwnerCharacter.IsValid())
	{
		// 카메라 기본값 캐시
		DefaultTargetArmLength = OwnerCharacter->GetSpringArm()->TargetArmLength;
		DefaultSocketOffset    = OwnerCharacter->GetSpringArm()->SocketOffset;
		DefaultCameraLagSpeed  = OwnerCharacter->GetSpringArm()->CameraLagSpeed;
		DefaultFieldOfView     = OwnerCharacter->GetCamera()->FieldOfView;

		// FTimeline 초기화 (UCurveFloat 바인딩)
		if (CameraTransitionCurve)
		{
			FOnTimelineFloat ProgressCallback;
			ProgressCallback.BindUFunction(this, FName("OnCameraTransitionUpdate"));
			CameraTransitionTimeline.AddInterpFloat(CameraTransitionCurve, ProgressCallback);
			CameraTransitionTimeline.SetTimelineLength(GetTargetLockConfig()->CameraTransitionDuration);
			CameraTransitionTimeline.SetLooping(false);
		}
		else
		{
			// 커브가 없으면 전투 카메라 전환(팔길이/소켓오프셋/FOV Lerp)이 통째로 스킵된다.
			// 예전엔 이 스킵이 무음이라 FOV 를 180 으로 놓고 라이브로 돌려 보다가 발견됐다(FIND-046).
			// 짝인 CameraTransitionDuration 은 DA 에 있어 눈에 띄는데 커브만 컴포넌트 디테일 패널이라
			// 배선 누락이 다른 화면에 숨는다. 커브를 Config 로 옮기기 전까지는 이 로그가 방어선이다.
			VB_LOG(Warning, "CameraTransitionCurve 미배선 - 전투 카메라 전환이 전부 스킵된다");
		}
	}

}

void UVBTargetLockComponent::OnCameraTransitionUpdate(float Alpha)
{
	// Gameplay Cameras owns camera rig transitions; the retained SpringArm is a rollback path.
	if (!OwnerCharacter.IsValid() || OwnerCharacter->IsUsingGameplayCamera()) return;
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();

	if (USpringArmComponent* SpringArm = OwnerCharacter->GetSpringArm())
	{
		SpringArm->TargetArmLength = FMath::Lerp(DefaultTargetArmLength, Cfg->CombatTargetArmLength, Alpha);
		SpringArm->SocketOffset    = FMath::Lerp(DefaultSocketOffset, Cfg->CombatSocketOffset, Alpha);
		SpringArm->CameraLagSpeed  = FMath::Lerp(DefaultCameraLagSpeed, Cfg->CombatCameraLagSpeed, Alpha);
	}

	if (UCameraComponent* Camera = OwnerCharacter->GetCamera())
	{
		Camera->SetFieldOfView(FMath::Lerp(DefaultFieldOfView, Cfg->CombatFieldOfView, Alpha));
	}
}


void UVBTargetLockComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelHitRecenter();
	if (OwnerCharacter.IsValid() && OwnerCharacter->GetInputDeviceManager())
		OwnerCharacter->GetInputDeviceManager()->OnInputDeviceChanged.RemoveDynamic(this, &ThisClass::OnInputDeviceChanged);
	if (GetOwner() && GetOwner()->HasAuthority() && bGameplayEffectsApplied)
	{
		RemoveLockOnGameplayEffects();
		bGameplayEffectsApplied = false;
	}
	Super::EndPlay(EndPlayReason);
}

AActor* UVBTargetLockComponent::FindSoftLungeTarget() const
{
	// 락온 없이 공격 시작 시 첫 스윙이 돌진할 정면 최근접 적을 고른다(GoW/DMC식 소프트 타겟).
	// 돌진은 후보를 가리지 않는다 - 콘 안에 있으면 아무나 대상이다. 그래서 술어가 항상 참이다.
	return FindConeTarget([](const AActor*) { return true; });
}

AActor* UVBTargetLockComponent::FindConeTarget(TFunctionRef<bool(const AActor*)> Predicate) const
{
	// 서버/클라가 같은 방향 규칙을 쓰되, 서로 다른 월드 스냅샷의 결과 일치까지 보장하지 않는다.
	if (!OwnerCharacter.IsValid()) return nullptr;

	const UVBTargetLockConfig* Cfg = GetTargetLockConfig(); // CDO 폴백 — 절대 null 아님

	TArray<AActor*>                       OverlapResults;
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(Cfg->SearchTraceChannel)); // config 외부화(현행 ECC_Pawn)
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(OwnerCharacter.Get());

	const FVector Origin  = OwnerCharacter->GetActorLocation();
	const FVector Forward = OwnerCharacter->GetActorForwardVector(); // 카메라가 아니라 액터 forward

	if (!UKismetSystemLibrary::SphereOverlapActors(
	                                               OwnerCharacter.Get(),
	                                               Origin,
	                                               Cfg->SoftLungeRadius,
	                                               ObjectTypes,
	                                               nullptr,
	                                               IgnoreActors,
	                                               OverlapResults))
	{
		return nullptr;
	}

	const float CosLimit  = FMath::Cos(FMath::DegreesToRadians(Cfg->SoftLungeAngle));
	AActor*     BestTarget = nullptr;
	float       BestScore  = -1.0f;

	for (AActor* Candidate : OverlapResults)
	{
		if (!Cast<IVBTargetable>(Candidate)) continue;
		if (!IsTargetEligible(Candidate)) continue; // 소프트 런지는 자체 반경을 사용한다.
		if (!Predicate(Candidate)) continue;     // 소비자별 「쓸 수 있는 후보」. 점수 경쟁 안에서 걸러야 한다

		const FVector Dir = (Candidate->GetActorLocation() - Origin).GetSafeNormal();
		const float   Dot = FVector::DotProduct(Forward, Dir);
		if (Dot < CosLimit) continue; // 정면 콘 밖 제외

		const float Distance   = FVector::Dist(Origin, Candidate->GetActorLocation());
		const float FinalScore  = Dot + (1.0f - Distance / Cfg->SoftLungeRadius) * Cfg->DistanceScoreWeight;
		if (FinalScore > BestScore)
		{
			BestScore  = FinalScore;
			BestTarget = Candidate;
		}
	}

	return BestTarget; // 상태 변이 0 — 워프 전용
}


void UVBTargetLockComponent::ApplyLockOnGameplayEffects()
{
	// 서버 전용 — ASC 태그 + 전투 자극(WSC) + Sprint 해제. RemoveLockOnGameplayEffects와 대칭.
	if (!OwnerCharacter.IsValid()) return;

	if (OwnerCharacter->GetIsSprinting())
	{
		OwnerCharacter->StopSprint();
	}
	// 락온 = 전투 자극 — 평상이면 상태2(전투·무기Off) 진입, 발도는 하지 않는다 (DEC-006 ①)
	OwnerCharacter->NotifyCombatStimulus();

	if (AVBPlayerState* PS = Cast<AVBPlayerState>(OwnerCharacter->GetPlayerState()))
	{
		if (UAbilitySystemComponent* ASC = PS->GetVBAbilitySystemComponent())
		{
			ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_LockedOn, 1, EGameplayTagReplicationState::CountToOwner);
		}
	}
}

void UVBTargetLockComponent::RemoveLockOnGameplayEffects()
{
	// 서버 전용 — ASC 태그 제거. CombatMode는 타이머 기반이라 여기서 강제 해제 X.
	if (!OwnerCharacter.IsValid()) return;

	if (AVBPlayerState* PS = Cast<AVBPlayerState>(OwnerCharacter->GetPlayerState()))
	{
		if (UAbilitySystemComponent* ASC = PS->GetVBAbilitySystemComponent())
		{
			ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_LockedOn, 1, EGameplayTagReplicationState::CountToOwner);
		}
	}
}


void UVBTargetLockComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBTargetLockComponent, LockState, COND_OwnerOnly, REPNOTIFY_Always);
}
