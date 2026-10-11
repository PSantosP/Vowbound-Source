// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/TimelineComponent.h"
#include "Curves/CurveFloat.h"
#include "Templates/Function.h"
#include "CommonInputTypeEnum.h"
#include "Character/VBTargetLockPolicy.h"
#include "VBTargetLockComponent.generated.h"

class AVBCharacter;
class APlayerController;
class UVBTargetLockConfig;

UENUM()
enum class EVBLockReason : uint8
{
	Manual, Switch, Guard, Hit, Parry, Reacquire, TargetDied, InvalidTarget, OutOfRange, Occluded, Gameplay, OwnerChanged
};

// 대상과 revision은 한 속성으로 복제한다. 카메라 중단은 전투 대상을 지우지 않는다.
USTRUCT()
struct FVBTargetLockState
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<AActor> Target = nullptr;
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() EVBLockReason Reason = EVBLockReason::Gameplay;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBTargetLockComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBTargetLockComponent();
	void ToggleLockOn();
	void SwitchTarget(float AxisValue);
	void HandleSwitchInput(float AxisValue, bool bGamepad);
	void ResetSwitchInput();
	void AddLookInput(float Yaw, float Pitch, bool bGamepad);
	bool CanTrackCamera() const;
	// PlayerController의 회전 단계에서 호출하여 카메라 평가/이동 전에 합성한다.
	void UpdateCameraRotation(float DeltaTime);
	AActor* GetLockedTarget() const;
	bool IsLockedOn() const;
	void ClearLockOn();
	void TryAutoLockOnTarget(AActor* Target);
	// 서버에서 확인한 근접 피해를 소유자 화면에만 전달한다. 락 상태/공격 대상은 바꾸지 않는다.
	void NotifyMeleeHitForCamera(AActor* Target);
	void NotifyManualCameraInput(float Yaw, float Pitch);
	void UpdateHitRecenter();
	void TryAutoLockOnParryTarget(AActor* Target);
	UFUNCTION(BlueprintCallable, Category="Vowbound|LockOn")
	void TryLockOnFromLocalView();
	AActor* FindSoftLungeTarget() const;
	AActor* FindConeTarget(TFunctionRef<bool(const AActor*)> Predicate) const;
	const UVBTargetLockConfig* GetTargetLockConfig() const;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetLockChanged, AActor*, TargetActor);
	UPROPERTY(BlueprintAssignable, Category="Vowbound|LockOn") FOnTargetLockChanged OnTargetLocked;
	UPROPERTY(BlueprintAssignable, Category="Vowbound|LockOn") FOnTargetLockChanged OnTargetUnlocked;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|LockOn") TObjectPtr<UVBTargetLockConfig> LockConfig;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Camera") TObjectPtr<UCurveFloat> CameraTransitionCurve;
private:
	UFUNCTION(Server, Reliable)
	void ServerRequestLock(AActor* Target, EVBLockReason Reason, uint32 RequestId, uint32 ExpectedRevision);
	UFUNCTION(Client, Reliable)
	void ClientLockResponse(uint32 RequestId, FVBTargetLockState State);
	UFUNCTION(Client, Unreliable)
	void ClientRecenterOnHit(AActor* Target, APlayerController* HitController, double ServerHitTime);
	UFUNCTION() void OnRep_LockState();
	UFUNCTION() void OnInputDeviceChanged(ECommonInputType NewDeviceType);
	UFUNCTION() void OnCameraTransitionUpdate(float Alpha);
	void RequestLock(AActor* Target, EVBLockReason Reason);
	void SetLockOn(AActor* Target, EVBLockReason Reason);
	void AcceptState(const FVBTargetLockState& State);
	void RefreshOwner();
	void ResetCameraInput();
	bool IsTargetEligible(AActor* Actor) const;
	bool IsTargetValid(AActor* Actor) const;
	bool CanOwnerLock() const;
	bool CanUseCombatCamera() const;
	void CancelHitRecenter(bool bSuppressNew = false);
	FVector GetAimPoint(AActor* Target) const;
	bool HasLineOfSight(AActor* Target, const FVector& Start) const;
	bool ProjectTarget(AActor* Target, FVector2D& OutScreen, bool bRequireOnScreen) const;
	AActor* FindScreenTarget(float Direction);
	AActor* FindBestTarget();
	AActor* FindSideTarget(float AxisValue);
	void ApplyLockOnGameplayEffects();
	void RemoveLockOnGameplayEffects();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing=OnRep_LockState) FVBTargetLockState LockState;
	// RPC와 속성 복제 순서가 달라도 최신 확정 상태만 소비한다.
	UPROPERTY() FVBTargetLockState ConfirmedState;
	UPROPERTY() TWeakObjectPtr<AVBCharacter> OwnerCharacter;
	UPROPERTY() TWeakObjectPtr<APlayerController> OwnerController;
	uint32 NextRequestId = 0;
	uint32 PendingRequestId = 0;
	uint32 LastServerRequestId = 0;
	bool bSuppressCameraUntilResponse = false;
	bool bDeathReacquirePending = false;
	bool bGameplayEffectsApplied = false;
	bool bHasVisibleAimPoint = false;
	bool bWasCameraTracking = false;
	FVector LastVisibleAimPoint = FVector::ZeroVector;
	FVector2D LookOffset = FVector2D::ZeroVector;
	float OccludedSeconds = 0.0f;
	float VisibilityCheckElapsed = 0.0f;
	bool bTargetVisible = true;
	double LastLookInputTime = -1.0;
	double SuppressHitUntil = -1.0;
	VBTargetLockPolicy::FStickLatch SwitchLatch;
	VBTargetLockPolicy::FHitRecenterWindow HitRecenterWindow;
	TWeakObjectPtr<AActor> HitRecenterTarget;
	FRotator HitRecenterStartRotation = FRotator::ZeroRotator;
	FRotator HitRecenterDelta = FRotator::ZeroRotator;
	double LastReceivedHitTime = -DBL_MAX;
	FTimeline CameraTransitionTimeline;
	float DefaultTargetArmLength = 0.0f;
	FVector DefaultSocketOffset = FVector::ZeroVector;
	float DefaultCameraLagSpeed = 0.0f;
	float DefaultFieldOfView = 0.0f;
};
