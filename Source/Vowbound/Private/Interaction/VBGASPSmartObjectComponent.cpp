#include "Interaction/VBGASPSmartObjectComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "MotionWarpingComponent.h"
#include "Net/UnrealNetwork.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "ProxyAsset.h"
#include "ProxyTable.h"
#include "SmartObjectComponent.h"
#include "SmartObjectBlueprintFunctionLibrary.h"
#include "SmartObjectRequestTypes.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeEvents.h"
#include "TimerManager.h"

namespace
{
	double ServerTime(const UWorld* World)
	{
		const AGameStateBase* State = World ? World->GetGameState() : nullptr;
		return State ? State->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0.0;
	}
	UAnimInstance* AnimFor(const UActorComponent* Component)
	{
		const ACharacter* Character = Cast<ACharacter>(Component->GetOwner());
		return Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	}
}

UVBGASPSmartObjectComponent::UVBGASPSmartObjectComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}
void UVBGASPSmartObjectComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBGASPSmartObjectComponent, Animation);
	DOREPLIFETIME(UVBGASPSmartObjectComponent, bInteracting);
}
void UVBGASPSmartObjectComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshAbilityState();
}
void UVBGASPSmartObjectComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	AbortInteraction();
	bInteracting = false;
	OnRep_Interacting();
	Super::EndPlay(Reason);
}
bool UVBGASPSmartObjectComponent::CanInteract() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !Character->GetMesh() || Character->GetMesh()->IsAnySimulatingPhysics()) return false;
	// Vowbound traversal/ladder owns the capsule while falling/flying/custom movement.
	if (!Character->GetCharacterMovement() || !Character->GetCharacterMovement()->IsMovingOnGround()) return false;
	if (const UVBPlayerRagdollComponent* Ragdoll = Character->FindComponentByClass<UVBPlayerRagdollComponent>(); Ragdoll && Ragdoll->IsRagdollActive()) return false;
	if (const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))
	{
		if (ASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead) || ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking)) return false;
	}
	const UVBFinisherComponent* Finisher = Character->FindComponentByClass<UVBFinisherComponent>();
	return !Finisher || !Finisher->IsPlaying();
}
bool UVBGASPSmartObjectComponent::FindSlot(FVector Extents, const FGameplayTagContainer& Tags, uint8 SearchType, FSmartObjectSlotHandle& OutSlot, AActor*& OutActor) const
{
	USmartObjectSubsystem* Subsystem = GetWorld()->GetSubsystem<USmartObjectSubsystem>();
	if (!Subsystem || !CanInteract()) return false;
	FSmartObjectRequestFilter Filter;
	Filter.UserTags = Tags;
	Filter.BehaviorDefinitionClasses.Add(UGameplayInteractionSmartObjectBehaviorDefinition::StaticClass());
	TArray<FSmartObjectRequestResult> Results;
	Subsystem->FindSmartObjects(FSmartObjectRequest(FBox::BuildAABB(GetOwner()->GetActorLocation(), Extents), Filter), Results,
		FConstStructView::Make(FSmartObjectActorUserData(GetOwner())));
	if (Results.IsEmpty()) return false;
	if (SearchType == 0 || SearchType == 3)
	{
		Results.Sort([&](const FSmartObjectRequestResult& A, const FSmartObjectRequestResult& B)
		{
			const auto TA = Subsystem->GetSlotTransform(A), TB = Subsystem->GetSlotTransform(B);
			const double DA = TA.IsSet() ? FVector::DistSquared(TA->GetLocation(), GetOwner()->GetActorLocation()) : DBL_MAX;
			const double DB = TB.IsSet() ? FVector::DistSquared(TB->GetLocation(), GetOwner()->GetActorLocation()) : DBL_MAX;
			return SearchType == 3 ? DA > DB : DA < DB;
		});
	}
	const FSmartObjectRequestResult& Result = Results[SearchType == 2 ? FMath::RandRange(0, Results.Num() - 1) : 0];
	USmartObjectComponent* Object = Subsystem->GetSmartObjectComponentByRequestResult(Result);
	OutSlot = Result.SlotHandle;
	OutActor = Object ? Object->GetOwner() : nullptr;
	return OutActor && OutSlot.IsValid();
}
bool UVBGASPSmartObjectComponent::TryInteract()
{
	if (bInteracting) return RequestExit();
	if (!PlayerStateTree || !CanInteract()) return false;
	FSmartObjectSlotHandle Slot;
	AActor* Object = nullptr;
	if (!FindPlayerSlot(Slot, Object)) return false;
	ServerTryInteract();
	return true;
}
void UVBGASPSmartObjectComponent::ServerTryInteract_Implementation()
{
	if (bInteracting || !PlayerStateTree || !CanInteract()) return;
	FGameplayTagContainer Tags(FGameplayTag::RequestGameplayTag(TEXT("SmartObject.ObjectType.Player")));
	FVBGASPSmartObjectPayload Payload;
	AActor* Object = nullptr;
	if (!FindPlayerSlot(Payload.Slot, Object)) return;
	Payload.Actor = Object;
	if (!PlayerTreeComponent)
	{
		PlayerTreeComponent = NewObject<UStateTreeComponent>(GetOwner(), TEXT("GASPSmartObjectStateTree"));
		PlayerTreeComponent->SetStartLogicAutomatically(false);
		PlayerTreeComponent->RegisterComponent();
		PlayerTreeComponent->SetStateTree(PlayerStateTree);
	}
	if (!PlayerTreeComponent->IsRunning()) PlayerTreeComponent->StartLogic();
	PlayerTreeComponent->SendStateTreeEvent(FStateTreeEvent(Tags.First(), FConstStructView::Make(Payload), TEXT("VBGASPSmartObject")));
}
bool UVBGASPSmartObjectComponent::FindPlayerSlot(FSmartObjectSlotHandle& OutSlot, AActor*& OutActor) const
{
	// Original AC_SmartObjectAnimation: forward box trace, then nearest free slot
	// in that actor, with the player on the object's positive local-X side.
	const FVector Location = GetOwner()->GetActorLocation();
	const FVector Forward = GetOwner()->GetActorForwardVector();
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(GASPSmartObjectInteraction), false, GetOwner());
	if (!GetWorld()->SweepSingleByObjectType(Hit, Location + Forward * InteractionTraceStartOffset,
		Location + Forward * InteractionDistance, GetOwner()->GetActorQuat(), FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeBox(InteractionTraceHalfSize.GetAbs()), Query)) return false;
	OutActor = Hit.GetActor();
	if (!OutActor || !OutActor->FindComponentByClass<USmartObjectComponent>()
		|| OutActor->GetActorTransform().InverseTransformPosition(Location).X <= 0.f) return false;
	// SmartObjectSubsystem deliberately does not exist on clients. Only the server
	// queries/claims slots; clients can nominate a visible actor, never a handle.
	if (!GetOwner()->HasAuthority()) return true;
	USmartObjectSubsystem* Subsystem = GetWorld()->GetSubsystem<USmartObjectSubsystem>();
	if (!Subsystem) return false;
	FSmartObjectRequestFilter Filter;
	Filter.UserTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("SmartObject.ObjectType.Player")));
	Filter.BehaviorDefinitionClasses.Add(UGameplayInteractionSmartObjectBehaviorDefinition::StaticClass());
	TArray<FSmartObjectRequestResult> Results;
	if (!USmartObjectBlueprintFunctionLibrary::FindSmartObjectsInActor(Filter, OutActor, Results, GetOwner())) return false;
	double NearestDistance = DBL_MAX;
	for (const FSmartObjectRequestResult& Result : Results)
	{
		const auto Transform = Subsystem->GetSlotTransform(Result);
		if (!Transform.IsSet() || Subsystem->GetSlotState(Result.SlotHandle) != ESmartObjectSlotState::Free) continue;
		const double Distance = FVector::DistSquared(Location, Transform->GetLocation());
		if (Distance < NearestDistance) { NearestDistance = Distance; OutSlot = Result.SlotHandle; }
	}
	return OutSlot.IsValid();
}
bool UVBGASPSmartObjectComponent::RequestExit()
{
	if (!bInteracting || Animation.NumLoops >= 0 || bExitRequested) return false;
	bExitRequested = true;
	ServerRequestExit();
	return true;
}
void UVBGASPSmartObjectComponent::ServerRequestExit_Implementation()
{
	// The original gate opens only for the indefinite player idle loop.
	if (bInteracting && Animation.NumLoops < 0) FinishAnimation(true);
}
bool UVBGASPSmartObjectComponent::AdoptClaim(FSmartObjectClaimHandle Handle)
{
	if (!GetOwner()->HasAuthority() || !Handle.IsValid() || (Claim.IsValid() && Claim != Handle)) return false;
	CaptureInputState();
	Claim = Handle;
	bInteracting = true;
	OnRep_Interacting();
	if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))
	{
		FGameplayTagContainer Cancelled;
		Cancelled.AddTag(VBGameplayTags::Ability_Locomotion_Crouch);
		Cancelled.AddTag(VBGameplayTags::Ability_Locomotion_Sprint);
		ASC->CancelAbilities(&Cancelled);
	}
	GetWorld()->GetSubsystem<USmartObjectSubsystem>()->RegisterSlotInvalidationCallback(Claim, FOnSlotInvalidated::CreateUObject(this, &ThisClass::OnSlotInvalidated));
	return true;
}
void UVBGASPSmartObjectComponent::OnSlotInvalidated(const FSmartObjectClaimHandle&, ESmartObjectSlotState) { AbortInteraction(); }
void UVBGASPSmartObjectComponent::ReleaseClaim()
{
	if (GetOwner()->HasAuthority() && Claim.IsValid())
	{
		if (USmartObjectSubsystem* Subsystem = GetWorld()->GetSubsystem<USmartObjectSubsystem>())
		{
			Subsystem->UnregisterSlotInvalidationCallback(Claim);
			Subsystem->MarkSlotAsFree(Claim);
		}
		Claim.Invalidate();
		bInteracting = false;
		OnRep_Interacting();
		Animation = {};
		GetOwner()->ForceNetUpdate();
	}
	RestorePlaybackState();
	if (AVBCharacter* Character = Cast<AVBCharacter>(GetOwner()); Character && bHasInputState)
	{
		bHasInputState = false;
		Character->RestoreGASPSmartObjectInputState(static_cast<EVBGait>(SavedGait), bSavedStrafe, bSavedAim);
	}
}
void UVBGASPSmartObjectComponent::CaptureInputState()
{
	if (const AVBCharacter* Character = Cast<AVBCharacter>(GetOwner()); Character && !bHasInputState)
	{
		SavedGait = static_cast<uint8>(Character->GetGait());
		bSavedStrafe = Character->GetWantsToStrafe();
		bSavedAim = Character->WantsToAim();
		bHasInputState = true;
	}
}
void UVBGASPSmartObjectComponent::AbortInteraction()
{
	if (PlayerTreeComponent && PlayerTreeComponent->IsRunning()) PlayerTreeComponent->StopLogic(TEXT("SmartObject cancelled"));
	FinishAnimation(false);
	ReleaseClaim();
}
uint32 UVBGASPSmartObjectComponent::PlayAnimation(FVBGASPSmartObjectAnimation Payload)
{
	if (!GetOwner()->HasAuthority() || !Payload.MontageToPlay || Payload.PlayRate <= 0.f) return 0;
	Payload.Serial = ++NextSerial;
	Payload.ServerStartTime = ServerTime(GetWorld());
	if (const UVBAbilitySystemComponent* ASC = Cast<UVBAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))) Payload.SupersededGASMontageId = ASC->GetMontagePlayInstanceId();
	Animation = Payload;
	GetWorld()->GetTimerManager().ClearTimer(RestartTimer);
	bExitRequested = false;
	LoopCount = 0;
	GetWorld()->GetTimerManager().ClearTimer(PlaybackTimer);
	if (!PlayLocal()) { FinishAnimation(false); return 0; }
	if (Payload.PlayTime > 0.f)
	{
		const float Duration = FMath::Max(0.01f, Payload.PlayTime + FMath::FRandRange(-Payload.RandomPlaytimeVariance, Payload.RandomPlaytimeVariance));
		GetWorld()->GetTimerManager().SetTimer(PlaybackTimer, FTimerDelegate::CreateWeakLambda(this, [this] { FinishAnimation(true); }), Duration, false);
	}
	GetOwner()->ForceNetUpdate();
	return Animation.Serial;
}
bool UVBGASPSmartObjectComponent::PlayLocal()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UAnimInstance* Anim = AnimFor(this);
	if (!Character || !Anim || !Animation.MontageToPlay) return false;
	UVBAbilitySystemComponent* ASC = Cast<UVBAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()));
	if (ASC && !ASC->AcquireExternalMontageOwnership(this, Animation.SupersededGASMontageId)) return false;
	if (!bHasSavedCorrectionFlag)
	{
		bSavedCorrectionFlag = Character->GetCharacterMovement()->bIgnoreClientMovementErrorChecksAndCorrection;
		bHasSavedCorrectionFlag = true;
	}
	Character->GetCharacterMovement()->bIgnoreClientMovementErrorChecksAndCorrection = true;
	if (AVBCharacter* VBCharacter = Cast<AVBCharacter>(Character)) VBCharacter->ApplyRotationMode();
	if (!MovementLockedController.IsValid() && Character->GetController())
	{
		MovementLockedController = Character->GetController();
		MovementLockedController->SetIgnoreMoveInput(true);
		Character->StopJumping();
	}
	if (UMotionWarpingComponent* Warp = Character->FindComponentByClass<UMotionWarpingComponent>())
	{
		if (Animation.bUseWarpTarget) Warp->AddOrUpdateWarpTargetFromTransform(WarpTargetName, Animation.WarpTargetTransform);
		else Warp->RemoveWarpTarget(WarpTargetName);
	}
	const float Elapsed = GetOwner()->HasAuthority() ? 0.f : FMath::Max(0.0, ServerTime(GetWorld()) - Animation.ServerStartTime);
	const float Position = FMath::Clamp(Animation.StartTime + Elapsed * Animation.PlayRate, 0.f, Animation.MontageToPlay->GetPlayLength() - KINDA_SMALL_NUMBER);
	bChangingAnimation = true;
	const float Duration = Anim->Montage_Play(Animation.MontageToPlay, Animation.PlayRate, EMontagePlayReturnType::MontageLength, Position, false);
	bChangingAnimation = false;
	if (Duration <= 0.f) { RestorePlaybackState(); return false; }
	MontageInstanceID = Anim->GetActiveInstanceForMontage(Animation.MontageToPlay)->GetInstanceID();
	Anim->OnPlayMontageNotifyBegin.AddUniqueDynamic(this, &ThisClass::OnMontageNotify);
	FOnMontageBlendingOutStarted Delegate;
	Delegate.BindUObject(this, &ThisClass::OnMontageBlendingOut);
	Anim->Montage_SetBlendingOutDelegate(Delegate, Animation.MontageToPlay);
	return true;
}
void UVBGASPSmartObjectComponent::OnRep_Animation()
{
	bExitRequested = false;
	if (Animation.MontageToPlay) PlayLocal();
	else RestorePlaybackState();
}
void UVBGASPSmartObjectComponent::StopAnimation(uint32 Serial)
{
	if (!GetOwner()->HasAuthority() || Serial == 0 || Animation.Serial != Serial || FinishedSerial == Serial) return;
	FinishAnimation(false);
	RestorePlaybackState();
	Animation = {};
	GetOwner()->ForceNetUpdate();
}
void UVBGASPSmartObjectComponent::OnRep_Interacting()
{
	RefreshAbilityState();
}
void UVBGASPSmartObjectComponent::RefreshAbilityState()
{
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	FGameplayTagContainer Blocked;
	Blocked.AddTag(VBGameplayTags::Ability_Attack);
	Blocked.AddTag(VBGameplayTags::Ability_Defense);
	Blocked.AddTag(VBGameplayTags::Ability_Medical_Execute);
	Blocked.AddTag(VBGameplayTags::Ability_Interact);
	Blocked.AddTag(VBGameplayTags::Ability_Locomotion_Crouch);
	Blocked.AddTag(VBGameplayTags::Ability_Locomotion_Sprint);
	if (bOwnsAbilityBlock && (BlockingASC.Get() != ASC || !bInteracting))
	{
		if (UAbilitySystemComponent* PreviousASC = BlockingASC.Get())
		{
			PreviousASC->RemoveLooseGameplayTag(VBGameplayTags::State_Interacting);
			PreviousASC->UnBlockAbilitiesWithTags(Blocked);
		}
		bOwnsAbilityBlock = false;
		BlockingASC.Reset();
	}
	if (ASC && bInteracting && !bOwnsAbilityBlock)
	{
		ASC->AddLooseGameplayTag(VBGameplayTags::State_Interacting);
		ASC->BlockAbilitiesWithTags(Blocked);
		BlockingASC = ASC;
		bOwnsAbilityBlock = true;
	}
	if (Animation.MontageToPlay && bInteracting)
	{
		if (MontageInstanceID == INDEX_NONE) PlayLocal();
		else if (UVBAbilitySystemComponent* VBASC = Cast<UVBAbilitySystemComponent>(ASC))
			VBASC->AcquireExternalMontageOwnership(this, Animation.SupersededGASMontageId);
	}
}
void UVBGASPSmartObjectComponent::OnMontageNotify(FName Name, const FBranchingPointNotifyPayload& Payload)
{
	if (!GetOwner()->HasAuthority() || Payload.MontageInstanceID != MontageInstanceID || Name != TEXT("Transition") || FinishedSerial == Animation.Serial) return;
	// GASP's Transition notify starts the next loop before the montage's long network tail.
	if (Animation.NumLoops < 0 || Animation.PlayTime > 0.f) { QueueAnimationRestart(); return; }
	++LoopCount;
	if (LoopCount >= Animation.NumLoops) FinishAnimation(true);
	else QueueAnimationRestart();
}
void UVBGASPSmartObjectComponent::OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (!GetOwner()->HasAuthority() || bChangingAnimation || Montage != Animation.MontageToPlay || FinishedSerial == Animation.Serial) return;
	if (bInterrupted) { FinishAnimation(false); return; }
	if (GetWorld()->GetTimerManager().TimerExists(RestartTimer)) return;
	++LoopCount;
	if (Animation.NumLoops < 0 || Animation.PlayTime > 0.f || LoopCount < Animation.NumLoops) QueueAnimationRestart();
	else FinishAnimation(true);
}
void UVBGASPSmartObjectComponent::QueueAnimationRestart()
{
	const uint32 Serial = Animation.Serial;
	GetWorld()->GetTimerManager().ClearTimer(RestartTimer);
	RestartTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Serial]
	{
		if (Animation.Serial == Serial) RestartAnimation();
	}));
}
void UVBGASPSmartObjectComponent::RestartAnimation()
{
	if (!bInteracting || FinishedSerial == Animation.Serial) return;
	Animation.ServerStartTime = ServerTime(GetWorld());
	if (!PlayLocal()) FinishAnimation(false);
	GetOwner()->ForceNetUpdate();
}
void UVBGASPSmartObjectComponent::FinishAnimation(bool bSucceeded)
{
	FinishedSerial = Animation.Serial;
	bLastAnimationSucceeded = bSucceeded;
	GetWorld()->GetTimerManager().ClearTimer(PlaybackTimer);
	GetWorld()->GetTimerManager().ClearTimer(RestartTimer);
}
bool UVBGASPSmartObjectComponent::IsAnimationFinished(uint32 Serial, bool& bSucceeded) const
{
	bSucceeded = bLastAnimationSucceeded;
	return Serial != 0 && FinishedSerial == Serial;
}
void UVBGASPSmartObjectComponent::RestorePlaybackState()
{
	GetWorld()->GetTimerManager().ClearTimer(PlaybackTimer);
	GetWorld()->GetTimerManager().ClearTimer(RestartTimer);
	if (UAnimInstance* Anim = AnimFor(this))
	{
		Anim->OnPlayMontageNotifyBegin.RemoveDynamic(this, &ThisClass::OnMontageNotify);
		if (UAnimMontage* Montage = Anim->GetCurrentActiveMontage(); Montage && Anim->GetActiveInstanceForMontage(Montage) && Anim->GetActiveInstanceForMontage(Montage)->GetInstanceID() == MontageInstanceID)
		{
			bChangingAnimation = true;
			Anim->Montage_Stop(0.2f, Montage);
			bChangingAnimation = false;
		}
	}
	MontageInstanceID = INDEX_NONE;
	if (MovementLockedController.IsValid()) MovementLockedController->SetIgnoreMoveInput(false);
	MovementLockedController.Reset();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()); Character && bHasSavedCorrectionFlag)
	{
		Character->GetCharacterMovement()->bIgnoreClientMovementErrorChecksAndCorrection = bSavedCorrectionFlag;
		bHasSavedCorrectionFlag = false;
		if (AVBCharacter* VBCharacter = Cast<AVBCharacter>(Character)) VBCharacter->ApplyRotationMode();
	}
	if (UMotionWarpingComponent* Warp = GetOwner()->FindComponentByClass<UMotionWarpingComponent>()) Warp->RemoveWarpTarget(WarpTargetName);
	if (UVBAbilitySystemComponent* ASC = Cast<UVBAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))) ASC->ReleaseExternalMontageOwnership(this);
	if (IgnoredActor)
	{
		if (GetOwner()->HasAuthority()) SetIgnoreActor(IgnoredActor, false);
		else MulticastIgnoreActor_Implementation(IgnoredActor, false);
	}
}
bool UVBGASPSmartObjectComponent::EvaluateAnimation(UProxyTable* Table, const FTransform& Destination, UAnimMontage*& Montage, float& Cost, float& StartTime) const
{
	UAnimInstance* Anim = AnimFor(this);
	if (!Anim || !Table || !SelectionProxy) return false;
	const auto* History = UPoseSearchLibrary::FindPoseHistoryNode(PoseHistoryName, Anim);
	if (!History || !History->GetPoseHistoryPtr()) return false;
	FVBGASPSmartObjectSelectionInputs Inputs;
	Inputs.TargetDistance = FVector::Distance(GetOwner()->GetActorLocation(), Destination.GetLocation());
	const FVector Relative = Destination.InverseTransformPosition(GetOwner()->GetActorLocation()).GetSafeNormal();
	Inputs.TargetAngle = FMath::RadiansToDegrees(FMath::Atan2(Relative.Y, Relative.X));
	Inputs.PoseHistoryNode = History->GetPoseHistoryReference();
	FVBGASPSmartObjectSelectionOutputs Outputs;
	FChooserEvaluationContext Context(Anim);
	Context.AddStructParam(Inputs);
	Context.AddStructParam(Outputs);
	Montage = Cast<UAnimMontage>(Table->FindProxyObject(SelectionProxy->Guid, Context));
	Cost = Outputs.Cost;
	StartTime = Outputs.StartTime;
	return Montage && FMath::IsFinite(Cost) && FMath::IsFinite(StartTime);
}
void UVBGASPSmartObjectComponent::SetIgnoreActor(AActor* OtherActor, bool bIgnore)
{
	if (GetOwner()->HasAuthority()) MulticastIgnoreActor(OtherActor, bIgnore);
}
void UVBGASPSmartObjectComponent::MulticastIgnoreActor_Implementation(AActor* OtherActor, bool bIgnore)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()); Character && OtherActor)
	{
		if (bIgnore)
		{
			if (IgnoredActor == OtherActor) return;
			if (IgnoredActor) MulticastIgnoreActor_Implementation(IgnoredActor, false);
			bCapsuleAlreadyIgnoredActor = Character->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(OtherActor);
			bMeshAlreadyIgnoredActor = Character->GetMesh()->GetMoveIgnoreActors().Contains(OtherActor);
			IgnoredActor = OtherActor;
			Character->GetCapsuleComponent()->IgnoreActorWhenMoving(OtherActor, true);
			Character->GetMesh()->IgnoreActorWhenMoving(OtherActor, true);
		}
		else if (IgnoredActor == OtherActor)
		{
			Character->GetCapsuleComponent()->IgnoreActorWhenMoving(OtherActor, bCapsuleAlreadyIgnoredActor);
			Character->GetMesh()->IgnoreActorWhenMoving(OtherActor, bMeshAlreadyIgnoredActor);
			IgnoredActor = nullptr;
		}
	}
}
void UVBGASPSmartObjectComponent::AddCooldown(FName Name, float Duration) { Cooldowns.Add(Name, GetWorld()->GetTimeSeconds() + Duration); }
bool UVBGASPSmartObjectComponent::HasCooldownExpired(FName Name) const
{
	const double* Until = Cooldowns.Find(Name);
	return !Until || *Until <= GetWorld()->GetTimeSeconds();
}
