// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#include "Character/VBPlayerRagdollComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/VBGA_PlayerRagdoll.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBRagdollGetUpTypes.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBTargetLockComponent.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Chooser.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBRagdollConfig.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchLibrary.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VBPlayerRagdollRecovery, "Event.Locomotion.Ragdoll.GetUp");
DEFINE_LOG_CATEGORY_STATIC(LogVBPlayerRagdoll, Log, All);

UVBPlayerRagdollComponent::UVBPlayerRagdollComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	// SandboxCharacter_Mover_Ragdoll CDO: DefaultRagdollMontageBlendOut.
	EntryMontageBlendOut.Blend.BlendTime = 0.3f;
	EntryMontageBlendOut.Blend.BlendOption = EAlphaBlendOption::HermiteCubic;
	EntryMontageBlendOut.BlendMode = EMontageBlendMode::Inertialization;
}

FGameplayTag UVBPlayerRagdollComponent::GetRecoveryEventTag()
{
	return TAG_VBPlayerRagdollRecovery;
}

void UVBPlayerRagdollComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<ACharacter>(GetOwner());
	Physics = GetOwner()->FindComponentByClass<UVBRagdollComponent>();
	// Match the gameplay families that cannot own movement/a montage during recovery.
	BlockedAbilities.AddTag(VBGameplayTags::Ability_Attack);
	BlockedAbilities.AddTag(VBGameplayTags::Ability_Defense);
	BlockedAbilities.AddTag(VBGameplayTags::Ability_Medical_Purify);
	BlockedAbilities.AddTag(VBGameplayTags::Ability_Medical_Diagnose);
	BlockedAbilities.AddTag(VBGameplayTags::Ability_Medical_Execute);
	BlockedAbilities.AddTag(VBGameplayTags::Ability_Locomotion_Crouch);
	BlockedAbilities.AddTag(VBGameplayTags::Ability_Locomotion_Sprint);
	BlockedAbilities.AddTag(VBGameplayTags::Ability_Interact);
	// 아이템 사용 동작(.sw/quickslot-input 6.1절)도 몽타주를 쥔다. 넘어지면 표식 전이면 소모 없이 끊긴다.
	BlockedAbilities.AddTag(VBGameplayTags::Ability_Item_Use);
	OnRep_State();
}

UAbilitySystemComponent* UVBPlayerRagdollComponent::GetASC() const
{
	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
}

void UVBPlayerRagdollComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBPlayerRagdollComponent, State);
	DOREPLIFETIME_CONDITION(UVBPlayerRagdollComponent, RollInput, COND_SkipOwner);
}

void UVBPlayerRagdollComponent::RequestRagdoll()
{
	if (!Character || !Character->IsLocallyControlled() || IsRagdollActive()) return;
	if (Character->HasAuthority()) StartAuthority();
	else ServerRequestRagdoll();
}

void UVBPlayerRagdollComponent::ServerRequestRagdoll_Implementation()
{
	StartAuthority();
}

bool UVBPlayerRagdollComponent::StartAuthority()
{
	UAbilitySystemComponent* ASC = GetASC();
	const UVBGASPSmartObjectComponent* SmartObject = GetOwner()->FindComponentByClass<UVBGASPSmartObjectComponent>();
	if (SmartObject && SmartObject->IsInteracting()) return false;
	if (!Character || !Character->HasAuthority() || !ASC || IsRagdollActive() ||
		ASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead) ||
		ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Finishing) ||
		ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_GettingUp) ||
		(Character->GetCharacterMovement()->MovementMode != MOVE_Walking &&
		 Character->GetCharacterMovement()->MovementMode != MOVE_NavWalking &&
		 Character->GetCharacterMovement()->MovementMode != MOVE_Falling))
		return false;
	// Do not strand a player in physics when its recovery ability was not granted.
	if (!ASC->FindAbilitySpecFromClass(UVBGA_PlayerRagdoll::StaticClass()))
	{
		UE_LOG(LogVBPlayerRagdoll, Error, TEXT("%s: missing VBGA_PlayerRagdoll ability"), *Character->GetName());
		return false;
	}
	const FVector EntryVelocity = Character->GetVelocity();
	if (!BeginRagdoll(EntryVelocity)) return false;
	State.EntryVelocity = EntryVelocity;
	State.Phase = EVBPlayerRagdollPhase::Ragdoll;
	Character->ForceNetUpdate();
	return true;
}

bool UVBPlayerRagdollComponent::BeginRagdoll(const FVector& EntryVelocity)
{
	if (IsRagdollActive()) return IsRagdolling();
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Mesh || !Anim || !Movement || !Physics || !Config || !Config->RecoverableGetUpChooser ||
		Mesh->GetPhysicsAsset() != Config->ExpectedPhysicsAsset || !Mesh->GetBodyInstance(TEXT("pelvis")) ||
		!UPoseSearchLibrary::FindPoseHistoryNode(TEXT("PoseHistory"), Anim))
	{
		UE_LOG(LogVBPlayerRagdoll, Error, TEXT("%s: invalid player ragdoll config, mesh, PhysicsControl or PoseHistory"), *GetNameSafe(Character));
		return false;
	}
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	CapsuleProfile = Capsule->GetCollisionProfileName();
	CapsuleCollision = Capsule->GetCollisionEnabled();
	CapsuleObjectType = Capsule->GetCollisionObjectType();
	CapsuleResponses = Capsule->GetCollisionResponseToChannels();
	MeshProfile = Mesh->GetCollisionProfileName();
	MeshCollision = Mesh->GetCollisionEnabled();
	MeshObjectType = Mesh->GetCollisionObjectType();
	MeshResponses = Mesh->GetCollisionResponseToChannels();
	MeshRelativeTransform = Mesh->GetRelativeTransform();
	bMovementTickWasEnabled = Movement->IsComponentTickEnabled();
	bReplicateMovementWasEnabled = Character->IsReplicatingMovement();
	bUseControllerYawWasEnabled = Character->bUseControllerRotationYaw;
	bOrientRotationWasEnabled = Movement->bOrientRotationToMovement;
	bControllerDesiredRotationWasEnabled = Movement->bUseControllerDesiredRotation;
	bRootMotionRotationWasEnabled = Movement->bAllowPhysicsRotationDuringAnimRootMotion;
	SavedMovementMode = Movement->MovementMode;
	SavedCustomMovementMode = Movement->CustomMovementMode;
	bSavedState = true;
	LocalPhase = EVBPlayerRagdollPhase::Ragdoll;
	if (AVBCharacter* Player = Cast<AVBCharacter>(Character))
	{
		if (Player->HasAuthority() || Player->IsLocallyControlled())
		{
			Player->StopSprint();
			Player->SetWantsToAim(false);
			if (UVBTargetLockComponent* Lock = Player->GetTargetLockComponent()) Lock->ClearLockOn();
		}
	}
	SetAbilityBlock(true);
	// Finish the owning ability too, instead of only stopping its montage task.
	if (UAbilitySystemComponent* ASC = GetASC())
	{
		if (Character->HasAuthority())
		{
			if (UGameplayAbility* AnimatingAbility = ASC->GetAnimatingAbility()) ASC->CancelAbility(AnimatingAbility);
		}
	}
	Anim->Montage_StopWithBlendSettings(EntryMontageBlendOut);
	Character->StopJumping();
	Character->bUseControllerRotationYaw = false;
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = false;
	Movement->bAllowPhysicsRotationDuringAnimRootMotion = false;
	Movement->SetMovementMode(MOVE_None);
	Movement->SetComponentTickEnabled(false);
	if (Character->HasAuthority()) Character->SetReplicateMovement(false);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
	Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh->SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), true, true);
	Mesh->bBlendPhysics = true;
	Mesh->WakeAllRigidBodies();
	Mesh->SetAllPhysicsLinearVelocity(EntryVelocity);
	Mesh->AddTickPrerequisiteComponent(this);
	AddTickPrerequisiteActor(Character);
	if (!Physics->BeginPoweredRagdoll(Mesh, Config))
	{
		Restore();
		return false;
	}
	LocalRollInput = RollInput = FVector::ZeroVector;
	bHasFloor = false;
	LastRollInputSendTime = LastServerRollInputTime = -1.0;
	SetComponentTickEnabled(true);
	UE_LOG(LogVBPlayerRagdoll, Log, TEXT("%s: entered powered ragdoll"), *Character->GetName());
	return true;
}

void UVBPlayerRagdollComponent::RefreshAbilityState()
{
	if (BlockedASC && BlockedASC != GetASC()) SetAbilityBlock(false);
	SetAbilityBlock(IsRagdollActive());
}

void UVBPlayerRagdollComponent::SetAbilityBlock(bool bBlock)
{
	if (bBlock && !BlockedASC)
	{
		BlockedASC = GetASC();
		if (!BlockedASC) return;
		BlockedASC->BlockAbilitiesWithTags(BlockedAbilities);
		if (GetOwner()->HasAuthority()) BlockedASC->CancelAbilities(&BlockedAbilities);
	}
	else if (!bBlock && BlockedASC)
	{
		BlockedASC->UnBlockAbilitiesWithTags(BlockedAbilities);
		BlockedASC = nullptr;
	}
}

void UVBPlayerRagdollComponent::SetRollInput(const FVector& WorldInput)
{
	LocalRollInput = WorldInput.ContainsNaN() ? FVector::ZeroVector : FVector(WorldInput.X, WorldInput.Y, 0.0).GetClampedToMaxSize(1.0);
}

void UVBPlayerRagdollComponent::ServerSetRollInput_Implementation(FVector_NetQuantize10 WorldInput)
{
	if (!IsRagdolling() || WorldInput.ContainsNaN()) return;
	RollInput = FVector(WorldInput.X, WorldInput.Y, 0.0).GetClampedToMaxSize(1.0);
	LastServerRollInputTime = GetWorld()->GetTimeSeconds();
}

bool UVBPlayerRagdollComponent::ProbeFloor(FVector& OutLocation, FVector& OutNormal) const
{
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	OutLocation = Mesh->GetSocketLocation(TEXT("pelvis"));
	OutNormal = FVector::UpVector;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBPlayerRagdollFloor), false, Character);
	Params.AddIgnoredComponent(Mesh);
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	if (!GetWorld()->SweepSingleByChannel(Hit, OutLocation, OutLocation - FVector(0, 0, HalfHeight * 2),
		FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(Capsule->GetScaledCapsuleRadius()), Params)) return false;
	OutLocation.Z = Hit.ImpactPoint.Z + HalfHeight;
	OutNormal = Hit.ImpactNormal;
	return Character->GetCharacterMovement()->IsWalkable(Hit);
}

void UVBPlayerRagdollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsRagdolling() || !Character || !Physics || DeltaTime <= 0.0f) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Character->IsLocallyControlled())
	{
		RollInput = LocalRollInput;
		if (!Character->HasAuthority() && Now - LastRollInputSendTime >= 0.05)
		{
			ServerSetRollInput(LocalRollInput);
			LastRollInputSendTime = Now;
		}
	}
	else if (Character->HasAuthority() && Now - LastServerRollInputTime > 0.25)
		RollInput = FVector::ZeroVector;

	FVector TargetLocation, FloorNormal;
	bHasFloor = ProbeFloor(TargetLocation, FloorNormal);
	Physics->SetEnvironment(FloorNormal, bHasFloor, RollInput);
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const FVector BeforeLocation = Character->GetActorLocation();
	const FTransform BeforeRelative = Mesh->GetRelativeTransform();
	FHitResult Hit;
	{
		// Keep SafeMove's penetration teleports from relocating simulated bodies.
		// This is the same guard that fixed the enemy's ragdoll body jumps.
		const FScopedPreventAttachedComponentMove PreventMeshMove(Mesh);
		Movement->SafeMoveUpdatedComponent(TargetLocation - BeforeLocation,
			Physics->GetTargetOrientation().Rotation(), true, Hit);
	}
	Mesh->SetRelativeTransform(BeforeRelative, false, nullptr, ETeleportType::None);
	Movement->Velocity = (Character->GetActorLocation() - BeforeLocation) / DeltaTime;
}

void UVBPlayerRagdollComponent::RequestGetUp()
{
	if (!Character || !Character->IsLocallyControlled() || !IsRagdolling()) return;
	if (Character->HasAuthority()) StartGetUpAuthority();
	else ServerRequestGetUp();
}

void UVBPlayerRagdollComponent::ServerRequestGetUp_Implementation()
{
	StartGetUpAuthority();
}

void UVBPlayerRagdollComponent::StartGetUpAuthority()
{
	UAbilitySystemComponent* ASC = GetASC();
	if (!Character || !Character->HasAuthority() || !IsRagdolling() || !ASC ||
		ASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead)) return;
	FVector Location, Normal;
	bHasFloor = ProbeFloor(Location, Normal);
	if (!bHasFloor) return;
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	const FAnimNode_PoseSearchHistoryCollector_Base* History = Anim
		? UPoseSearchLibrary::FindPoseHistoryNode(TEXT("PoseHistory"), Anim) : nullptr;
	if (!History || !History->GetPoseHistoryPtr()) return;
	// Capture before changing the capsule frame or physics profile. The server's
	// selected montage/start time is sent as the gameplay event payload to its owner.
	Anim->SavePoseSnapshot(TEXT("Ragdoll"));
	UPoseSearchLibrary::OverridePoseHistoryFromOwningMesh(Anim, TEXT("PoseHistory"));
	FVBRagdollGetUpInputs Inputs;
	Inputs.MovementMode = MOVE_Walking;
	Inputs.RollingGetup = Physics->ShouldPlayRollingGetUp();
	Inputs.PoseHistory = History->GetPoseHistoryReference();
	FVBRagdollGetUpOutputs Outputs;
	FChooserEvaluationContext Context(Anim);
	Context.AddStructParam(Inputs);
	Context.AddStructParam(Outputs);
	UAnimMontage* Montage = nullptr;
	UChooserTable::EvaluateChooser(Context, Config->RecoverableGetUpChooser,
		FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&Montage](UObject* Result)
		{
			Montage = Cast<UAnimMontage>(Result);
			return Montage ? FObjectChooserBase::EIteratorStatus::Stop : FObjectChooserBase::EIteratorStatus::Continue;
		}));
	if (!Montage || Montage->GetSkeleton() != Character->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton() ||
		!FMath::IsFinite(Outputs.MontageStartTime) || Outputs.MontageStartTime < 0 || Outputs.MontageStartTime >= Montage->GetPlayLength())
	{
		UE_LOG(LogVBPlayerRagdoll, Error, TEXT("%s: no valid native GASP get-up result; ragdoll remains active"), *Character->GetName());
		return;
	}
	if (!PrepareForGetUp()) return;
	State.Phase = EVBPlayerRagdollPhase::GettingUp;
	Character->ForceNetUpdate();
	FGameplayEventData Event;
	Event.EventTag = GetRecoveryEventTag();
	Event.Target = Character;
	Event.OptionalObject = Montage;
	Event.EventMagnitude = static_cast<float>(Outputs.MontageStartTime);
	if (ASC->HandleGameplayEvent(Event.EventTag, &Event) == 0)
	{
		UE_LOG(LogVBPlayerRagdoll, Error, TEXT("%s: recovery ability activation failed"), *Character->GetName());
		FinishRecovery();
	}
}

bool UVBPlayerRagdollComponent::PrepareForGetUp()
{
	if (LocalPhase == EVBPlayerRagdollPhase::GettingUp) return true;
	if (!IsRagdolling() || !Character || !Physics) return false;
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	if (!Anim) return false;
	Anim->SavePoseSnapshot(TEXT("Ragdoll"));
	UPoseSearchLibrary::OverridePoseHistoryFromOwningMesh(Anim, TEXT("PoseHistory"));
	if (!Physics->PrepareForGetUp()) return false;
	LocalPhase = EVBPlayerRagdollPhase::GettingUp;
	SetComponentTickEnabled(false);
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	Capsule->SetCollisionProfileName(CapsuleProfile);
	Capsule->SetCollisionObjectType(CapsuleObjectType);
	Capsule->SetCollisionResponseToChannels(CapsuleResponses);
	Capsule->SetCollisionEnabled(CapsuleCollision);
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Walking);
	Movement->SetComponentTickEnabled(bMovementTickWasEnabled);
	// Root-motion correction/replication must resume once physics releases the pose.
	if (Character->HasAuthority()) Character->SetReplicateMovement(bReplicateMovementWasEnabled);
	return true;
}

void UVBPlayerRagdollComponent::OnRep_State()
{
	if (!Character || !Physics) return; // BeginPlay replays initial replication.
	if (State.Phase == EVBPlayerRagdollPhase::Ragdoll)
		BeginRagdoll(State.EntryVelocity);
	else if (State.Phase == EVBPlayerRagdollPhase::GettingUp)
		PrepareForGetUp();
	else
		Restore(LocalPhase == EVBPlayerRagdollPhase::GettingUp);
}

void UVBPlayerRagdollComponent::FinishRecovery()
{
	if (!Character || !Character->HasAuthority() || !IsRagdollActive()) return;
	State.Phase = EVBPlayerRagdollPhase::None;
	Restore(true);
	Character->ForceNetUpdate();
}

void UVBPlayerRagdollComponent::CancelRagdoll()
{
	if (Character && Character->HasAuthority())
	{
		State.Phase = EVBPlayerRagdollPhase::None;
		Character->ForceNetUpdate();
	}
	Restore();
	// Release a recovery montage before a save restore/death path takes ownership.
	// Restore first: the GA's end callback must not re-enter physical cleanup.
	if (Character && Character->HasAuthority())
	{
		if (UAbilitySystemComponent* ASC = GetASC())
		{
			if (FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(UVBGA_PlayerRagdoll::StaticClass()))
				if (Spec->IsActive()) ASC->CancelAbilityHandle(Spec->Handle);
		}
	}
}

void UVBPlayerRagdollComponent::Restore(bool bCompletedRecovery)
{
	if (!bSavedState) return;
	const bool bWasGettingUp = LocalPhase == EVBPlayerRagdollPhase::GettingUp;
	UAbilitySystemComponent* ASC = GetASC();
	const bool bKeepPhysicalAnimation = bCompletedRecovery && bWasGettingUp && Physics && Config &&
		(!ASC || !ASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead)) &&
		Config->DefaultPhysicsProfile == EVBRagdollPhysicsProfile::PhysicalAnimation && Physics->CompleteGetUpProfile();
	LocalPhase = EVBPlayerRagdollPhase::None;
	SetComponentTickEnabled(false);
	if (Physics && !bKeepPhysicalAnimation) Physics->EndPoweredRagdoll();
	if (Character)
	{
		USkeletalMeshComponent* Mesh = Character->GetMesh();
		Mesh->RemoveTickPrerequisiteComponent(this);
		RemoveTickPrerequisiteActor(Character);
		if (!bKeepPhysicalAnimation)
		{
			Mesh->SetAllBodiesSimulatePhysics(false);
			Mesh->bBlendPhysics = false;
		}
		Mesh->SetRelativeTransform(MeshRelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		Mesh->SetCollisionProfileName(MeshProfile);
		Mesh->SetCollisionObjectType(MeshObjectType);
		Mesh->SetCollisionResponseToChannels(MeshResponses);
		Mesh->SetCollisionEnabled(MeshCollision);
		UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		Capsule->SetCollisionProfileName(CapsuleProfile);
		Capsule->SetCollisionObjectType(CapsuleObjectType);
		Capsule->SetCollisionResponseToChannels(CapsuleResponses);
		Capsule->SetCollisionEnabled(CapsuleCollision);
		Character->bUseControllerRotationYaw = bUseControllerYawWasEnabled;
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		Movement->bOrientRotationToMovement = bOrientRotationWasEnabled;
		Movement->bUseControllerDesiredRotation = bControllerDesiredRotationWasEnabled;
		Movement->bAllowPhysicsRotationDuringAnimRootMotion = bRootMotionRotationWasEnabled;
		Movement->SetComponentTickEnabled(bMovementTickWasEnabled);
		Movement->SetMovementMode(bWasGettingUp ? (bHasFloor ? MOVE_Walking : MOVE_Falling) : static_cast<EMovementMode>(SavedMovementMode), SavedCustomMovementMode);
		if (Character->HasAuthority()) Character->SetReplicateMovement(bReplicateMovementWasEnabled);
		// Sprint/lock-on can end when ragdoll starts. Recompute from current
		// gameplay intent instead of restoring their obsolete rotation flags.
		if (AVBCharacter* Player = Cast<AVBCharacter>(Character)) Player->ApplyRotationMode();
	}
	LocalRollInput = RollInput = FVector::ZeroVector;
	bSavedState = false;
	SetAbilityBlock(false);
}

void UVBPlayerRagdollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Restore();
	Super::EndPlay(EndPlayReason);
}
