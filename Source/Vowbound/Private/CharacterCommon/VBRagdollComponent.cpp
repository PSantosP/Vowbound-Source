// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#include "CharacterCommon/VBRagdollComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBRagdollConfig.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsControlAsset.h"
#include "PhysicsControlComponent.h"
#include "PhysicsControlBPLibrary.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "PhysicsEngine/PhysicsAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogVBRagdoll, Log, All);

static TAutoConsoleVariable<int32> CVarRagdollPoseDiagnostics(
	TEXT("vb.Ragdoll.PoseDiagnostics"), 1,
	TEXT("Record finalized ragdoll/recovery bone spikes and per-episode peaks. 0 disables sampling."), ECVF_Default);

UVBRagdollComponent::UVBRagdollComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UVBRagdollComponent::ValidateSetup(FString& OutError) const
{
	if (!Mesh || !Config || !Config->PhysicsControlAsset || !Config->ExpectedPhysicsAsset)
	{
		OutError = TEXT("Mesh, config, PhysicsControlAsset and ExpectedPhysicsAsset must be assigned");
		return false;
	}
	if (Mesh->GetPhysicsAsset() != Config->ExpectedPhysicsAsset || !Mesh->GetAnimInstance())
	{
		OutError = TEXT("Physics asset does not match the authored config, or AnimInstance is missing");
		return false;
	}
	const UPhysicsControlAsset* Asset = Config->PhysicsControlAsset;
	if (Config->DefaultPhysicsProfile != EVBRagdollPhysicsProfile::Kinematic &&
		Config->DefaultPhysicsProfile != EVBRagdollPhysicsProfile::PhysicalAnimation)
	{
		OutError = TEXT("DefaultPhysicsProfile must be Kinematic or PhysicalAnimation");
		return false;
	}
	if (!Asset->Profiles.Contains(Config->RagdollProfile) || Config->StrengthBindings.IsEmpty())
	{
		OutError = TEXT("Ragdoll profile or animation strength bindings are missing");
		return false;
	}
	if (!Mesh->DoesSocketExist(Config->ImpactTraceSocket) ||
		Config->ImpactPredictionSeconds <= 0.0f || Config->ImpactPredictionFrequency <= 0.0f ||
		Config->AutoRollSlopeDegrees.Y <= Config->AutoRollSlopeDegrees.X)
	{
		OutError = TEXT("Impact trace socket or behavior ranges are invalid");
		return false;
	}
	const auto RequireBody = [this, &OutError](FName Bone)
	{
		if (Bone.IsNone() || Mesh->GetBoneIndex(Bone) == INDEX_NONE ||
			Config->ExpectedPhysicsAsset->FindBodyIndex(Bone) == INDEX_NONE || !Mesh->GetBodyInstance(Bone))
		{
			OutError = FString::Printf(TEXT("Required simulated body '%s' is absent on '%s'"),
				*Bone.ToString(), *GetNameSafe(Mesh->GetSkeletalMeshAsset()));
			return false;
		}
		return true;
	};
	if (!RequireBody(Config->SpineBody)) return false;
	for (const FPhysicsControlLimbSetupData& Limb : Asset->CharacterSetupData.LimbSetupData)
	{
		// Limb roots need a skeleton bone; physics limbs may start at a non-simulated parent.
		if (Limb.StartBone.IsNone() || Mesh->GetBoneIndex(Limb.StartBone) == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("Physics Control limb root '%s' is absent"), *Limb.StartBone.ToString());
			return false;
		}
	}
	for (const auto& Pair : Asset->AdditionalControlsAndModifiers.Controls)
	{
		if (!RequireBody(Pair.Value.Control.ChildBoneName)) return false;
		if (!Pair.Value.Control.ParentBoneName.IsNone() && !RequireBody(Pair.Value.Control.ParentBoneName)) return false;
	}
	for (const auto& Pair : Asset->AdditionalControlsAndModifiers.Modifiers)
	{
		if (!RequireBody(Pair.Value.Modifier.BoneName)) return false;
	}
	return true;
}

bool UVBRagdollComponent::BeginPoweredRagdoll(USkeletalMeshComponent* InMesh, UVBRagdollConfig* InConfig)
{
	EndPoweredRagdoll();
	Mesh = InMesh;
	Config = InConfig;
	FString Error;
	if (!ValidateSetup(Error))
	{
		UE_LOG(LogVBRagdoll, Error, TEXT("%s: powered ragdoll rejected: %s"), *GetNameSafe(GetOwner()), *Error);
		Mesh = nullptr;
		Config = nullptr;
		return false;
	}

	// GASP has a separate, non-simulated root body. Manny's first body is pelvis:
	// letting it also drive the component makes EndPhysics rotate/move the mesh
	// relative to the capsule that already follows pelvis. Physics owns the bones;
	// the capsule owns the component frame throughout powered ragdoll and recovery.
	OriginalPhysicsTransformUpdateMode = Mesh->PhysicsTransformUpdateMode;
	InitialMeshRelativeTransform = Mesh->GetRelativeTransform();
	GeometryLogSamples = 0;
	Mesh->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::ComponentTransformIsKinematic;
	PhysicsControl = NewObject<UPhysicsControlComponent>(GetOwner());
	PhysicsControl->PhysicsControlAsset = Config->PhysicsControlAsset.Get();
	PhysicsControl->RegisterComponent();
	// Same ordering as GASP's AC_PostABPTick: pose/curves -> behaviors -> physics control targets.
	AddTickPrerequisiteComponent(Mesh);
	PhysicsControl->AddTickPrerequisiteComponent(this);
	if (!PhysicsControl->CreateControlsAndBodyModifiersFromPhysicsControlAsset(Mesh, nullptr, NAME_None))
	{
		UE_LOG(LogVBRagdoll, Error, TEXT("%s: Physics Control creation failed"), *GetNameSafe(GetOwner()));
		EndPoweredRagdoll();
		return false;
	}
	// 합쳐진 Arms/Legs 집합은 반대쪽 하나만 생성돼도 비어 있지 않다.
	// 저작된 각 limb를 따로 확인하여 한쪽 물리 바디 누락을 초기화 실패로 드러낸다.
	for (const FPhysicsControlLimbSetupData& Limb : Config->PhysicsControlAsset->CharacterSetupData.LimbSetupData)
	{
		if (!Limb.bCreateParentSpaceControls) continue;
		const FName LimbSet(*FString::Printf(TEXT("ParentSpace_%s"), *Limb.LimbName.ToString()));
		if (PhysicsControl->GetControlNamesInSet(LimbSet).IsEmpty())
		{
			UE_LOG(LogVBRagdoll, Error, TEXT("%s: required Physics Control limb set '%s' is empty (root '%s')"),
				*GetNameSafe(GetOwner()), *LimbSet.ToString(), *Limb.StartBone.ToString());
			EndPoweredRagdoll();
			return false;
		}
	}
	for (const FVBRagdollStrengthBinding& Binding : Config->StrengthBindings)
	{
		if (Binding.Curve.IsNone() || PhysicsControl->GetControlNamesInSet(Binding.ControlSet).IsEmpty())
		{
			UE_LOG(LogVBRagdoll, Error, TEXT("%s: missing curve name or empty Physics Control set '%s'"),
				*GetNameSafe(GetOwner()), *Binding.ControlSet.ToString());
			EndPoweredRagdoll();
			return false;
		}
	}
	if (!SetPhysicsProfile(EVBRagdollPhysicsProfile::Ragdoll))
	{
		EndPoweredRagdoll();
		return false;
	}
	PreviousCenterOfMass = Mesh->GetSkeletalCenterOfMass();
	AnimationState.InjuryState = Config->InjuryState;
	bReportedMissingCurves = false;
	AnimationUpdateCounterAtStart = Mesh->GetAnimInstance()->GetUpdateCounter().Get();
	bHasUpdatedPoseSinceStart = false;
	UpdateStrengths();
	LogRagdollGeometry(TEXT("Enter"));
	DiagnosticPoseSamples = DiagnosticWarnings = 0;
	PeakLocalBoneDelta = PeakBoneStep = PeakBodySpeed = 0.0;
	PeakLocalBone = PeakStepBone = PeakSpeedBody = NAME_None;
	LastDiagnosticFrame = MAX_uint64;
	PreviousDiagnosticBonePositions.Reset();
	PreviousDiagnosticActorLocation = GetOwner()->GetActorLocation();
	PreviousDiagnosticActorRotation = GetOwner()->GetActorQuat();
	// Observe the final skin pose after animation/physics blending, including get-up
	// while this component's behavior tick is disabled. Never modify the pose here.
	FinalPoseDiagnosticHandle = Mesh->RegisterOnBoneTransformsFinalizedDelegate(
		FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &ThisClass::SampleFinalRagdollPose));
	SetComponentTickEnabled(true);
	return true;
}

bool UVBRagdollComponent::GetLegBodies(TArray<FName>& OutLeft, TArray<FName>& OutRight) const
{
	if (!Mesh || !Config) return false;
	if (Mesh->GetBoneIndex(Config->LeftLegRoot) == INDEX_NONE || Mesh->GetBoneIndex(Config->RightLegRoot) == INDEX_NONE)
	{
		UE_LOG(LogVBRagdoll, Error, TEXT("%s: cross-leg collision roots are not present on the skeleton"), *GetNameSafe(GetOwner()));
		return false;
	}
	Mesh->ForEachBodyBelow(Config->LeftLegRoot, true, false, [&OutLeft](FBodyInstance* Body)
	{
		if (Body && Body->BodySetup.IsValid()) OutLeft.Add(Body->BodySetup->BoneName);
	});
	Mesh->ForEachBodyBelow(Config->RightLegRoot, true, false, [&OutRight](FBodyInstance* Body)
	{
		if (Body && Body->BodySetup.IsValid()) OutRight.Add(Body->BodySetup->BoneName);
	});
	return !OutLeft.IsEmpty() && !OutRight.IsEmpty();
}

bool UVBRagdollComponent::SetCrossLegCollision(bool bEnable)
{
	TArray<FName> LeftBodies, RightBodies;
	if (!GetLegBodies(LeftBodies, RightBodies)) return false;
	// UE 5.8 exposes these Physics Control helpers to Blueprint but declares them private
	// in C++. Invoke the reflected UFUNCTION here; no gameplay decision lives in a BP.
	const FName FunctionName = bEnable ? TEXT("EnableCollisionBetweenBodyArrays") : TEXT("DisableCollisionBetweenBodyArrays");
	UPhysicsControlBPLibrary* Library = GetMutableDefault<UPhysicsControlBPLibrary>();
	UFunction* Function = Library->FindFunction(FunctionName);
	if (!Function) return false;
	FStructOnScope Params(Function);
	void* Memory = Params.GetStructMemory();
	FObjectPropertyBase* FirstComponent = FindFProperty<FObjectPropertyBase>(Function, TEXT("FirstComponent"));
	FArrayProperty* FirstBoneNames = FindFProperty<FArrayProperty>(Function, TEXT("FirstBoneNames"));
	FObjectPropertyBase* SecondComponent = FindFProperty<FObjectPropertyBase>(Function, TEXT("SecondComponent"));
	FArrayProperty* SecondBoneNames = FindFProperty<FArrayProperty>(Function, TEXT("SecondBoneNames"));
	FBoolProperty* Result = FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue"));
	if (!FirstComponent || !FirstBoneNames || !SecondComponent || !SecondBoneNames || !Result) return false;
	FirstComponent->SetObjectPropertyValue_InContainer(Memory, Mesh);
	FirstBoneNames->CopyCompleteValue(FirstBoneNames->ContainerPtrToValuePtr<void>(Memory), &LeftBodies);
	SecondComponent->SetObjectPropertyValue_InContainer(Memory, Mesh);
	SecondBoneNames->CopyCompleteValue(SecondBoneNames->ContainerPtrToValuePtr<void>(Memory), &RightBodies);
	Library->ProcessEvent(Function, Memory);
	return Result->GetPropertyValue_InContainer(Memory);
}

bool UVBRagdollComponent::SetPhysicsProfile(EVBRagdollPhysicsProfile NewProfile)
{
	if (!Mesh || !Config || !PhysicsControl || NewProfile == EVBRagdollPhysicsProfile::None) return false;
	if (NewProfile == AnimationState.PhysicsProfile) return true;
	const EVBRagdollPhysicsProfile Previous = AnimationState.PhysicsProfile;
	const FName Name = NewProfile == EVBRagdollPhysicsProfile::Ragdoll
		? Config->RagdollProfile : NewProfile == EVBRagdollPhysicsProfile::Kinematic
		? FName(TEXT("Kinematic")) : Config->PhysicalAnimationProfile;
	if (NewProfile == EVBRagdollPhysicsProfile::PhysicalAnimation)
	{
		TArray<FName> LeftBodies, RightBodies;
		if (!GetLegBodies(LeftBodies, RightBodies)) return false;
	}
	if (!Config->PhysicsControlAsset->Profiles.Contains(Name) || !PhysicsControl->InvokeControlProfile(Name))
	{
		UE_LOG(LogVBRagdoll, Error, TEXT("%s: Physics Control profile '%s' could not be applied"),
			*GetNameSafe(GetOwner()), *Name.ToString());
		return false;
	}
	if (NewProfile == EVBRagdollPhysicsProfile::PhysicalAnimation)
	{
		if (!SetCrossLegCollision(false))
		{
			SetCrossLegCollision(true); // Undo any pairs applied before a partial failure.
			if (Previous == EVBRagdollPhysicsProfile::Ragdoll) PhysicsControl->InvokeControlProfile(Config->RagdollProfile);
			return false;
		}
		Mesh->SetConstraintProfileForAll(Config->PhysicalAnimationConstraintProfile, false);
		// Ragdoll pose curves can have left a zero multiplier on these controls.
		for (const FVBRagdollStrengthBinding& Binding : Config->StrengthBindings)
			PhysicsControl->SetControlMultipliersInSet(Binding.ControlSet, FPhysicsControlMultiplier(), false);
	}
	else
	{
		// Enable is harmless when PhysicalAnimation has never disabled a pair.
		if (Previous == EVBRagdollPhysicsProfile::PhysicalAnimation && !SetCrossLegCollision(true))
		{
			SetCrossLegCollision(false);
			PhysicsControl->InvokeControlProfile(Config->PhysicalAnimationProfile);
			return false;
		}
		Mesh->SetConstraintProfileForAll(Config->ConstraintProfile, true);
		if (NewProfile == EVBRagdollPhysicsProfile::Ragdoll) UpdateStrengths();
	}
	AnimationState.PhysicsProfile = NewProfile;
	AnimationState.bPowered = NewProfile == EVBRagdollPhysicsProfile::Ragdoll;
	return true;
}

bool UVBRagdollComponent::ShouldPlayRollingGetUp() const
{
	return Config && AnimationState.CenterOfMassVelocity.Size2D() > Config->RollingGetUpSpeed;
}

FVector UVBRagdollComponent::GetTargetOrientation() const
{
	if (!Mesh || !Config || !GetOwner()) return FVector::ForwardVector;
	if (AnimationState.ElapsedSeconds < Config->CapsuleRotationFreezeSeconds)
		return GetOwner()->GetActorForwardVector();
	if (ShouldPlayRollingGetUp()) return AnimationState.CenterOfMassVelocity.GetSafeNormal2D();
	// GASP Get_RagdollTransform: upright frame, pelvis -> spine_05, reversed
	// when the spine's right axis faces upward. Pelvis socket yaw is not this frame.
	const FTransform Spine = Mesh->GetSocketTransform(Config->SpineBody, RTS_World);
	FVector Forward = Spine.GetLocation() - Mesh->GetSocketLocation(TEXT("pelvis"));
	if (Spine.GetUnitAxis(EAxis::Y).Z > 0.0) Forward *= -1.0;
	return FRotationMatrix::MakeFromZX(FVector::UpVector, Forward).GetUnitAxis(EAxis::X);
}

bool UVBRagdollComponent::PrepareForGetUp()
{
	if (!Mesh || !PhysicsControl || !Config || !Mesh->GetAnimInstance()) return false;
	LogRagdollGeometry(TEXT("BeforeGetUp"));
	// GASP On_RagdollMode_Exit suppresses target velocity during the snapshot jump.
	FPhysicsControlData Data;
	Data.LinearTargetVelocityMultiplier = 0.0f;
	Data.AngularTargetVelocityMultiplier = 0.0f;
	if (!PhysicsControl->SetControlDatasInSet(TEXT("ParentSpace"), Data)) return false;
	AnimationState.bPowered = false;
	GetUpAnimationUpdateCounter = Mesh->GetAnimInstance()->GetUpdateCounter().Get();
	bPendingGetUpProfile = true;
	SetComponentTickEnabled(true);
	return true;
}

bool UVBRagdollComponent::CompleteGetUpProfile()
{
	if (!Config || !Mesh) return false;
	bPendingGetUpProfile = false;
	if (!SetPhysicsProfile(Config->DefaultPhysicsProfile)) return false;
	if (Config->DefaultPhysicsProfile == EVBRagdollPhysicsProfile::Kinematic)
	{
		Mesh->SetAllBodiesSimulatePhysics(false);
		Mesh->bBlendPhysics = false;
	}
	SetComponentTickEnabled(false);
	UE_LOG(LogVBRagdoll, Log, TEXT("GASPGetUp profile restored: %s profile=%d"),
		*GetNameSafe(GetOwner()), int32(Config->DefaultPhysicsProfile));
	return true;
}

void UVBRagdollComponent::EndPoweredRagdoll()
{
	if (Mesh && FinalPoseDiagnosticHandle.IsValid())
	{
		Mesh->UnregisterOnBoneTransformsFinalizedDelegate(FinalPoseDiagnosticHandle);
		if (DiagnosticPoseSamples > 0)
		{
			UE_LOG(LogVBRagdoll, Log, TEXT("RagdollPoseSummary: %s samples=%d spikes=%d maxLocalDeltaCm=%.2f bone=%s maxBoneStepCm=%.2f bone=%s maxBodySpeedCmS=%.1f body=%s"),
				*GetNameSafe(GetOwner()), DiagnosticPoseSamples, DiagnosticWarnings,
				PeakLocalBoneDelta, *PeakLocalBone.ToString(), PeakBoneStep, *PeakStepBone.ToString(), PeakBodySpeed, *PeakSpeedBody.ToString());
		}
	}
	FinalPoseDiagnosticHandle.Reset();
	PreviousDiagnosticBonePositions.Reset();
	bPendingGetUpProfile = false;
	SetComponentTickEnabled(false);
	if (AnimationState.PhysicsProfile == EVBRagdollPhysicsProfile::PhysicalAnimation)
	{
		SetCrossLegCollision(true);
	}
	if (PhysicsControl)
	{
		PhysicsControl->RemoveTickPrerequisiteComponent(this);
		PhysicsControl->DestroyAllControlsAndBodyModifiers();
		PhysicsControl->DestroyComponent();
		PhysicsControl = nullptr;
	}
	if (Mesh)
	{
		RemoveTickPrerequisiteComponent(Mesh);
		if (OriginalPhysicsTransformUpdateMode.IsSet())
			Mesh->PhysicsTransformUpdateMode = OriginalPhysicsTransformUpdateMode.GetValue();
	}
	OriginalPhysicsTransformUpdateMode.Reset();
	Mesh = nullptr;
	Config = nullptr;
	AnimationState = FVBRagdollAnimationState();
	AutoRollForce = FVector::ZeroVector;
	MoveInput = FVector::ZeroVector;
	bHasFloor = false;
	AnimationUpdateCounterAtStart = INDEX_NONE;
	bHasUpdatedPoseSinceStart = false;
}

void UVBRagdollComponent::SetEnvironment(const FVector& InFloorNormal, bool bInHasFloor, const FVector& InMoveInput)
{
	bHasFloor = bInHasFloor && !InFloorNormal.ContainsNaN() && !InFloorNormal.IsNearlyZero();
	FloorNormal = bHasFloor ? InFloorNormal.GetSafeNormal() : FVector::UpVector;
	MoveInput = InMoveInput.ContainsNaN() ? FVector::ZeroVector : InMoveInput.GetClampedToMaxSize(1.0);
}

void UVBRagdollComponent::UpdateStrengths()
{
	bool bAllCurvesPresent = true;
	for (const FVBRagdollStrengthBinding& Binding : Config->StrengthBindings)
	{
		float Value = 0.0f;
		bAllCurvesPresent &= Mesh->GetCurveValue(Binding.Curve, 0.0f, Value);
		FPhysicsControlMultiplier Multiplier;
		// Missing curves mean zero strength in GASP too. Diagnose the disconnected producer explicitly.
		Multiplier.AngularStrengthMultiplier = FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
		PhysicsControl->SetControlMultipliersInSet(Binding.ControlSet, Multiplier, false);
	}
	AnimationState.bHasAllStrengthCurves = bAllCurvesPresent;
	if (!bAllCurvesPresent && !bReportedMissingCurves && bHasUpdatedPoseSinceStart)
	{
		UE_LOG(LogVBRagdoll, Warning, TEXT("%s: powered ragdoll pose lacks strength curves; verify the AnimGraph ragdoll pose producer"),
			*GetNameSafe(GetOwner()));
		bReportedMissingCurves = true;
	}
}

void UVBRagdollComponent::UpdateBehaviors(float DeltaTime)
{
	AnimationState.SpineVelocity = Mesh->GetPhysicsLinearVelocity(Config->SpineBody);
	AnimationState.Speed = AnimationState.SpineVelocity.Size();
	const FVector CenterOfMass = Mesh->GetSkeletalCenterOfMass();
	AnimationState.CenterOfMassVelocity = (CenterOfMass - PreviousCenterOfMass) / DeltaTime;
	PreviousCenterOfMass = CenterOfMass;

	const FTransform Origin = Mesh->GetSocketTransform(Config->ImpactTraceSocket, RTS_World);
	FPredictProjectilePathParams Params;
	Params.StartLocation = Origin.GetLocation();
	Params.LaunchVelocity = AnimationState.SpineVelocity;
	Params.bTraceWithCollision = true;
	Params.bTraceWithChannel = true;
	Params.TraceChannel = Config->ImpactTraceChannel;
	Params.ProjectileRadius = Config->ImpactTraceRadius;
	Params.MaxSimTime = Config->ImpactPredictionSeconds;
	Params.SimFrequency = Config->ImpactPredictionFrequency;
	Params.ActorsToIgnore.Add(GetOwner());
	FPredictProjectilePathResult Result;
	UGameplayStatics::PredictProjectilePath(this, Params, Result);
	// Original graph uses LastTraceDestination both with and without an impact.
	AnimationState.TimeToImpact = Result.LastTraceDestination.Time;
	const FVector Direction = Origin.InverseTransformVectorNoScale(
		Result.LastTraceDestination.Location - Origin.GetLocation()).GetSafeNormal();
	AnimationState.ImpactDirection = FVector2D(
		FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)),
		FMath::RadiansToDegrees(FMath::Atan2(Direction.Z, FVector2D(Direction.X, Direction.Y).Size())));

	const float FloorAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(FloorNormal, FVector::UpVector), -1.0, 1.0)));
	const FVector TargetAutoRoll = bHasFloor ? FloorNormal * FMath::GetMappedRangeValueClamped(
		Config->AutoRollSlopeDegrees, FVector2D(0.0, 1.0), FloorAngle) : FVector::ZeroVector;
	const float InterpSpeed = TargetAutoRoll.SizeSquared() < AutoRollForce.SizeSquared()
		? Config->AutoRollDecreaseSpeed : Config->AutoRollIncreaseSpeed;
	AutoRollForce = FMath::VInterpConstantTo(AutoRollForce, TargetAutoRoll, DeltaTime, InterpSpeed);
	AnimationState.RollForce = (MoveInput + AutoRollForce).GetClampedToMaxSize(1.0);
	AnimationState.RollAmount = AnimationState.RollForce.Size();
	if (Mesh->IsSimulatingPhysics(Config->SpineBody))
	{
		Mesh->AddTorqueInDegrees(FVector::CrossProduct(FVector::UpVector, AnimationState.RollForce)
			* Config->RollTorqueAcceleration, Config->SpineBody, true);
	}
	const float Gravity = AnimationState.CenterOfMassVelocity.Z <= Config->GravityCutoffVerticalSpeed
		? 0.0f : Config->GravityMultiplier;
	PhysicsControl->SetBodyModifierGravityMultiplier(TEXT("All"), Gravity);
}

void UVBRagdollComponent::SampleFinalRagdollPose()
{
	if (CVarRagdollPoseDiagnostics.GetValueOnGameThread() == 0 || !Mesh || !Mesh->GetSkeletalMeshAsset() ||
		!GetOwner() || !GetWorld() || LastDiagnosticFrame == GFrameCounter) return;
	LastDiagnosticFrame = GFrameCounter;
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	const TArray<FTransform>& Pose = Mesh->GetComponentSpaceTransforms();
	const TArray<FTransform>& Reference = Skeleton.GetRefBonePose();
	if (Pose.Num() != Reference.Num()) return;
	const bool bHasPrevious = PreviousDiagnosticBonePositions.Num() == Pose.Num();
	PreviousDiagnosticBonePositions.SetNumZeroed(Pose.Num());
	const FTransform MeshWorld = Mesh->GetComponentTransform();
	double MaxLocalDelta = 0.0, MaxStep = 0.0, MaxSpeed = 0.0, MaxScaleDelta = 0.0;
	FName LocalBone, StepBone, SpeedBody;
	bool bInvalidPose = false;
	for (int32 Index = 1; Index < Pose.Num(); ++Index)
	{
		const int32 Parent = Skeleton.GetParentIndex(Index);
		const FName Bone = Skeleton.GetBoneName(Index);
		const FString BoneName = Bone.ToString();
		// Root/pelvis translation and IK/virtual targets can intentionally move far.
		// Include the physical limbs AND non-physical twist/corrective skin bones.
		if (Parent <= 0 || BoneName.StartsWith(TEXT("ik_")) || BoneName.StartsWith(TEXT("VB "))) continue;
		if (Pose[Index].ContainsNaN() || Pose[Parent].ContainsNaN()) { bInvalidPose = true; continue; }
		const FTransform Local = Pose[Index].GetRelativeTransform(Pose[Parent]);
		const double LocalDelta = FVector::Distance(Local.GetTranslation(), Reference[Index].GetTranslation());
		if (LocalDelta > MaxLocalDelta) { MaxLocalDelta = LocalDelta; LocalBone = Bone; }
		MaxScaleDelta = FMath::Max(MaxScaleDelta, FVector::Distance(Local.GetScale3D(), Reference[Index].GetScale3D()));
		const FVector WorldPosition = MeshWorld.TransformPosition(Pose[Index].GetTranslation());
		const double Step = bHasPrevious ? FVector::Distance(WorldPosition, PreviousDiagnosticBonePositions[Index]) : 0.0;
		if (Step > MaxStep) { MaxStep = Step; StepBone = Bone; }
		PreviousDiagnosticBonePositions[Index] = WorldPosition;
	}
	for (const FBodyInstance* Body : Mesh->Bodies)
	{
		if (!Body || !Body->BodySetup.IsValid() || !Mesh->IsSimulatingPhysics(Body->BodySetup->BoneName)) continue;
		const double Speed = Body->GetUnrealWorldVelocity().Size();
		if (Speed > MaxSpeed) { MaxSpeed = Speed; SpeedBody = Body->BodySetup->BoneName; }
	}
	if (MaxLocalDelta > PeakLocalBoneDelta) { PeakLocalBoneDelta = MaxLocalDelta; PeakLocalBone = LocalBone; }
	if (MaxStep > PeakBoneStep) { PeakBoneStep = MaxStep; PeakStepBone = StepBone; }
	if (MaxSpeed > PeakBodySpeed) { PeakBodySpeed = MaxSpeed; PeakSpeedBody = SpeedBody; }
	const double ActorStep = FVector::Distance(GetOwner()->GetActorLocation(), PreviousDiagnosticActorLocation);
	const double ActorRotationStep = FMath::RadiansToDegrees(PreviousDiagnosticActorRotation.AngularDistance(GetOwner()->GetActorQuat()));
	PreviousDiagnosticActorLocation = GetOwner()->GetActorLocation();
	PreviousDiagnosticActorRotation = GetOwner()->GetActorQuat();
	++DiagnosticPoseSamples;
	// Diagnostic thresholds only: fast authored motion may also trigger these.
	// Preserve every frame in peak statistics, but bound the number of detail logs.
	const bool bSpike = bInvalidPose || MaxLocalDelta > 20.0 || MaxScaleDelta > 0.1 || MaxStep > 75.0 || MaxSpeed > 2500.0;
	const bool bLogSpike = bSpike && DiagnosticWarnings++ < 8;
	if (bLogSpike || DiagnosticPoseSamples == 1)
	{
		UAnimInstance* Anim = Mesh->GetAnimInstance();
		UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		UE_LOG(LogVBRagdoll, Log, TEXT("RagdollPoseFrame: %s frame=%llu worldTime=%.3f dt=%.4f profile=%d pendingGetUp=%d spike=%d invalid=%d localDeltaCm=%.2f bone=%s boneStepCm=%.2f bone=%s scaleDelta=%.3f bodySpeedCmS=%.1f body=%s actorStepCm=%.2f actorRotationStepDeg=%.2f montage=%s montageTime=%.3f"),
			*GetNameSafe(GetOwner()), static_cast<unsigned long long>(GFrameCounter), GetWorld()->GetTimeSeconds(), GetWorld()->GetDeltaSeconds(),
			int32(AnimationState.PhysicsProfile), bPendingGetUpProfile, bSpike, bInvalidPose,
			MaxLocalDelta, *LocalBone.ToString(), MaxStep, *StepBone.ToString(), MaxScaleDelta,
			MaxSpeed, *SpeedBody.ToString(), ActorStep, ActorRotationStep, *GetNameSafe(Montage), Montage ? Anim->Montage_GetPosition(Montage) : -1.0f);
		LogRagdollGeometry(TEXT("FinalPose"));
	}
}

void UVBRagdollComponent::LogRagdollGeometry(const TCHAR* Phase) const
{
	if (!Mesh) return;
	float MaxJointGap = 0.0f;
	FName WorstJoint;
	for (const FConstraintInstance* Joint : Mesh->Constraints)
	{
		if (!Joint) continue;
		const FLinearConstraint& Limit = Joint->ProfileInstance.LinearLimit;
		if (Limit.XMotion != LCM_Locked || Limit.YMotion != LCM_Locked || Limit.ZMotion != LCM_Locked) continue;
		const FBodyInstance* Child = Mesh->GetBodyInstance(Joint->ConstraintBone1);
		const FBodyInstance* Parent = Mesh->GetBodyInstance(Joint->ConstraintBone2);
		if (!Child || !Parent) continue;
		const FVector ChildAnchor = Child->GetUnrealWorldTransform().TransformPosition(
			Joint->GetRefFrame(EConstraintFrame::Frame1).GetTranslation());
		const FVector ParentAnchor = Parent->GetUnrealWorldTransform().TransformPosition(
			Joint->GetRefFrame(EConstraintFrame::Frame2).GetTranslation());
		const float Gap = FVector::Distance(ChildAnchor, ParentAnchor);
		if (Gap > MaxJointGap) { MaxJointGap = Gap; WorstJoint = Joint->JointName; }
	}
	const FTransform Relative = Mesh->GetRelativeTransform();
	UE_LOG(LogVBRagdoll, Log, TEXT("RagdollGeometry %s: %s frameMode=%d relativeDriftCm=%.2f relativeDriftDeg=%.2f scale=%s maxJointGapCm=%.2f joint=%s"),
		Phase, *GetNameSafe(GetOwner()), int32(Mesh->PhysicsTransformUpdateMode),
		FVector::Distance(Relative.GetLocation(), InitialMeshRelativeTransform.GetLocation()),
		FMath::RadiansToDegrees(Relative.GetRotation().AngularDistance(InitialMeshRelativeTransform.GetRotation())),
		*Mesh->GetComponentScale().ToString(), MaxJointGap, *WorstJoint.ToString());
}

void UVBRagdollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bPendingGetUpProfile && Mesh && Mesh->GetAnimInstance())
	{
		// Mesh is our tick prerequisite, so its worker evaluation has finished.
		if (Mesh->GetAnimInstance()->GetUpdateCounter().Get() != GetUpAnimationUpdateCounter)
		{
			if (!CompleteGetUpProfile())
				UE_LOG(LogVBRagdoll, Error, TEXT("GASPGetUp default physics profile failed: %s"), *GetNameSafe(GetOwner()));
		}
		return;
	}
	if (TickType != LEVELTICK_All || DeltaTime <= UE_SMALL_NUMBER || !AnimationState.bPowered || !Mesh || !PhysicsControl) return;
	// Begin은 montage notify 안에서도 호출된다. 경과 시간이나 component tick만으로는
	// powered 입력을 받은 AnimGraph가 실행됐다고 볼 수 없다. Mesh tick prerequisite가
	// 병렬 pose 완료까지 기다린 뒤, 시작 이후 graph update가 있었는지 확인한다.
	if (const UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
	{
		const FGraphTraversalCounter& Counter = AnimInstance->GetUpdateCounter();
		bHasUpdatedPoseSinceStart |= Counter.HasEverBeenUpdated() && Counter.Get() != AnimationUpdateCounterAtStart;
	}
	AnimationState.ElapsedSeconds += DeltaTime;
	if (GeometryLogSamples < 2 && AnimationState.ElapsedSeconds >= (GeometryLogSamples == 0 ? 0.25 : 1.0))
	{
		LogRagdollGeometry(GeometryLogSamples++ == 0 ? TEXT("0.25s") : TEXT("1s"));
	}
	UpdateBehaviors(DeltaTime);
	UpdateStrengths();
}

void UVBRagdollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndPoweredRagdoll();
	Super::EndPlay(EndPlayReason);
}
