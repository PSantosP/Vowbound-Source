#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "GameplayInteractionContext.h"
#include "Interaction/VBGASPSmartObjectTypes.h"
#include "PoseSearch/PoseSearchFeatureChannel_Distance.h"
#include "VBGASPSmartObjectTasks.generated.h"

class AActor;
class AAIController;
class UProxyTable;
class UPathFollowingComponent;
class UVBGASPSmartObjectComponent;
struct FAIRequestID;
struct FPathFollowingResult;

UENUM()
enum class EVBGASPSmartObjectTaskOperation : uint8
{
	FindSmartObject,
	ClaimSlot,
	ClearHandle,
	FindSlotTransforms,
	PlayAnimMontage,
	PlayAnimFromBestCost,
	FocusToTarget,
	ClearFocus,
	IgnoreCollisions,
	AddCooldown,
	UseSmartObject,
	SetCharacterInputState,
	FindRandomLocation
};

/** Native implementation behind the sample StateTree's existing node IDs/bindings. */
UCLASS(meta=(DisplayName="GASP Smart Object Task"))
class VOWBOUND_API UVBGASPSmartObjectTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()
public:
	UVBGASPSmartObjectTask(const FObjectInitializer& ObjectInitializer);
	UPROPERTY(EditAnywhere, Category="Parameter") EVBGASPSmartObjectTaskOperation Operation = EVBGASPSmartObjectTaskOperation::FindSmartObject;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> Actor;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> Character;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AAIController> AIController;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> SmartObjectActor;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FSmartObjectSlotHandle SlotToBeClaimed;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FSmartObjectClaimHandle ClaimHandle;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FSmartObjectClaimHandle SlotHandle;
	UPROPERTY(EditAnywhere, Category="Output") FSmartObjectClaimHandle ClaimedHandle;
	UPROPERTY(EditAnywhere, Category="Output") FSmartObjectSlotHandle CandidateSlot;
	UPROPERTY(EditAnywhere, Category="Output") TObjectPtr<AActor> SmartObject;
	UPROPERTY(EditAnywhere, Category="Output") FTransform OutSlotTransform;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FVector SearchBoxExtents = FVector(2000.0);
	// Original SmartObjectSearchType: closest / first / random / farthest.
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) uint8 SearchType = 0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FGameplayTagContainer SmartObjectUserTags;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<UAnimMontage> MontageToPlay;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) float StartTime = 0.f;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double PlayTime = 0.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double PlayTimeVariance = 0.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) float PlayRate = 1.f;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) int32 NumberOfLoops = 0;
	// Retained for source asset mapping; the sample montage task never reads this flag.
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) bool IgnoreCollision = false;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<UProxyTable> AnimationProxyTable;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double CostThreshold = 0.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FTransform Destination;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double MaximumDistanceThreshold = 300.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double MinimumVelocityCheck = 100.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> TargetToFocus;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> OtherActor;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) bool ShouldIgnore = false;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FString CooldownName;
	// Unconnected in STT_AddCooldown; the source uses CooldownName as its map key.
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FGameplayTag CooldownTag;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double CooldownTime = 0.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) bool WantsToWalk = false;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AAIController> AIReference;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) float SearchRadius = 0.f;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) float SearchExcludeDistance = 0.f;
	UPROPERTY(EditAnywhere, Category="Output") FVector RandomLocation = FVector::ZeroVector;
	// Source LoctionAwayFromSmartObject hardwires BP_SmartObject_Base; authoring supplies that class contract.
	UPROPERTY(EditAnywhere, Category="Parameter") TSubclassOf<AActor> ExcludedSmartObjectClass;
	UPROPERTY(EditAnywhere, Category="Parameter", meta=(ClampMin="1")) int32 MaxLocationAttempts = 32;

protected:
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;

private:
	AActor* ResolveActor(const FStateTreeExecutionContext& Context) const;
	EStateTreeRunStatus TickBestCost();
	EStateTreeRunStatus TickAnimation();
	EStateTreeRunStatus FindRandomLocation();
	void EndInteraction(bool bAborted);
	void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result);
	UPROPERTY(Transient) TObjectPtr<AActor> RuntimeActor;
	UPROPERTY(Transient) TObjectPtr<UVBGASPSmartObjectComponent> Component;
	UPROPERTY(Transient) TObjectPtr<UPathFollowingComponent> PathFollowing;
	UPROPERTY(Transient) FGameplayInteractionContext Interaction;
	FDelegateHandle MoveCompletedHandle;
	uint32 AnimationSerial = 0;
	bool bInteractionActive = false;
	bool bPathFailed = false;
};

UCLASS(meta=(DisplayName="GASP Get AI Data"))
class VOWBOUND_API UVBGASPGetAIDataEvaluator : public UStateTreeEvaluatorBlueprintBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> Actor;
	UPROPERTY(EditAnywhere, Category="Output") TObjectPtr<AAIController> AIController;
protected:
	virtual void TreeStart(FStateTreeExecutionContext& Context) override;
};

UCLASS(meta=(DisplayName="GASP Check Cooldown"))
class VOWBOUND_API UVBGASPCheckCooldownCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AAIController> AIController;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FString CooldownName;
protected:
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

/** Uses the native Chooser input; indexing and weighting remain the engine Distance channel's. */
UCLASS(EditInlineNew, meta=(DisplayName="GASP Distance To Smart Object"))
class VOWBOUND_API UVBGASPSmartObjectDistanceChannel : public UPoseSearchFeatureChannel_Distance
{
	GENERATED_BODY()
public:
	virtual void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext) const override;
};
