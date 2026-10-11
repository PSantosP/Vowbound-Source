// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimMontage.h"
#include "Engine/EngineTypes.h"
#include "Engine/NetSerialization.h"
#include "VBPlayerRagdollComponent.generated.h"

class ACharacter;
class UAbilitySystemComponent;
class UVBRagdollComponent;
class UVBRagdollConfig;

UENUM(BlueprintType)
enum class EVBPlayerRagdollPhase : uint8
{
	None,
	Ragdoll,
	GettingUp
};

USTRUCT()
struct FVBPlayerRagdollState
{
	GENERATED_BODY()
	UPROPERTY() EVBPlayerRagdollPhase Phase = EVBPlayerRagdollPhase::None;
	UPROPERTY() FVector_NetQuantize10 EntryVelocity = FVector::ZeroVector;
};

/** CMC player lifecycle. Physics/roll behavior stays in the shared VBRagdollComponent. */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBPlayerRagdollComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBPlayerRagdollComponent();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Ragdoll")
	TObjectPtr<UVBRagdollConfig> Config;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Ragdoll")
	FMontageBlendSettings EntryMontageBlendOut;

	// Original GASP: ragdoll action enters; jump requests recovery while on a floor.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Ragdoll")
	void RequestRagdoll();
	UFUNCTION(BlueprintCallable, Category="Vowbound|Ragdoll")
	void RequestGetUp();
	void SetRollInput(const FVector& WorldInput);

	UFUNCTION(BlueprintPure, Category="Vowbound|Ragdoll")
	bool IsRagdollActive() const { return LocalPhase != EVBPlayerRagdollPhase::None; }
	UFUNCTION(BlueprintPure, Category="Vowbound|Ragdoll")
	bool IsRagdolling() const { return LocalPhase == EVBPlayerRagdollPhase::Ragdoll; }
	UFUNCTION(BlueprintPure, Category="Vowbound|Ragdoll")
	EVBPlayerRagdollPhase GetPhase() const { return LocalPhase; }

	// Called by the recovery GA before montage playback; safe after phase replication.
	bool PrepareForGetUp();
	void FinishRecovery();
	// Call before death/restore takes ownership of mesh, collision or movement.
	void CancelRagdoll();
	// PlayerState/ASC can become available after ragdoll phase replication.
	void RefreshAbilityState();
	static FGameplayTag GetRecoveryEventTag();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION(Server, Reliable) void ServerRequestRagdoll();
	UFUNCTION(Server, Reliable) void ServerRequestGetUp();
	UFUNCTION(Server, Unreliable) void ServerSetRollInput(FVector_NetQuantize10 WorldInput);
	UFUNCTION() void OnRep_State();
	UPROPERTY(ReplicatedUsing=OnRep_State) FVBPlayerRagdollState State;
	UPROPERTY(Replicated) FVector_NetQuantize10 RollInput = FVector::ZeroVector;

	bool StartAuthority();
	bool BeginRagdoll(const FVector& EntryVelocity);
	void StartGetUpAuthority();
	void Restore(bool bCompletedRecovery = false);
	bool ProbeFloor(FVector& OutLocation, FVector& OutNormal) const;
	void SetAbilityBlock(bool bBlock);
	UAbilitySystemComponent* GetASC() const;

	UPROPERTY(Transient) TObjectPtr<ACharacter> Character;
	UPROPERTY(Transient) TObjectPtr<UVBRagdollComponent> Physics;
	UPROPERTY(Transient) TObjectPtr<UAbilitySystemComponent> BlockedASC;
	EVBPlayerRagdollPhase LocalPhase = EVBPlayerRagdollPhase::None;
	FGameplayTagContainer BlockedAbilities;
	FVector LocalRollInput = FVector::ZeroVector;
	double LastRollInputSendTime = -1.0;
	double LastServerRollInputTime = -1.0;
	bool bHasFloor = false;
	bool bSavedState = false;
	bool bMovementTickWasEnabled = false;
	bool bReplicateMovementWasEnabled = false;
	bool bUseControllerYawWasEnabled = false;
	bool bOrientRotationWasEnabled = false;
	bool bControllerDesiredRotationWasEnabled = false;
	bool bRootMotionRotationWasEnabled = false;
	uint8 SavedMovementMode = 0;
	uint8 SavedCustomMovementMode = 0;
	FTransform MeshRelativeTransform;
	FName CapsuleProfile;
	FName MeshProfile;
	TEnumAsByte<ECollisionEnabled::Type> CapsuleCollision;
	TEnumAsByte<ECollisionEnabled::Type> MeshCollision;
	TEnumAsByte<ECollisionChannel> CapsuleObjectType;
	TEnumAsByte<ECollisionChannel> MeshObjectType;
	FCollisionResponseContainer CapsuleResponses;
	FCollisionResponseContainer MeshResponses;
};
