// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/VBAnimationTypes.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "VBGASPAnimNotifies.generated.h"

class UCameraShakeBase;
class UCharacterMovementComponent;
class UPrimitiveComponent;
class USoundAttenuation;
class USoundBase;

UENUM(BlueprintType)
enum class EVBGASPEarlyTransitionCondition : uint8
{
	Always,
	GaitNotEqual
};

UENUM(BlueprintType)
enum class EVBGASPEarlyTransitionDestination : uint8
{
	ReTransition,
	ToLoop
};

UENUM(BlueprintType)
enum class EVBGASPMontageBlendOutCondition : uint8
{
	ForceBlendOut,
	WithMovementInput,
	IfFalling
};

UENUM(BlueprintType)
enum class EVBGASPFoleySide : uint8
{
	None,
	Left,
	Right
};

UCLASS(meta=(DisplayName="VB GASP Early Transition"))
class VOWBOUND_API UVBGASPNotifyState_EarlyTransition : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	EVBGASPEarlyTransitionCondition TransitionCondition = EVBGASPEarlyTransitionCondition::Always;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	EVBGASPEarlyTransitionDestination TransitionDestination = EVBGASPEarlyTransitionDestination::ReTransition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	EVBGait GaitNotEqual = EVBGait::Walk;

	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
};

UCLASS(meta=(DisplayName="VB GASP Montage Blend Out"))
class VOWBOUND_API UVBGASPNotifyState_MontageBlendOut : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	EVBGASPMontageBlendOutCondition BlendOutCondition = EVBGASPMontageBlendOutCondition::ForceBlendOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP", meta=(ClampMin="0"))
	float BlendOutTime = 0.2f;

	/** GASP resolves this on the receiving AnimInstance's skeleton. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	FName BlendProfile = TEXT("FastFeet_InstantRoot");

	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void BranchingPointNotifyTick(FBranchingPointNotifyPayload& Payload, float FrameDeltaTime) override;
};

/** Restores the actual component responses, not the sample-only CharacterCapsule profile. */
UCLASS(meta=(DisplayName="VB GASP Override Capsule Collision"))
class VOWBOUND_API UVBGASPNotifyState_OverrideCapsuleCollision : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UVBGASPNotifyState_OverrideCapsuleCollision();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	TEnumAsByte<ECollisionResponse> NewResponse = ECR_Ignore;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	bool bResetOnEnd = true;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual void BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload) override;
	virtual void BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload) override;
};

/** CMC adaptation of the sample's Mover-only movement-mode notify. */
UCLASS(meta=(DisplayName="VB GASP Override Movement Mode (CMC)"))
class VOWBOUND_API UVBGASPNotifyState_OverrideMovementMode : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UVBGASPNotifyState_OverrideMovementMode();

	/** Original GASP is Mover-only and has no effect on CMC. Enable only for an authored CMC window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	bool bApplyToCMC = false;

	/** Preserved source data; Mover is intentionally not a Vowbound runtime dependency. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GASP|Source")
	FName SourceMoverOverrideMode;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GASP|Source")
	FName SourceMoverEndingMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	TEnumAsByte<EMovementMode> OverrideMovementMode = MOVE_Flying;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP", meta=(EditCondition="OverrideMovementMode == EMovementMode::MOVE_Custom"))
	uint8 OverrideCustomMode = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	bool bRestorePreviousMode = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP", meta=(EditCondition="!bRestorePreviousMode"))
	TEnumAsByte<EMovementMode> EndingMovementMode = MOVE_Falling;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP", meta=(EditCondition="!bRestorePreviousMode && EndingMovementMode == EMovementMode::MOVE_Custom"))
	uint8 EndingCustomMode = 0;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual void BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload) override;
	virtual void BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload) override;
};

UCLASS(meta=(DisplayName="VB GASP Camera Shake"))
class VOWBOUND_API UVBGASPAnimNotify_SimpleCameraShake : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	TSubclassOf<UCameraShakeBase> Shake;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP", meta=(ClampMin="0"))
	float InnerRadius = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP", meta=(ClampMin="0"))
	float OuterRadius = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP", meta=(ClampMin="0"))
	float Falloff = 1.f;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};

/** Data-only counterpart of DABP_FoleyAudioBank::GetSoundFromFoleyEvent (exact tag lookup). */
UCLASS(BlueprintType)
class VOWBOUND_API UVBGASPFoleyBank : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GASP")
	TMap<FGameplayTag, TObjectPtr<USoundBase>> Assets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GASP")
	TObjectPtr<USoundAttenuation> Attenuation;

};

// 옛 UVBGASPFoleyComponent(AC_FoleyEvents 의 C++ 판)는 2026-09-30 에 뗐다 - 발소리는 UVBContextEffectComponent 한 길로 난다
//   (design .sw/context-effects/design.md §10). 뱅크는 남는다: 이 노티파이의 Bank 칸을 자산 1,771개가 들고 있어 애님 에디터 미리보기에만 쓴다.
UCLASS(meta=(DisplayName="VB GASP Foley Event"))
class VOWBOUND_API UVBGASPAnimNotify_FoleyEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	TObjectPtr<UVBGASPFoleyBank> Bank;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	FGameplayTag Event;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP")
	EVBGASPFoleySide Side = EVBGASPFoleySide::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP", meta=(ClampMin="0"))
	float VolumeMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP", meta=(ClampMin="0.01"))
	float PitchMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|Debug")
	FLinearColor VisLogDebugColor = FLinearColor::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|Debug")
	FString VisLogDebugText;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};

// Shared animation assets must not store per-character mutable notify state. This transient,
// non-ticking component owns overlapping windows on one actor and is destroyed with that actor.
struct FVBGASPNotifyWindow
{
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TWeakObjectPtr<UAnimNotifyState> Notify;
	int32 InstanceId = INDEX_NONE;
	uint8 Value = 0;
	uint8 CustomMode = 0;
	bool bRestorePrevious = true;
	uint8 EndingMode = MOVE_Falling;
	uint8 EndingCustomMode = 0;

	bool Matches(USkeletalMeshComponent* InMesh, UAnimNotifyState* InNotify, int32 InInstanceId) const;
};

UCLASS(Transient, NotBlueprintable)
class VOWBOUND_API UVBGASPNotifyStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBGASPNotifyStateComponent();
	static UVBGASPNotifyStateComponent* Find(USkeletalMeshComponent* MeshComp, bool bCreate);
	void BeginCollision(USkeletalMeshComponent* MeshComp, UVBGASPNotifyState_OverrideCapsuleCollision* Notify, int32 InstanceId);
	void EndCollision(USkeletalMeshComponent* MeshComp, UAnimNotifyState* Notify, int32 InstanceId);
	void BeginMovement(USkeletalMeshComponent* MeshComp, UVBGASPNotifyState_OverrideMovementMode* Notify, int32 InstanceId);
	void EndMovement(USkeletalMeshComponent* MeshComp, UAnimNotifyState* Notify, int32 InstanceId);

private:
	bool StillOwnsCollision() const;
	bool StillOwnsMovement() const;
	TArray<FVBGASPNotifyWindow> CollisionWindows;
	TArray<FVBGASPNotifyWindow> MovementWindows;
	TWeakObjectPtr<UPrimitiveComponent> CollisionTarget;
	TWeakObjectPtr<UCharacterMovementComponent> MovementTarget;
	FCollisionResponseContainer OriginalResponses;
	FCollisionResponseContainer AppliedResponses;
	FName OriginalProfile;
	FName AppliedProfile;
	ECollisionEnabled::Type OriginalCollisionEnabled = ECollisionEnabled::NoCollision;
	EMovementMode OriginalMode = MOVE_None;
	uint8 OriginalCustomMode = 0;
	EMovementMode AppliedMode = MOVE_None;
	uint8 AppliedCustomMode = 0;
};
