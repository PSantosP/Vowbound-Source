#include "Interaction/VBGASPSmartObjectTasks.h"

#include "AIController.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Character/VBCharacter.h"
#include "Enemy/VBEnemyBase.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchFeatureVectorHelper.h"
#include "SmartObjectComponent.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeExecutionContext.h"

namespace
{
	AActor* PawnOrActor(AActor* Actor)
	{
		if (const AController* Controller = Cast<AController>(Actor))
			if (APawn* Pawn = Controller->GetPawn()) return Pawn;
		return Actor;
	}

	UVBGASPSmartObjectComponent* SmartComponent(AActor* Actor)
	{
		Actor = PawnOrActor(Actor);
		return Actor ? Actor->FindComponentByClass<UVBGASPSmartObjectComponent>() : nullptr;
	}
}

UVBGASPSmartObjectTask::UVBGASPSmartObjectTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The engine wrapper calls these native virtuals. No Blueprint event dispatch is used.
	bShouldCallTick = true;
}

AActor* UVBGASPSmartObjectTask::ResolveActor(const FStateTreeExecutionContext& Context) const
{
	if (Actor) return PawnOrActor(Actor);
	if (Character) return PawnOrActor(Character);
	if (AIController) return PawnOrActor(AIController);
	if (AIReference) return PawnOrActor(AIReference);
	UObject* Owner = Context.GetOwner();
	if (AActor* OwnerActor = Cast<AActor>(Owner)) return PawnOrActor(OwnerActor);
	if (const UActorComponent* OwnerComponent = Cast<UActorComponent>(Owner)) return PawnOrActor(OwnerComponent->GetOwner());
	return nullptr;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	RunStatus = EStateTreeRunStatus::Running;
	AnimationSerial = 0;
	bPathFailed = false;
	RuntimeActor = ResolveActor(Context);
	Component = SmartComponent(RuntimeActor);
	if (!RuntimeActor || !RuntimeActor->HasAuthority()) return RunStatus = EStateTreeRunStatus::Failed;
	USmartObjectSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<USmartObjectSubsystem>();
	using Op = EVBGASPSmartObjectTaskOperation;
	switch (Operation)
	{
	case Op::FindSmartObject:
	{
		CandidateSlot = {};
		AActor* Object = nullptr;
		const bool bFound = Component && Component->FindSlot(SearchBoxExtents, SmartObjectUserTags, SearchType, CandidateSlot, Object);
		SmartObject = Object;
		return RunStatus = bFound ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
	}
	case Op::ClaimSlot:
		ClaimedHandle = {};
		if (!Subsystem || !Component || !SlotToBeClaimed.IsValid()) return RunStatus = EStateTreeRunStatus::Failed;
		ClaimedHandle = Subsystem->MarkSlotAsClaimed(SlotToBeClaimed, ESmartObjectClaimPriority::Normal,
			FConstStructView::Make(FSmartObjectActorUserData(RuntimeActor)));
		if (!ClaimedHandle.IsValid() || !Component->AdoptClaim(ClaimedHandle))
		{
			if (ClaimedHandle.IsValid()) Subsystem->MarkSlotAsFree(ClaimedHandle);
			ClaimedHandle.Invalidate();
			return RunStatus = EStateTreeRunStatus::Failed;
		}
		// The source claim task stays active while child states consume its output.
		return RunStatus;
	case Op::ClearHandle:
	{
		const bool bOwned = Component && Component->OwnsClaim(ClaimHandle);
		if (bOwned) Component->ReleaseClaim();
		return RunStatus = bOwned ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
	}
	case Op::FindSlotTransforms:
		if (!Subsystem || !Subsystem->GetSlotTransform(ClaimHandle, OutSlotTransform)) return RunStatus = EStateTreeRunStatus::Failed;
		return RunStatus;
	case Op::PlayAnimMontage:
	{
		// The source explicitly completes successfully when no montage is assigned.
		if (!MontageToPlay) return RunStatus = EStateTreeRunStatus::Succeeded;
		if (!Component) return RunStatus = EStateTreeRunStatus::Failed;
		FVBGASPSmartObjectAnimation Payload;
		Payload.MontageToPlay = MontageToPlay;
		Payload.StartTime = StartTime;
		Payload.PlayTime = PlayTime;
		Payload.RandomPlaytimeVariance = PlayTimeVariance;
		Payload.PlayRate = PlayRate;
		Payload.NumLoops = NumberOfLoops;
		Payload.bUseWarpTarget = Subsystem && Subsystem->GetSlotTransform(SlotHandle, Payload.WarpTargetTransform);
		AnimationSerial = Component->PlayAnimation(Payload);
		return RunStatus = AnimationSerial ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
	}
	case Op::PlayAnimFromBestCost:
	{
		if (!Component || !AnimationProxyTable) return RunStatus = EStateTreeRunStatus::Failed;
		if (AAIController* Controller = UAIBlueprintHelperLibrary::GetAIController(RuntimeActor))
		{
			PathFollowing = Controller->GetPathFollowingComponent();
			if (PathFollowing) MoveCompletedHandle = PathFollowing->OnRequestFinished.AddUObject(this, &ThisClass::OnMoveCompleted);
		}
		// Like Setup Task, EnterState only subscribes; the distance/cost gate is evaluated on Tick.
		return RunStatus;
	}
	case Op::FocusToTarget:
		if (IsValid(AIController) && IsValid(TargetToFocus)) AIController->SetFocus(TargetToFocus);
		return RunStatus;
	case Op::ClearFocus:
		if (IsValid(AIController)) AIController->ClearFocus(EAIFocusPriority::Gameplay);
		return RunStatus;
	case Op::IgnoreCollisions:
		if (!Component || !OtherActor) return RunStatus = EStateTreeRunStatus::Failed;
		Component->SetIgnoreActor(OtherActor, ShouldIgnore);
		return RunStatus = EStateTreeRunStatus::Succeeded;
	case Op::AddCooldown:
		if (Component) Component->AddCooldown(FName(*CooldownName), CooldownTime);
		return RunStatus = EStateTreeRunStatus::Succeeded;
	case Op::UseSmartObject:
	{
		if (!Subsystem || !Component || !Subsystem->IsClaimedSmartObjectValid(ClaimHandle)) return RunStatus = EStateTreeRunStatus::Failed;
		if (!Component->OwnsClaim(ClaimHandle) && !Component->AdoptClaim(ClaimHandle)) return RunStatus = EStateTreeRunStatus::Failed;
		const auto* Definition = Subsystem->MarkSlotAsOccupied<UGameplayInteractionSmartObjectBehaviorDefinition>(ClaimHandle);
		const USmartObjectComponent* Object = Subsystem->GetSmartObjectComponent(ClaimHandle);
		if (!Definition || !Object)
		{
			Component->ReleaseClaim();
			return RunStatus = EStateTreeRunStatus::Failed;
		}
		Interaction = {};
		Interaction.SetContextActor(RuntimeActor);
		Interaction.SetSmartObjectActor(Object->GetOwner());
		Interaction.SetClaimedHandle(ClaimHandle);
		// The component owns slot invalidation. Registering another callback here would replace it.
		bInteractionActive = true;
		if (!Interaction.Activate(*Definition))
		{
			EndInteraction(true);
			return RunStatus = EStateTreeRunStatus::Failed;
		}
		return RunStatus;
	}
	case Op::SetCharacterInputState:
		if (Component) Component->CaptureInputState();
		if (AVBCharacter* Player = Cast<AVBCharacter>(RuntimeActor)) Player->SetGASPSmartObjectWalkState(WantsToWalk);
		else if (AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(RuntimeActor))
		{
			Enemy->UnCrouch();
			Enemy->ApplyGait(WantsToWalk ? EVBGait::Walk : EVBGait::Run);
		}
		return RunStatus;
	case Op::FindRandomLocation:
		return RunStatus = FindRandomLocation();
	}
	return RunStatus = EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::FindRandomLocation()
{
	if (!AIReference || !ExcludedSmartObjectClass || !FMath::IsFinite(SearchRadius) || !FMath::IsFinite(SearchExcludeDistance))
		return EStateTreeRunStatus::Failed;
	TArray<AActor*> ExcludedActors;
	UGameplayStatics::GetAllActorsOfClass(RuntimeActor, ExcludedSmartObjectClass, ExcludedActors);
	// Source calls Actor.K2_GetActorLocation on the controller, not on its pawn.
	// AController hides transform helpers; use the public AActor view of the same object.
	const FVector Origin = static_cast<const AActor*>(AIReference.Get())->GetActorLocation();
	// Source retries synchronously forever. Bound that loop so an impossible exclusion area cannot freeze the server.
	for (int32 Attempt = 0; Attempt < FMath::Max(1, MaxLocationAttempts); ++Attempt)
	{
		if (!UNavigationSystemV1::K2_GetRandomReachablePointInRadius(RuntimeActor, Origin, RandomLocation, SearchRadius)) RandomLocation = Origin;
		const bool bTooClose = ExcludedActors.ContainsByPredicate([this](const AActor* Object)
		{
			return IsValid(Object) && FVector::Distance(RandomLocation, Object->GetActorLocation()) < SearchExcludeDistance;
		});
		if (!bTooClose) return EStateTreeRunStatus::Running;
	}
	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::TickAnimation()
{
	if (!Component || AnimationSerial == 0) return EStateTreeRunStatus::Failed;
	bool bSucceeded = false;
	return Component->IsAnimationFinished(AnimationSerial, bSucceeded)
		? (bSucceeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed)
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::TickBestCost()
{
	if (!Component || !RuntimeActor || bPathFailed) return EStateTreeRunStatus::Failed;
	if (AnimationSerial) return TickAnimation();
	double Distance = FVector::Distance(RuntimeActor->GetActorLocation(), Destination.GetLocation());
	if (AAIController* Controller = UAIBlueprintHelperLibrary::GetAIController(RuntimeActor))
		if (const UNavigationPath* Path = UAIBlueprintHelperLibrary::GetCurrentPath(Controller)) Distance = Path->GetPathLength();
	if (Distance > MaximumDistanceThreshold || RuntimeActor->GetVelocity().Size() < MinimumVelocityCheck) return EStateTreeRunStatus::Running;
	UAnimMontage* Montage = nullptr;
	float Cost = 0.f, SelectedStartTime = 0.f;
	if (!Component->EvaluateAnimation(AnimationProxyTable, Destination, Montage, Cost, SelectedStartTime)) return EStateTreeRunStatus::Running;
	// Original dedicated-server branch accepts the first valid distance-matched pose.
	if (RuntimeActor->GetNetMode() != NM_DedicatedServer && Cost > CostThreshold) return EStateTreeRunStatus::Running;
	FVBGASPSmartObjectAnimation Payload;
	Payload.MontageToPlay = Montage;
	Payload.StartTime = SelectedStartTime;
	if (const USmartObjectSubsystem* Subsystem = RuntimeActor->GetWorld()->GetSubsystem<USmartObjectSubsystem>())
		Payload.bUseWarpTarget = Subsystem->GetSlotTransform(ClaimHandle, Payload.WarpTargetTransform);
	AnimationSerial = Component->PlayAnimation(Payload);
	return AnimationSerial ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime)
{
	if (RunStatus != EStateTreeRunStatus::Running) return RunStatus;
	using Op = EVBGASPSmartObjectTaskOperation;
	if (Operation == Op::PlayAnimMontage) return RunStatus = TickAnimation();
	if (Operation == Op::PlayAnimFromBestCost) return RunStatus = TickBestCost();
	if (Operation == Op::UseSmartObject)
	{
		USmartObjectSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<USmartObjectSubsystem>();
		if (!bInteractionActive || !Component || !Component->OwnsClaim(ClaimHandle) || !Subsystem || !Subsystem->IsClaimedSmartObjectValid(ClaimHandle))
		{
			EndInteraction(true);
			return RunStatus = EStateTreeRunStatus::Failed;
		}
		if (!Interaction.Tick(DeltaTime))
		{
			RunStatus = Interaction.GetLastRunStatus() == EStateTreeRunStatus::Succeeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
			EndInteraction(RunStatus != EStateTreeRunStatus::Succeeded);
		}
	}
	return RunStatus;
}

void UVBGASPSmartObjectTask::EndInteraction(bool bAborted)
{
	if (!bInteractionActive) return;
	bInteractionActive = false;
	if (bAborted)
	{
		FGameplayInteractionAbortContext Abort;
		Abort.Reason = EGameplayInteractionAbortReason::ExternalAbort;
		Interaction.SetAbortContext(Abort);
	}
	Interaction.Deactivate();
	if (Component && Component->OwnsClaim(ClaimHandle)) Component->ReleaseClaim();
	Interaction = {};
}

void UVBGASPSmartObjectTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	if (PathFollowing && MoveCompletedHandle.IsValid()) PathFollowing->OnRequestFinished.Remove(MoveCompletedHandle);
	MoveCompletedHandle.Reset();
	PathFollowing = nullptr;
	EndInteraction(RunStatus != EStateTreeRunStatus::Succeeded);
	if (Component)
	{
		if (AnimationSerial && RunStatus != EStateTreeRunStatus::Succeeded) Component->StopAnimation(AnimationSerial);
		if (Operation == EVBGASPSmartObjectTaskOperation::ClaimSlot && Component->OwnsClaim(ClaimedHandle)) Component->ReleaseClaim();
		// A search can fail after input capture but before any slot is claimed.
		if (Operation == EVBGASPSmartObjectTaskOperation::SetCharacterInputState && !Component->IsInteracting()) Component->ReleaseClaim();
	}
	AnimationSerial = 0;
	Component = nullptr;
	RuntimeActor = nullptr;
}

void UVBGASPSmartObjectTask::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	// The source ignores Success and Aborted; distance matching is expected to win before arrival.
	bPathFailed |= Result.Code == EPathFollowingResult::Blocked || Result.Code == EPathFollowingResult::OffPath || Result.Code == EPathFollowingResult::Invalid;
}

void UVBGASPGetAIDataEvaluator::TreeStart(FStateTreeExecutionContext& Context)
{
	AIController = UAIBlueprintHelperLibrary::GetAIController(Actor);
}

bool UVBGASPCheckCooldownCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const UVBGASPSmartObjectComponent* Component = SmartComponent(AIController);
	// As in the source, a missing controller/cooldown entry permits selection.
	return !Component || Component->HasCooldownExpired(FName(*CooldownName));
}

void UVBGASPSmartObjectDistanceChannel::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext) const
{
	float Distance = 0.f;
	if (const FChooserEvaluationContext* Chooser = SearchContext.GetContext(SampleRole))
	{
		for (const FStructView& Param : Chooser->Params)
		{
			if (const auto* Input = Param.GetPtr<FVBGASPSmartObjectSelectionInputs>())
			{
				Distance = Input->TargetDistance;
				break;
			}
		}
	}
	UE::PoseSearch::FFeatureVectorHelper::EncodeFloat(SearchContext.EditFeatureVector(), ChannelDataOffset, Distance);
}
