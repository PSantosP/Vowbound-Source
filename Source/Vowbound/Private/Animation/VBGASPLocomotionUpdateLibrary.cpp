#include "Animation/VBGASPLocomotionUpdateLibrary.h"

#include "Animation/VBGASPBridgeLibrary.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "Animation/VBGASPLocomotionStateLibrary.h"
#include "Animation/VBGASPLocomotionLibrary.h"
#include "VBGASPMutableStateView.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSubsystem_Tag.h"
#include "Animation/TrajectoryTypes.h"
#include "AnimationWarpingLibrary.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "KismetAnimationLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchResult.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "UObject/Stack.h"

using VBGASP::FMutableStateView;
using VBGASP::FStateView;

namespace
{
	// FL, FR, BL, BR, in degrees. IDs are the exact original UDS enum pin values.
	FVector4 DirectionThresholds(UAnimInstance* Instance)
	{
		const FMutableStateView S(Instance);
		if (S.IsEnumId(TEXT("MovementDirection"), TEXT("NewEnumerator0")) ||
			S.IsEnumId(TEXT("MovementDirection"), TEXT("NewEnumerator1")) || UVBGASPLocomotionLibrary::IsPivoting(Instance))
			return FVector4(-60.0, 60.0, -120.0, 120.0);
		const bool bLoopNotAim = S.Child(TEXT("BlendStackInputs")).Bool(TEXT("Loop")) &&
			!S.Child(TEXT("CharacterProperties")).Child(TEXT("InputState")).Bool(TEXT("WantsToAim"));
		return FVector4(bLoopNotAim ? -60.0 : -40.0, bLoopNotAim ? 60.0 : 40.0, -140.0, 140.0);
	}

	void WriteThresholds(const FMutableStateView& Target, const FVector4& Value)
	{
		Target.SetNumber(TEXT("FL"), Value.X);
		Target.SetNumber(TEXT("FR"), Value.Y);
		Target.SetNumber(TEXT("BL"), Value.Z);
		Target.SetNumber(TEXT("BR"), Value.W);
	}

	int32 ReadConsoleInt(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetInt() : 0;
	}

	float ReadConsoleFloat(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetFloat() : 0.0f;
	}

	FTransform ReadOffsetRoot(UAnimInstance* Instance)
	{
		FTransform Transform = FTransform::Identity;
		IAnimClassInterface* Class = Instance ? IAnimClassInterface::GetFromClass(Instance->GetClass()) : nullptr;
		const FAnimSubsystem_Tag* Tags = Class ? Class->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
		if (FAnimNode_OffsetRootBone* Node = Tags ? Tags->FindNodeByTag<FAnimNode_OffsetRootBone>(TEXT("OffsetRoot"), Instance) : nullptr)
			Node->GetOffsetRootTransform(Transform);
		return Transform;
	}
}

void UVBGASPLocomotionUpdateLibrary::Update_PropertiesFromCharacter(UAnimInstance* Instance)
{
	if (!Instance || !ensureMsgf(IsInGameThread(), TEXT("GASP character snapshot must run on the game thread"))) return;
	const FMutableStateView S(Instance);
	const auto* Property = CastField<FStructProperty>(S.Find(TEXT("CharacterProperties")));
	ACharacter* Character = Cast<ACharacter>(Instance->GetOwningActor());
	if (!Property || !Character) return;
	// The source's Pawn interface returns this same struct. Gameplay is supplied
	// directly by the native character adapter; no Blueprint/interface execution.
	ensureMsgf(UVBGASPBridgeLibrary::FillCharacterProperties(Character, Property->Struct,
		Property->ContainerPtrToValuePtr<void>(Instance)), TEXT("Invalid GASP character property contract"));
	UVBGASPBridgeLibrary::CaptureCMCSelection(Instance);
}

void UVBGASPLocomotionUpdateLibrary::Update_CVarDrivenVariables(UAnimInstance* Instance)
{
	if (!Instance || !ensureMsgf(IsInGameThread(), TEXT("GASP CVar capture must run on the game thread"))) return;
	const FMutableStateView S(Instance);
	S.SetBool(TEXT("OffsetRootBoneEnabled"), ReadConsoleInt(TEXT("a.animnode.offsetrootbone.enable")) != 0);
	S.SetNumber(TEXT("MMDatabaseLOD"), ReadConsoleInt(TEXT("DDCvar.MMDatabaseLOD")));
	S.SetNumber(TEXT("OffsetRootTranslationRadius"), ReadConsoleFloat(TEXT("DDCvar.OffsetRootBone.TranslationRadius")));
	S.SetBool(TEXT("UseThreadSafeUpdateAnimation"), ReadConsoleInt(TEXT("DDCVar.ThreadSafeAnimationUpdate.Enable")) != 0);
	S.SetBool(TEXT("DebugExperimentalStateMachine"), ReadConsoleInt(TEXT("DDCvar.DrawCharacterDebugShapes")) != 0);
	const USkeletalMeshComponent* Mesh = Instance->GetSkelMeshComponent();
	const bool bExperimental = (ReadConsoleInt(TEXT("DDCVar.ExperimentalStateMachine.Enable")) != 0 ||
		(Mesh && Mesh->ComponentHasTag(TEXT("Force SM Setup")))) && !(Mesh && Mesh->ComponentHasTag(TEXT("Force MM Setup")));
	S.SetBool(TEXT("UseExperimentalStateMachine"), bExperimental);
	// The original sequence's final branch overwrites the previous calculation.
	int32 Setup = FMath::Clamp(ReadConsoleInt(TEXT("DDCVar.LocomotionSetupCMC")), 0, 1);
	const auto* StateProperty = CastField<FStructProperty>(S.Find(TEXT("CMCState")));
	if (StateProperty && StateProperty->Struct == FVBGASPCMCState::StaticStruct())
	{
		FVBGASPCMCState& State = *StateProperty->ContainerPtrToValuePtr<FVBGASPCMCState>(Instance);
		if (State.bSelectionContractFailed) Setup = 0;
		const bool bActive = Setup == 1;
		if (bActive != State.bSetupWasActive)
		{
			State.bNeedsReselect = bActive;
			State.bSlopeInitialized = false;
			State.bHasSelection = false;
			State.bWaitingForPhase = false;
			State.bTransitionToLoopRequested = false;
			// 피벗 대체 래치도 설정 켜짐·꺼짐(재초기화 포함)에 함께 지운다(FVBGASPCMCState::bPivotFallbackLatched).
			State.bPivotFallbackLatched = false;
			State.PendingForceSerial = State.ConsumedForceSerial;
		}
		State.bSetupWasActive = bActive;
	}
	S.SetNumber(TEXT("LocomotionSetup"), Setup);
	S.SetBool(TEXT("UseExperimentalStateMachine"), Setup != 0);
}

void UVBGASPLocomotionUpdateLibrary::GenerateLocomotionTrajectory(const UObject* Context,
	const FPoseSearchTrajectoryData& Data, float DeltaSeconds, bool bSuppressAttackMotion,
	FTransformTrajectory& History, float& LastYaw, FTransformTrajectory& Generated)
{
	if (!bSuppressAttackMotion)
	{
		UPoseSearchTrajectoryLibrary::PoseSearchGenerateTransformTrajectory(Context, Data, DeltaSeconds,
			History, LastYaw, Generated, -1.0f, 30, 0.1f, 15);
		return;
	}
	FPoseSearchTrajectoryData::FState State;
	State.DesiredControllerYawLastUpdate = LastYaw;
	FPoseSearchTrajectoryData::FDerived Derived;
	if (!Data.UpdateData(DeltaSeconds, Context, Derived, State))
	{
		Generated = History;
		return;
	}
	Derived.Velocity = FVector::ZeroVector;
	Derived.Acceleration = FVector::ZeroVector;
	FPoseSearchTrajectoryData::FSampling Sampling;
	Sampling.NumHistorySamples = 30;
	Sampling.SecondsPerHistorySample = -1.0f;
	Sampling.NumPredictionSamples = 15;
	Sampling.SecondsPerPredictionSample = 0.1f;
	// 칼이 몸을 밀었던 이력까지 지워야 정지 복귀를 달리기/정지 클립과 매칭하지 않는다.
	History.Samples.Reset();
	UPoseSearchTrajectoryLibrary::InitTrajectorySamples(History, Derived.Position, Derived.Facing, Sampling, DeltaSeconds);
	UPoseSearchTrajectoryLibrary::UpdateHistory_TransformHistory(History, Derived.Position, Derived.Velocity, Sampling, DeltaSeconds);
	UPoseSearchTrajectoryLibrary::UpdatePrediction_SimulateCharacterMovement(History, Data, Derived, Sampling, DeltaSeconds);
	LastYaw = State.DesiredControllerYawLastUpdate;
	Generated = History;
}

void UVBGASPLocomotionUpdateLibrary::Update_Trajectory(UAnimInstance* Instance)
{
	if (!Instance) return;
	const FMutableStateView S(Instance);
	const FPoseSearchTrajectoryData Data = S.NativeStruct<FPoseSearchTrajectoryData>(
		S.Number(TEXT("Speed2D")) > 0.0 ? TEXT("TrajectoryGenerationData_Moving") : TEXT("TrajectoryGenerationData_Idle"));
	FTransformTrajectory History = S.NativeStruct<FTransformTrajectory>(TEXT("Trajectory"));
	float LastYaw = static_cast<float>(S.Number(TEXT("PreviousDesiredControllerYaw")));
	FTransformTrajectory Generated, Collided;
	FPoseSearchTrajectory_WorldCollisionResults Collision;
	// Original PropertyAccess is owning component -> root AnimInstance. The engine
	// exposes both trajectory operations as BlueprintThreadSafe and performs the
	// same source prediction/collision work; no UObject creation/loading occurs here.
	const USkeletalMeshComponent* Mesh = Instance->GetSkelMeshComponent();
	const UAnimInstance* MainInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!MainInstance) return;
	const UVBAnimInstance* Main = Cast<UVBAnimInstance>(MainInstance);
	GenerateLocomotionTrajectory(MainInstance, Data, Instance->GetDeltaSeconds(),
		Main && Main->IsSuppressingAttackLocomotion(), History, LastYaw, Generated);
	UPoseSearchTrajectoryLibrary::HandleTransformTrajectoryWorldCollisions(Instance, Instance, Generated, true, 0.01f,
		Collided, Collision, ETraceTypeQuery::TraceTypeQuery1, false, TArray<AActor*>(), EDrawDebugTrace::None, true, 150.0f);
	S.SetNumber(TEXT("PreviousDesiredControllerYaw"), LastYaw);
	S.SetNativeStruct(TEXT("TrajectoryCollision"), Collision);
	S.SetNativeStruct(TEXT("Trajectory"), Collided);
	FVector Velocity;
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(Collided, -0.3f, -0.2f, Velocity, false);
	S.SetStruct(TEXT("Trj_PastVelocity"), Velocity);
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(Collided, 0.0f, 0.2f, Velocity, false);
	S.SetStruct(TEXT("Trj_CurrentVelocity"), Velocity);
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(Collided, 0.4f, 0.5f, Velocity, false);
	S.SetStruct(TEXT("Trj_FutureVelocity"), Velocity);
}

void UVBGASPLocomotionUpdateLibrary::Update_EssentialValues(UAnimInstance* Instance)
{
	if (!Instance) return;
	const FMutableStateView S(Instance);
	const FStateView P = S.Child(TEXT("CharacterProperties"));
	S.Copy(TEXT("CharacterTransform_LastFrame"), S, TEXT("CharacterTransform"));
	S.Copy(TEXT("CharacterTransform"), P, TEXT("ActorTransform"));
	// Source Branch condition is an unconnected literal true. OffsetRootBoneEnabled
	// exists beside it but has no wire to that branch.
	const FTransform Offset = ReadOffsetRoot(Instance);
	FRotator RootRotation = Offset.Rotator();
	RootRotation.Yaw += 90.0;
	const FTransform Root(RootRotation, Offset.GetLocation(), FVector::OneVector);
	S.SetStruct(TEXT("RootTransform"), Root);
	S.Copy(TEXT("Acceleration_LastFrame"), S, TEXT("Acceleration"));
	S.Copy(TEXT("Acceleration"), P, TEXT("InputAcceleration"));
	const double MaxAcceleration = P.Number(TEXT("CurrentMaxAcceleration"));
	const double AccelerationAmount = MaxAcceleration == 0.0 ? 0.0 : S.Struct<FVector>(TEXT("Acceleration")).Size() / MaxAcceleration;
	S.SetNumber(TEXT("AccelerationAmount"), AccelerationAmount);
	S.SetBool(TEXT("HasAcceleration"), AccelerationAmount > 0.0);
	S.Copy(TEXT("Velocity_LastFrame"), S, TEXT("Velocity"));
	S.Copy(TEXT("Velocity"), P, TEXT("Velocity"));
	const FVector Velocity = S.Struct<FVector>(TEXT("Velocity"));
	const double Speed = Velocity.Size2D();
	S.SetNumber(TEXT("Speed2D"), Speed);
	S.SetBool(TEXT("HasVelocity"), Speed > 5.0);
	const FVector Acceleration = (Velocity - S.Struct<FVector>(TEXT("Velocity_LastFrame"))) / FMath::Max(Instance->GetDeltaSeconds(), 0.001f);
	S.SetStruct(TEXT("VelocityAcceleration"), Acceleration);
	S.SetStruct(TEXT("RelativeAcceleration"), RootRotation.UnrotateVector(Acceleration));
	if (Speed > 5.0) S.SetStruct(TEXT("LastNonZeroVelocity"), Velocity);
}

void UVBGASPLocomotionUpdateLibrary::Update_States(UAnimInstance* Instance)
{
	if (!Instance) return;
	const FMutableStateView S(Instance);
	const FStateView P = S.Child(TEXT("CharacterProperties"));
	S.Copy(TEXT("MovementMode_LastFrame"), S, TEXT("MovementMode"));
	S.Copy(TEXT("MovementMode"), P, TEXT("MovementMode"));
	S.Copy(TEXT("RotationMode_LastFrame"), S, TEXT("RotationMode"));
	S.Copy(TEXT("RotationMode"), P, TEXT("RotationMode"));
	S.Copy(TEXT("MovementState_LastFrame"), S, TEXT("MovementState"));
	S.SetEnumId(TEXT("MovementState"), UVBGASPLocomotionLibrary::IsMoving(Instance) ? TEXT("NewEnumerator0") : TEXT("NewEnumerator4"));
	S.Copy(TEXT("Gait_LastFrame"), S, TEXT("Gait"));
	S.Copy(TEXT("Gait"), P, TEXT("Gait"));
	S.Copy(TEXT("Stance_LastFrame"), S, TEXT("Stance"));
	S.Copy(TEXT("Stance"), P, TEXT("Stance"));
}

void UVBGASPLocomotionUpdateLibrary::Get_MovementDirectionThresholds(UAnimInstance*, int32&)
{
	checkNoEntry(); // UDS return storage is supplied by the Blueprint VM thunk.
}

DEFINE_FUNCTION(UVBGASPLocomotionUpdateLibrary::execGet_MovementDirectionThresholds)
{
	P_GET_OBJECT(UAnimInstance, Instance);
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const FStructProperty* Property = CastField<FStructProperty>(Stack.MostRecentProperty);
	void* Output = Stack.MostRecentPropertyAddress;
	P_FINISH;
	P_NATIVE_BEGIN;
	if (ensureMsgf(Property && Output, TEXT("GASP threshold return must be the authored threshold struct")))
		WriteThresholds(FMutableStateView(Property->Struct, Output), DirectionThresholds(Instance));
	P_NATIVE_END;
}

void UVBGASPLocomotionUpdateLibrary::Update_MovementDirection(UAnimInstance* Instance)
{
	if (!Instance) return;
	const FMutableStateView S(Instance);
	S.Copy(TEXT("MovementDirectionLastFrame"), S, TEXT("MovementDirection"));
	if (!S.IsEnumId(TEXT("MovementState"), TEXT("NewEnumerator0"))) return;
	const FRotator Intent = S.Child(TEXT("CharacterProperties")).Struct<FRotator>(TEXT("OrientationIntent"));
	const double Direction = UKismetAnimationLibrary::CalculateDirection(S.Struct<FVector>(TEXT("Trj_FutureVelocity")).GetSafeNormal(0.0001), Intent);
	const FVector4 Thresholds = DirectionThresholds(Instance);
	WriteThresholds(FMutableStateView(S.Child(TEXT("MovementDirectionThresholds"))), Thresholds);
	const bool bBias0 = S.IsEnumId(TEXT("MovementDirectionBias"), TEXT("NewEnumerator0"));
	const TCHAR* Value;
	if (S.IsEnumId(TEXT("RotationMode"), TEXT("NewEnumerator0")) || S.IsEnumId(TEXT("Gait"), TEXT("NewEnumerator2"))) Value = TEXT("NewEnumerator0");
	else if (Direction >= Thresholds.X && Direction <= Thresholds.Y) Value = TEXT("NewEnumerator0");
	else if (Direction >= Thresholds.Z && Direction <= Thresholds.X) Value = bBias0 ? TEXT("NewEnumerator3") : TEXT("NewEnumerator2");
	else if (Direction >= Thresholds.Y && Direction <= Thresholds.W) Value = bBias0 ? TEXT("NewEnumerator4") : TEXT("NewEnumerator5");
	else Value = TEXT("NewEnumerator1");
	S.SetEnumId(TEXT("MovementDirection"), Value);
}

double UVBGASPLocomotionUpdateLibrary::Get_StrafeYawRotationOffset(UAnimInstance* Instance, UAnimSequence* StrafeCurve)
{
	if (!Instance || !ensureMsgf(StrafeCurve, TEXT("GASP strafe curve input is missing"))) return 0.0;
	const FMutableStateView S(Instance);
	const FRotator Intent = S.Child(TEXT("CharacterProperties")).Struct<FRotator>(TEXT("OrientationIntent"));
	const double Direction = UKismetAnimationLibrary::CalculateDirection(S.Struct<FVector>(TEXT("Acceleration")).GetSafeNormal(0.0001), Intent);
	const float Time = static_cast<float>(FMath::GetMappedRangeValueClamped(FVector2D(-180.0, 180.0), FVector2D(0.0, 8.0), Direction) / 30.0);
	FName CurveName = NAME_None;
	if (S.IsEnumId(TEXT("MovementDirection"), TEXT("NewEnumerator0"))) CurveName = TEXT("StrafeOffset_F");
	else if (S.IsEnumId(TEXT("MovementDirection"), TEXT("NewEnumerator1"))) CurveName = TEXT("StrafeOffset_B");
	else if (S.IsEnumId(TEXT("MovementDirection"), TEXT("NewEnumerator3"))) CurveName = TEXT("StrafeOffset_LL");
	else if (S.IsEnumId(TEXT("MovementDirection"), TEXT("NewEnumerator2"))) CurveName = TEXT("StrafeOffset_LR");
	else if (S.IsEnumId(TEXT("MovementDirection"), TEXT("NewEnumerator4"))) CurveName = TEXT("StrafeOffset_RL");
	else if (S.IsEnumId(TEXT("MovementDirection"), TEXT("NewEnumerator5"))) CurveName = TEXT("StrafeOffset_RR");
	float Value = 0.0f;
	UAnimationWarpingLibrary::GetCurveValueFromAnimation(StrafeCurve, CurveName, Time, Value);
	return Value;
}

void UVBGASPLocomotionUpdateLibrary::Update_TargetRotation(UAnimInstance* Instance, UAnimSequence* StrafeCurve)
{
	if (!Instance) return;
	const FMutableStateView S(Instance);
	FRotator Target = S.Child(TEXT("CharacterProperties")).Struct<FRotator>(TEXT("OrientationIntent"));
	if (!UVBGASPLocomotionLibrary::IsMoving(Instance) || S.IsEnumId(TEXT("RotationMode"), TEXT("NewEnumerator0")))
		S.SetStruct(TEXT("TargetRotation"), Target);
	else if (S.IsEnumId(TEXT("RotationMode"), TEXT("NewEnumerator1")))
	{
		Target.Yaw += Get_StrafeYawRotationOffset(Instance, StrafeCurve);
		S.SetStruct(TEXT("TargetRotation"), Target);
	}
	S.SetNumber(TEXT("TargetRotationDelta"), (S.Struct<FRotator>(TEXT("TargetRotation")) - S.Struct<FTransform>(TEXT("RootTransform")).Rotator()).GetNormalized().Yaw);
}

void UVBGASPLocomotionUpdateLibrary::Update_Logic(UAnimInstance* Instance, UAnimSequence* StrafeCurve)
{
	if (!Instance) return;
	Update_Trajectory(Instance);
	Update_EssentialValues(Instance);
	Update_States(Instance);
	if (FStateView(Instance).Bool(TEXT("UseExperimentalStateMachine")))
	{
		Update_MovementDirection(Instance);
		Update_TargetRotation(Instance, StrafeCurve);
		const FStateView View(Instance);
		const auto* P = CastField<FStructProperty>(View.Find(TEXT("CMCState")));
		if (P && P->Struct == FVBGASPCMCState::StaticStruct())
		{
			FVBGASPCMCState& State = *P->ContainerPtrToValuePtr<FVBGASPCMCState>(Instance);
			State.SecondsSinceSearch += Instance->GetDeltaSeconds();
			const FVBGASPCMCBlendInputs Inputs = View.NativeStruct<FVBGASPCMCBlendInputs>(TEXT("CMCBlendInputs"));
			// Moving weapon loops need a fresh trajectory query even while their state is unchanged.
			// Non-looping starts/stops keep the authored state/notify transition ownership.
			if (State.bUsesWeaponLocomotion && Inputs.bLoop && View.Number(TEXT("Speed2D")) > 5.0 && State.SecondsSinceSearch >= 0.1f)
			{
				State.bNeedsReselect = true;
				State.SecondsSinceSearch = 0.0f;
			}
		}
		UVBGASPLocomotionStateLibrary::UpdateCMCSelection(Instance);
		Update_CMCSlope(Instance);
		UVBGASPLocomotionStateLibrary::ConsumeCMCForceBlend(Instance);
	}
}

FVector UVBGASPLocomotionUpdateLibrary::CalculateCMCSlopeInput(float PitchDegrees, bool bEnabled, const FVBGSlopeSettings& Settings)
{
	if (!bEnabled || !FMath::IsFinite(PitchDegrees) || !FMath::IsFinite(Settings.MaxSlopeDegrees) ||
		Settings.MaxSlopeDegrees <= UE_SMALL_NUMBER || FMath::Abs(PitchDegrees) < Settings.DeadZoneDegrees)
		return FVector::ZeroVector;
	return FVector(FMath::Clamp(PitchDegrees / Settings.MaxSlopeDegrees, -1.0f, 1.0f), 0, 0);
}

void UVBGASPLocomotionUpdateLibrary::Update_CMCSlope(UAnimInstance* Instance)
{
	if (!Instance) return;
	const FStateView View(Instance);
	const auto* StateProperty = CastField<FStructProperty>(View.Find(TEXT("CMCState")));
	const auto* InputProperty = CastField<FStructProperty>(View.Find(TEXT("CMCBlendInputs")));
	const auto* SettingsProperty = CastField<FStructProperty>(View.Find(TEXT("CMCSlopeSettings")));
	if (!StateProperty || StateProperty->Struct != FVBGASPCMCState::StaticStruct() ||
		!InputProperty || InputProperty->Struct != FVBGASPCMCBlendInputs::StaticStruct() ||
		!SettingsProperty || SettingsProperty->Struct != FVBGSlopeSettings::StaticStruct()) return;
	FVBGASPCMCState& State = *StateProperty->ContainerPtrToValuePtr<FVBGASPCMCState>(Instance);
	FVBGASPCMCBlendInputs& Inputs = *InputProperty->ContainerPtrToValuePtr<FVBGASPCMCBlendInputs>(Instance);
	const FVBGSlopeSettings& Settings = *SettingsProperty->ContainerPtrToValuePtr<FVBGSlopeSettings>(Instance);
	if (!State.bHasWalkableFloor || State.GroundNormal.ContainsNaN() || State.GroundNormal.IsNearlyZero())
	{
		State.bSlopeInitialized = false;
		State.SlopeAngle = FVector2D::ZeroVector;
		Inputs.BlendParameters = FVector::ZeroVector;
		return;
	}
	State.SmoothedGroundNormal = State.bSlopeInitialized
		? FMath::VInterpTo(State.SmoothedGroundNormal, State.GroundNormal, Instance->GetDeltaSeconds(), Settings.NormalInterpSpeed)
		: State.GroundNormal;
	State.bSlopeInitialized = true;
	const FRotator Root = View.Struct<FTransform>(TEXT("RootTransform")).Rotator();
	float Pitch = 0.0f, Roll = 0.0f;
	UKismetMathLibrary::GetSlopeDegreeAngles(UKismetMathLibrary::GetRightVector(Root),
		State.SmoothedGroundNormal, UKismetMathLibrary::GetUpVector(Root), Pitch, Roll);
	State.SlopeAngle = FVector2D(Roll, Pitch);
	Inputs.BlendParameters = CalculateCMCSlopeInput(Pitch, Inputs.Tags.Contains(TEXT("BS_Slope")), Settings);
}

void UVBGASPLocomotionUpdateLibrary::BlueprintThreadSafeUpdateAnimation(UAnimInstance* Instance, float DeltaTime, UAnimSequence* StrafeCurve)
{
	const FStateView S(Instance);
	// Original DeltaTime pin is unused; update helpers read AnimInstance.GetDeltaSeconds.
	if (S.Bool(TEXT("HasOwningActor")) && S.Bool(TEXT("UseThreadSafeUpdateAnimation"))) Update_Logic(Instance, StrafeCurve);
}

void UVBGASPLocomotionUpdateLibrary::Update_MotionMatching(UAnimInstance* Instance, const FAnimUpdateContext& Context, const FAnimNodeReference& Node, UChooserTable* Chooser)
{
	if (!Instance) return;
	if (!ensureMsgf(Chooser, TEXT("GASP locomotion Chooser was not loaded"))) return;
	// Preserve EvaluateChooser2's context and AnimInstance proxy synchronization.
	FChooserEvaluationContext ChooserContext(Instance);
	const FInstancedStruct Selection = UChooserFunctionLibrary::MakeEvaluateChooser(Chooser);
	const TArray<UObject*> Results = UChooserFunctionLibrary::EvaluateObjectChooserBaseMulti(
		ChooserContext, Selection, UPoseSearchDatabase::StaticClass());
	FMutableStateView(Instance).SetObjectArray(TEXT("ValidDatabases"), Results);
	TArray<UPoseSearchDatabase*> Databases;
	for (UObject* Result : Results) if (UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(Result)) Databases.Add(Database);
	EAnimNodeReferenceConversionResult Conversion;
	const FMotionMatchingAnimNodeReference Matching = UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Node, Conversion);
	UMotionMatchingAnimNodeLibrary::SetDatabasesToSearch(Matching, Databases, UVBGASPLocomotionLibrary::Get_MMInterruptMode(Instance));
	// Preserve Vowbound's existing native weapon overlay after the sample selection.
	UVBGASPBridgeLibrary::ApplyWeaponDatabases(Instance, Node);
}

void UVBGASPLocomotionUpdateLibrary::Update_MotionMatching_PostSelection(UAnimInstance* Instance, const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	if (!Instance) return;
	EAnimNodeReferenceConversionResult Conversion;
	const FMotionMatchingAnimNodeReference Matching = UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Node, Conversion);
	FPoseSearchBlueprintResult Result;
	bool bValid;
	UMotionMatchingAnimNodeLibrary::GetMotionMatchingSearchResult(Matching, Result, bValid);
	const FMutableStateView S(Instance);
	S.SetObject(TEXT("CurrentSelectedAnim"), Result.SelectedAnim.Get());
	S.SetObject(TEXT("CurrentSelectedDatabase"), const_cast<UPoseSearchDatabase*>(Result.SelectedDatabase.Get()));
	TArray<FName> Tags;
	UPoseSearchLibrary::GetDatabaseTags(Result.SelectedDatabase.Get(), Tags);
	S.SetNames(TEXT("CurrentDatabaseTags"), Tags);
	// Source OverrideMotionMatchingBlendSettings has no incoming execution wire.
	// Its 0.2s struct is dead graph data, so do not introduce that override here.
}
