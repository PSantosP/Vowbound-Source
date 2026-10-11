#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/VBGASPSmartObjectTypes.h"
#include "VBGASPSmartObjectComponent.generated.h"
class UProxyTable;
class UProxyAsset;
class UStateTree;
class UStateTreeComponent;
class UAbilitySystemComponent;
struct FBranchingPointNotifyPayload;

/** Server-selected SmartObject slots; CMC montage/warp lifecycle replaces AC_SmartObjectAnimation. */
UCLASS(ClassGroup=(Interaction), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBGASPSmartObjectComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBGASPSmartObjectComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION(BlueprintCallable, Category="Smart Object") bool TryInteract();
	UFUNCTION(BlueprintCallable, Category="Smart Object") bool RequestExit();
	UFUNCTION(BlueprintPure, Category="Smart Object") bool IsInteracting() const { return bInteracting; }
	bool OwnsCapsuleFacing() const { return bHasSavedCorrectionFlag; }
	void AbortInteraction();
	void CaptureInputState();
	/** Reapply replicated ownership after PlayerState/ASC actor info becomes available. */
	void RefreshAbilityState();
	bool FindSlot(FVector Extents, const FGameplayTagContainer& Tags, uint8 SearchType, FSmartObjectSlotHandle& OutSlot, AActor*& OutActor) const;
	bool AdoptClaim(FSmartObjectClaimHandle Handle);
	bool OwnsClaim(const FSmartObjectClaimHandle& Handle) const { return Claim.IsValid() && Claim == Handle; }
	void ReleaseClaim();
	uint32 PlayAnimation(FVBGASPSmartObjectAnimation Payload);
	void StopAnimation(uint32 Serial);
	bool IsAnimationFinished(uint32 Serial, bool& bSucceeded) const;
	bool EvaluateAnimation(UProxyTable* Table, const FTransform& Destination, UAnimMontage*& Montage, float& Cost, float& StartTime) const;
	void SetIgnoreActor(AActor* OtherActor, bool bIgnore);
	void AddCooldown(FName Name, float Duration);
	bool HasCooldownExpired(FName Name) const;
	UPROPERTY(EditDefaultsOnly, Category="Smart Object") TObjectPtr<UStateTree> PlayerStateTree;
	UPROPERTY(EditDefaultsOnly, Category="Smart Object") TObjectPtr<UProxyAsset> SelectionProxy;
	UPROPERTY(EditDefaultsOnly, Category="Smart Object") FName PoseHistoryName = TEXT("PoseHistory");
	UPROPERTY(EditDefaultsOnly, Category="Smart Object", meta=(ClampMin="1")) float InteractionDistance = 200.f;
	UPROPERTY(EditDefaultsOnly, Category="Smart Object") float InteractionTraceStartOffset = 15.f;
	UPROPERTY(EditDefaultsOnly, Category="Smart Object") FVector InteractionTraceHalfSize = FVector(1.f, 64.f, 86.f);
	UPROPERTY(EditDefaultsOnly, Category="Smart Object") FName WarpTargetName = TEXT("SmartObject");
private:
	UPROPERTY(ReplicatedUsing=OnRep_Animation) FVBGASPSmartObjectAnimation Animation;
	UPROPERTY(ReplicatedUsing=OnRep_Interacting) bool bInteracting = false;
	UPROPERTY(Transient) TObjectPtr<UStateTreeComponent> PlayerTreeComponent;
	UPROPERTY(Transient) FSmartObjectClaimHandle Claim;
	UPROPERTY(Transient) TObjectPtr<AActor> IgnoredActor;
	UPROPERTY(Transient) TMap<FName, double> Cooldowns;
	UPROPERTY(Transient) TWeakObjectPtr<AController> MovementLockedController;
	UPROPERTY(Transient) TWeakObjectPtr<UAbilitySystemComponent> BlockingASC;
	bool bOwnsAbilityBlock = false;
	bool bCapsuleAlreadyIgnoredActor = false;
	bool bMeshAlreadyIgnoredActor = false;
	bool bHasInputState = false;
	uint8 SavedGait = 0;
	bool bSavedStrafe = false;
	bool bSavedAim = false;
	uint32 NextSerial = 0;
	uint32 FinishedSerial = 0;
	bool bLastAnimationSucceeded = false;
	bool bChangingAnimation = false;
	bool bExitRequested = false;
	bool bSavedCorrectionFlag = false;
	bool bHasSavedCorrectionFlag = false;
	int32 MontageInstanceID = INDEX_NONE;
	int32 LoopCount = 0;
	FTimerHandle PlaybackTimer;
	FTimerHandle RestartTimer;
	UFUNCTION(Server, Reliable) void ServerTryInteract();
	UFUNCTION(Server, Reliable) void ServerRequestExit();
	UFUNCTION(NetMulticast, Reliable) void MulticastIgnoreActor(AActor* OtherActor, bool bIgnore);
	UFUNCTION() void OnRep_Animation();
	UFUNCTION() void OnRep_Interacting();
	UFUNCTION() void OnMontageNotify(FName Name, const FBranchingPointNotifyPayload& Payload);
	void OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);
	void FinishAnimation(bool bSucceeded);
	void RestartAnimation();
	void QueueAnimationRestart();
	void RestorePlaybackState();
	bool PlayLocal();
	bool CanInteract() const;
	bool FindPlayerSlot(FSmartObjectSlotHandle& OutSlot, AActor*& OutActor) const;
	void OnSlotInvalidated(const FSmartObjectClaimHandle& Handle, ESmartObjectSlotState State);
};
