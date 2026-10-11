#include "Animation/VBGASPLocomotionLibrary.h"
#include "Animation/AnimClassInterface.h"
#include "VBGASPStateView.h"
#include "Animation/VBGASPBridgeLibrary.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "Animation/VBGASPLocomotionStateLibrary.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AnimationWarpingLibrary.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"

using VBGASP::FStateView;
DEFINE_LOG_CATEGORY_STATIC(LogVBGASPPlayRate, Log, All);
namespace
{
	double SampleRootSpeed(const UAnimSequence* Sequence, double Time)
	{
		const double Length = Sequence->GetPlayLength();
		if (!FMath::IsFinite(Time) || Length <= UE_SMALL_NUMBER) return 0.0;
		const double Duration = FMath::Min(0.1, Length);
		bool bLooping = false;
		UPoseSearchLibrary::IsAnimationAssetLooping(Sequence, bLooping);
		double Start = FMath::Clamp(Time, 0.0, Length);
		if (bLooping && Start >= Length) Start = 0.0;
		if (!bLooping) Start = FMath::Min(Start, Length - Duration);
		const double End = FMath::Min(Start + Duration, Length);
		const FAnimExtractContext ExtractContext(Start, true);
		double Distance = Sequence->ExtractRootMotionFromRange(Start, End, ExtractContext).GetTranslation().Size2D();
		// A loop's root resets at its boundary. Sample both ranges instead of measuring that reset.
		const double Remaining = Duration - (End - Start);
		if (bLooping && Remaining > UE_SMALL_NUMBER)
			Distance += Sequence->ExtractRootMotionFromRange(0.0, Remaining, FAnimExtractContext(0.0, true)).GetTranslation().Size2D();
		return FMath::IsFinite(Distance) ? Distance / Duration : 0.0;
	}

	double ArmedRootMotionPlayRate(UAnimInstance* Instance, const UAnimSequence* Sequence, double Time)
	{
		const FStateView View(Instance);
		const FStructProperty* Property = CastField<FStructProperty>(View.Find(TEXT("CMCState")));
		const FVBGASPCMCState* State = Property && Property->Struct == FVBGASPCMCState::StaticStruct()
			? Property->ContainerPtrToValuePtr<FVBGASPCMCState>(View.Data) : nullptr;
		if (!State || !State->bSetupWasActive || !State->bUsesWeaponLocomotion) return 1.0;
		const double RootSpeed = SampleRootSpeed(Sequence, Time);
		const double CharacterSpeed = View.Number(TEXT("Speed2D"));
		if (RootSpeed <= 1.0 || !FMath::IsFinite(CharacterSpeed))
		{
			// In-place/no-root data cannot support a measured speed ratio. Do not infer authored speed.
			UE_LOG(LogVBGASPPlayRate, VeryVerbose, TEXT("No measurable root speed on %s at %.3f; CMC play rate remains 1"),
				*GetNameSafe(Sequence), Time);
			return 1.0;
		}
		return FMath::Clamp(CharacterSpeed / RootSpeed, 0.85, 1.15);
	}

	UAnimInstance* MontageOwner(UAnimInstance* Instance)
	{
		// The sample owns both locomotion and DefaultSlot in one instance. Vowbound
		// links locomotion beneath the host's montage slots; read that host's slot
		// state without making the linked graph evaluate the montage a second time.
		USkeletalMeshComponent* Mesh = Instance ? Instance->GetSkelMeshComponent() : nullptr;
		return Mesh && Mesh->GetAnimInstance() ? Mesh->GetAnimInstance() : Instance;
	}
	bool UsesStopSettings(const FStateView& S)
	{
		return S.Bool(TEXT("UseExperimentalStateMachine"))
			? S.Child(TEXT("BlendStackInputs")).HasName(TEXT("Tags"), TEXT("Stop"))
			: S.HasName(TEXT("CurrentDatabaseTags"), TEXT("Stops"));
	}
	double Map(double Value, double A, double B, double C, double D)
	{
		return FMath::GetMappedRangeValueClamped(FVector2D(A, B), FVector2D(C, D), Value);
	}
}

bool UVBGASPLocomotionLibrary::IsMoving(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	// GASP 5.8 CMC has the future-velocity pin disconnected: acceleration is active.
	return !S.Struct<FVector>(TEXT("Velocity")).Equals(FVector::ZeroVector, 0.1)
		&& !S.Struct<FVector>(TEXT("Acceleration")).Equals(FVector::ZeroVector, 0.0);
}
bool UVBGASPLocomotionLibrary::IsStarting(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	return IsMoving(Instance) && S.Struct<FVector>(TEXT("Trj_FutureVelocity")).Size2D() >= S.Struct<FVector>(TEXT("Velocity")).Size2D() + 100.0
		&& !S.HasName(TEXT("CurrentDatabaseTags"), TEXT("Pivots"));
}
double UVBGASPLocomotionLibrary::Get_TrajectoryTurnAngle(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	return (S.Struct<FVector>(TEXT("Acceleration")).Rotation() - S.Struct<FVector>(TEXT("Velocity")).Rotation()).GetNormalized().Yaw;
}
bool UVBGASPLocomotionLibrary::IsPivoting(UAnimInstance* Instance)
{
	if (!IsPivotingRaw(Instance)) return false;
	// 콘텐츠 없는 피벗 대체 래치(FVBGASPCMCState::bPivotFallbackLatched) - 켜져 있으면 피벗으로 다시 들어가지 않는다.
	const FStateView View(Instance);
	const FStructProperty* Property = CastField<FStructProperty>(View.Find(TEXT("CMCState")));
	const FVBGASPCMCState* State = Property && Property->Struct == FVBGASPCMCState::StaticStruct()
		? Property->ContainerPtrToValuePtr<FVBGASPCMCState>(View.Data) : nullptr;
	return !(State && State->bPivotFallbackLatched && State->bSetupWasActive && !State->bSelectionContractFailed && State->bUsesWeaponLocomotion
		&& !UVBGASPLocomotionStateLibrary::HasPivotFallbackOwnerChanged(*State)
		&& State->bPivotFallbackCrouched == View.Is(TEXT("Stance"), TEXT("Crouch")));
}
bool UVBGASPLocomotionLibrary::IsPivotingRaw(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	const double Angle = FMath::Abs(Get_TrajectoryTurnAngle(Instance));
	if (!S.Bool(TEXT("UseExperimentalStateMachine")))
	{
		const double Threshold = S.Is(TEXT("RotationMode"), TEXT("OrientToMovement")) ? 45.0 : S.Is(TEXT("RotationMode"), TEXT("Strafe")) ? 30.0 : 0.0;
		return IsMoving(Instance) && Angle >= Threshold;
	}
	const double Speed = S.Number(TEXT("Speed2D"));
	const bool Crouch = S.Is(TEXT("Stance"), TEXT("Crouch"));
	const bool Walk = S.Is(TEXT("Gait"), TEXT("Walk"));
	const bool Sprint = S.Is(TEXT("Gait"), TEXT("Sprint"));
	const double MinSpeed = Crouch || Walk ? 50.0 : 200.0;
	const double MaxSpeed = Crouch || Walk ? 200.0 : Sprint ? 700.0 : 550.0;
	const double Threshold = Crouch ? 65.0 : Walk ? Map(Speed, 150, 200, 70, 60)
		: Sprint ? Map(Speed, 300, 700, 60, 50) : Map(Speed, 300, 500, 70, 60);
	return IsMoving(Instance) && Angle >= Threshold && Speed >= MinSpeed && Speed <= MaxSpeed;
}
bool UVBGASPLocomotionLibrary::ShouldSpinTransition(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	const double Yaw = (S.Struct<FTransform>(TEXT("CharacterTransform")).Rotator() - S.Struct<FTransform>(TEXT("RootTransform")).Rotator()).GetNormalized().Yaw;
	return FMath::Abs(Yaw) >= 130.0 && S.Number(TEXT("Speed2D")) >= 150.0 && !S.HasName(TEXT("CurrentDatabaseTags"), TEXT("Pivots"));
}
bool UVBGASPLocomotionLibrary::ShouldTurnInPlace(UAnimInstance* Instance)
{
	const FStateView S(Instance), P = S.Child(TEXT("CharacterProperties"));
	const double Yaw = (P.Struct<FRotator>(TEXT("OrientationIntent")) - S.Struct<FTransform>(TEXT("RootTransform")).Rotator()).GetNormalized().Yaw;
	const bool Stopped = S.Is(TEXT("MovementState"), TEXT("Idle")) && S.Is(TEXT("MovementState_LastFrame"), TEXT("Moving"));
	const UVBAnimInstance* Main = Cast<UVBAnimInstance>(MontageOwner(Instance));
	const bool GuardTurn = Main && Main->bUseGuardLocomotionPose;
	return FMath::Abs(Yaw) >= 50.0 && (P.Child(TEXT("InputState")).Bool(TEXT("WantsToAim")) || Stopped || GuardTurn);
}
bool UVBGASPLocomotionLibrary::JustTraversed(UAnimInstance* Instance)
{
	const UAnimInstance* Owner = MontageOwner(Instance);
	return Instance && Owner && !Owner->IsSlotActive(TEXT("DefaultSlot")) && Instance->GetCurveValue(TEXT("MovingTraversal")) > 0.0f && FMath::Abs(Get_TrajectoryTurnAngle(Instance)) <= 50.0;
}
bool UVBGASPLocomotionLibrary::JustLanded_Light(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	return S.Child(TEXT("CharacterProperties")).Bool(TEXT("JustLanded")) && FMath::Abs(Get_LandVelocity(Instance)) < FMath::Abs(S.Number(TEXT("HeavyLandSpeedThreshold")));
}
bool UVBGASPLocomotionLibrary::JustLanded_Heavy(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	return S.Child(TEXT("CharacterProperties")).Bool(TEXT("JustLanded")) && FMath::Abs(Get_LandVelocity(Instance)) >= FMath::Abs(S.Number(TEXT("HeavyLandSpeedThreshold")));
}
double UVBGASPLocomotionLibrary::Get_LandVelocity(UAnimInstance* Instance)
{
	return FStateView(Instance).Child(TEXT("CharacterProperties")).Struct<FVector>(TEXT("LandVelocity")).Z;
}
double UVBGASPLocomotionLibrary::Get_MMBlendTime(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	if (S.Is(TEXT("MovementMode"), TEXT("OnGround")))
		return S.Is(TEXT("MovementMode_LastFrame"), TEXT("OnGround")) ? 0.5 : S.Is(TEXT("MovementMode_LastFrame"), TEXT("InAir")) ? 0.2 : 0.0;
	return S.Is(TEXT("MovementMode"), TEXT("InAir")) ? (S.Struct<FVector>(TEXT("Velocity")).Z > 100.0 ? 0.15 : 0.5) : 0.0;
}
EPoseSearchInterruptMode UVBGASPLocomotionLibrary::Get_MMInterruptMode(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	const bool ModeChanged = S.Number(TEXT("MovementMode")) != S.Number(TEXT("MovementMode_LastFrame"));
	const bool StateChanged = S.Number(TEXT("MovementState")) != S.Number(TEXT("MovementState_LastFrame"));
	const bool StanceChanged = S.Number(TEXT("Stance")) != S.Number(TEXT("Stance_LastFrame"));
	const bool RotationChanged = S.Number(TEXT("RotationMode")) != S.Number(TEXT("RotationMode_LastFrame"));
	const bool GaitChanged = S.Number(TEXT("Gait")) != S.Number(TEXT("Gait_LastFrame")) && S.Is(TEXT("MovementState"), TEXT("Moving"));
	return ModeChanged || (S.Is(TEXT("MovementMode"), TEXT("OnGround")) && (StateChanged || GaitChanged || StanceChanged || RotationChanged))
		? EPoseSearchInterruptMode::InterruptOnDatabaseChange : EPoseSearchInterruptMode::DoNotInterrupt;
}
double UVBGASPLocomotionLibrary::Get_MMNotifyRecencyTimeOut(UAnimInstance* Instance)
{
	return FStateView(Instance).Is(TEXT("Gait"), TEXT("Sprint")) ? 0.16 : 0.2;
}
FVector2D UVBGASPLocomotionLibrary::Get_AOValue(UAnimInstance* Instance)
{
	if (!Instance) return FVector2D::ZeroVector;
	const FStateView S(Instance);
	const FRotator Delta = (S.Child(TEXT("CharacterProperties")).Struct<FRotator>(TEXT("AimingRotation")) - S.Struct<FTransform>(TEXT("RootTransform")).Rotator()).GetNormalized();
	return FVector2D(Delta.Yaw, Delta.Pitch) * (1.0 - Instance->GetCurveValue(TEXT("Disable_AO")));
}
double UVBGASPLocomotionLibrary::Get_AO_Yaw(UAnimInstance* Instance)
{
	return FStateView(Instance).Is(TEXT("RotationMode"), TEXT("Strafe")) ? Get_AOValue(Instance).X : 0.0;
}
bool UVBGASPLocomotionLibrary::Enable_AO(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	UAnimInstance* Owner = MontageOwner(Instance);
	return Instance && S.Is(TEXT("RotationMode"), TEXT("Strafe")) && FMath::Abs(Get_AOValue(Instance).X) <= (S.Is(TEXT("MovementState"), TEXT("Moving")) ? 180.0 : 115.0)
		&& Owner && Owner->Blueprint_GetSlotMontageLocalWeight(TEXT("DefaultSlot")) < 0.5f;
}
FVector UVBGASPLocomotionLibrary::CalculateRelativeAccelerationAmount(UAnimInstance* Instance)
{
	const FStateView S(Instance), P = S.Child(TEXT("CharacterProperties"));
	const double Accel = P.Number(TEXT("CurrentMaxAcceleration")), Decel = P.Number(TEXT("CurrentMaxDeceleration"));
	if (Accel <= 0.0 || Decel <= 0.0) return FVector::ZeroVector;
	const double Limit = FVector::DotProduct(S.Struct<FVector>(TEXT("Acceleration")), S.Struct<FVector>(TEXT("Velocity"))) > 0.0 ? Accel : Decel;
	return S.Struct<FTransform>(TEXT("CharacterTransform")).GetRotation().UnrotateVector(S.Struct<FVector>(TEXT("VelocityAcceleration")).GetClampedToMaxSize(Limit) / Limit);
}
FVector2D UVBGASPLocomotionLibrary::Get_LeanAmount(UAnimInstance* Instance)
{
	return FVector2D(CalculateRelativeAccelerationAmount(Instance).Y * Map(FStateView(Instance).Number(TEXT("Speed2D")), 165, 375, 0.5, 1.0), 0.0);
}
EOffsetRootBoneMode UVBGASPLocomotionLibrary::Get_OffsetRootRotationMode(UAnimInstance* Instance)
{
	const UAnimInstance* Owner = MontageOwner(Instance);
	const UVBAnimInstance* Main = Cast<UVBAnimInstance>(Owner);
	const bool bFullBodySlot = Owner && Owner->IsSlotActive(TEXT("DefaultSlot"))
		&& !(Main && Main->bUseGuardLocomotionPose);
	return UVBGASPBridgeLibrary::ConstrainRootOffsetMode(Instance, bFullBodySlot ? EOffsetRootBoneMode::Release : EOffsetRootBoneMode::Accumulate);
}
EOffsetRootBoneMode UVBGASPLocomotionLibrary::Get_OffsetRootTranslationMode(UAnimInstance* Instance)
{
	const UAnimInstance* Owner = MontageOwner(Instance);
	const bool Interpolate = Owner && !Owner->IsSlotActive(TEXT("DefaultSlot")) && FStateView(Instance).Is(TEXT("MovementMode"), TEXT("OnGround")) && IsMoving(Instance);
	return UVBGASPBridgeLibrary::ConstrainRootOffsetMode(Instance, Interpolate ? EOffsetRootBoneMode::Interpolate : EOffsetRootBoneMode::Release);
}
double UVBGASPLocomotionLibrary::Get_OffsetRootTranslationHalfLife(UAnimInstance* Instance)
{
	return FStateView(Instance).Is(TEXT("MovementState"), TEXT("Moving")) ? 0.3 : 0.1;
}
double UVBGASPLocomotionLibrary::Get_OffsetRootTranslationRadius(UAnimInstance* Instance)
{
	return FStateView(Instance).Number(TEXT("OffsetRootTranslationRadius"));
}
EOrientationWarpingSpace UVBGASPLocomotionLibrary::Get_OrientationWarpingWarpingSpace(UAnimInstance* Instance)
{
	return FStateView(Instance).Bool(TEXT("OffsetRootBoneEnabled")) ? EOrientationWarpingSpace::RootBoneTransform : EOrientationWarpingSpace::ComponentTransform;
}
bool UVBGASPLocomotionLibrary::AllowFootPinning(UAnimInstance* Instance)
{
	return FStateView(Instance).Is(TEXT("MovementMode"), TEXT("OnGround")) && IsMoving(Instance);
}
int32 UVBGASPLocomotionLibrary::ConfigureFootPlacementPelvis(UAnimInstance* Instance, float MaxOffset, float LinearStiffness)
{
	if (!IsInGameThread() || !Instance || Instance->HasAnyFlags(RF_ClassDefaultObject)
		|| !FMath::IsFinite(MaxOffset) || MaxOffset <= 0.f
		|| !FMath::IsFinite(LinearStiffness) || LinearStiffness <= 0.f) return 0;
	const IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(Instance->GetClass());
	if (!AnimClass) return 0;
	int32 Configured = 0;
	for (const FStructProperty* Property : AnimClass->GetAnimNodeProperties())
	{
		if (!Property || Property->Struct != FAnimNode_FootPlacement::StaticStruct()) continue;
		FAnimNode_FootPlacement* Node = Property->ContainerPtrToValuePtr<FAnimNode_FootPlacement>(Instance);
		Node->PelvisSettings.MaxOffset = MaxOffset;
		Node->PelvisSettings.LinearStiffness = LinearStiffness;
		++Configured;
	}
	return Configured;
}

FFootPlacementPlantSettings UVBGASPLocomotionLibrary::Get_FootPlacementPlantSettings(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	return S.NativeStruct<FFootPlacementPlantSettings>(UsesStopSettings(S) ? TEXT("PlantSettings_Stops") : TEXT("PlantSettings_Default"));
}
FFootPlacementInterpolationSettings UVBGASPLocomotionLibrary::Get_FootPlacementInterpolationSettings(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	return S.NativeStruct<FFootPlacementInterpolationSettings>(UsesStopSettings(S) ? TEXT("InterpolationSettings_Stops") : TEXT("InterpolationSettings_Default"));
}
bool UVBGASPLocomotionLibrary::EnableSteering(UAnimInstance* Instance, const FAnimNodeReference& Node)
{
	const FStateView S(Instance);
	return (S.Is(TEXT("MovementState"), TEXT("Moving")) || S.Is(TEXT("MovementMode"), TEXT("InAir"))) && UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimIsActive(Node);
}
double UVBGASPLocomotionLibrary::Get_DynamicPlayRate(UAnimInstance* Instance, const FAnimNodeReference& BlendStackInput)
{
	const auto* Sequence = Cast<UAnimSequence>(UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(BlendStackInput));
	if (!Sequence) return 1.0;
	const float Time = UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(BlendStackInput);
	float Speed = 0.0f, Alpha = 0.0f, MinRate = 0.75f, MaxRate = 1.25f;
	if (!UAnimationWarpingLibrary::GetCurveValueFromAnimation(Sequence, TEXT("MoveData_Speed"), Time, Speed)
		|| !UAnimationWarpingLibrary::GetCurveValueFromAnimation(Sequence, TEXT("Enable_Warping"), Time, Alpha))
		return ArmedRootMotionPlayRate(Instance, Sequence, Time);
	float Sample;
	if (UAnimationWarpingLibrary::GetCurveValueFromAnimation(Sequence, TEXT("MinDynamicPlayRate"), Time, Sample)) MinRate = Sample;
	if (UAnimationWarpingLibrary::GetCurveValueFromAnimation(Sequence, TEXT("MaxDynamicPlayRate"), Time, Sample)) MaxRate = Sample;
	return FMath::Lerp(1.0, FMath::Clamp(FStateView(Instance).Number(TEXT("Speed2D")) / FMath::Clamp(double(Speed), 1.0, 999.0), double(MinRate), double(MaxRate)), double(Alpha));
}
FQuat UVBGASPLocomotionLibrary::Get_DesiredFacing(UAnimInstance* Instance)
{
	const FStateView S(Instance);
	if (S.Bool(TEXT("UseExperimentalStateMachine")))
		return FRotator(0, -90, 0).Quaternion() * S.Struct<FRotator>(TEXT("TargetRotation")).Quaternion();
	FTransformTrajectorySample Sample;
	UPoseSearchTrajectoryLibrary::GetTransformTrajectorySampleAtTime(S.NativeStruct<FTransformTrajectory>(TEXT("Trajectory")), 0.5f, Sample, false);
	return Sample.Facing;
}
