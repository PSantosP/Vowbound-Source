#include "Animation/AnimNode_VBLadderIK.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimMontage.h"
#include "Character/VBCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBClimbAnimSet.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Traversal/VBLadder.h"
#include "Traversal/VBTraversalComponent.h"
#include "TwoBoneIK.h"

DEFINE_LOG_CATEGORY_STATIC(LogVBLadderIK, Log, All);

static TAutoConsoleVariable<int32> CVarLadderIKTelemetry(
	TEXT("vb.LadderIK.Telemetry"), 0,
	TEXT("Ladder hand/foot IK telemetry: 0=off, 1=transitions and 10Hz samples, 2=every evaluation. Positions are grip/ball world cm; solver output precedes node alpha blending."),
	ECVF_Default);

FAnimNode_VBLadderIK::FAnimNode_VBLadderIK()
{
	const FName EndNames[] = { TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r") };
	const FName ContactNames[] = { TEXT("hand_l"), TEXT("hand_r"), TEXT("ball_l"), TEXT("ball_r") };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Ends[Index].BoneName = EndNames[Index];
		Contacts[Index].BoneName = ContactNames[Index];
		Middles[Index] = Roots[Index] = FCompactPoseBoneIndex(INDEX_NONE);
	}
}

void FAnimNode_VBLadderIK::PreUpdate(const UAnimInstance* InAnimInstance)
{
	// UObject는 게임 스레드에서만 읽는다. 병렬 포즈 평가는 이 값 스냅샷만 쓴다.
	bEnabled = false;
	const AVBCharacter* Character = Cast<AVBCharacter>(InAnimInstance->GetOwningActor());
	const UVBTraversalComponent* Traversal = Character ? Character->GetTraversalComponent() : nullptr;
	const AVBLadder* Ladder = Traversal ? Traversal->GetActiveLadderForIK() : nullptr;
	const UVBClimbAnimSet* Config = Traversal ? Traversal->GetClimbAnimSetForIK() : nullptr;
	if (!Ladder || !Config) return;
	LadderId = Ladder->GetUniqueID();
	Geometry.ToWorld = Ladder->GetActorTransform();
	Geometry.Height = Ladder->GetLadderHeight();
	Geometry.Width = Ladder->GetLadderWidth();
	Geometry.Spacing = FMath::Max(Ladder->GetRungSpacing(), 5.0f) / FMath::Max(Ladder->GetRungSubdivision(), 1);
	Geometry.Drop = FMath::Clamp(Ladder->GetRungDropOffset(), 0.0f, Geometry.Spacing);
	Geometry.Depth = FMath::Max(Ladder->GetRungDepth(), 0.0f);
	Geometry.Radius = Ladder->GetPartThickness() * 0.5f;
	MaxCorrection = FMath::Max(Config->LadderIKMaxCorrection, 0.0f);
	FootMaxCorrection = FMath::Max(Config->LadderFootIKMaxCorrection, 0.0f);
	FootPoseOffsetWS = Config->LadderFootPoseDepth > 0.0f
		? Geometry.GetPoseDepthOffset(Config->LadderFootPoseDepth, Config->LadderDepth) : FVector::ZeroVector;
	PoseBlendTime = FMath::Max(Config->LadderEnterBlendTime, 0.0f);
	HandWeight = FMath::Clamp(Config->LadderHandIKWeight, 0.0f, 1.0f);
	HandGripOffsets[0] = Config->LadderLeftHandGripOffset;
	HandGripOffsets[1] = Config->LadderRightHandGripOffset;
	FootWeight = FMath::Clamp(Config->LadderFootIKWeight, 0.0f, 1.0f);
	bEnabled = Geometry.Height > 0.0f && Geometry.Width > 0.0f;
	if (CVarLadderIKTelemetry.GetValueOnGameThread() > 0)
	{
		DiagnosticActor = Character->GetName();
		DiagnosticFrame = GFrameCounter;
		DiagnosticWorldTime = Character->GetWorld()->GetTimeSeconds();
		// 이 노드는 후처리 인스턴스에 있으므로 본체 AnimInstance에서 몽타주를 읽는다.
		UAnimInstance* MainAnim = Character->GetMesh()->GetAnimInstance();
		UAnimMontage* Montage = MainAnim ? MainAnim->GetCurrentActiveMontage() : nullptr;
		DiagnosticMontage = GetNameSafe(Montage);
		DiagnosticMontagePosition = Montage ? MainAnim->Montage_GetPosition(Montage) : 0.0f;
	}
}

void FAnimNode_VBLadderIK::UpdateInternal(const FAnimationUpdateContext& Context)
{
	Super::UpdateInternal(Context);
	Elapsed += Context.GetDeltaTime();
	if (!bEnabled || EvaluatedLadderId != LadderId)
	{
		for (FVBLadderIKContact& State : States) State = FVBLadderIKContact();
		EvaluatedLadderId = LadderId;
		Elapsed = Context.GetDeltaTime();
		DiagnosticElapsed = 0.0f;
		PoseAlpha = 0.0f;
	}
	// 기본 포즈 보정은 접촉 가중치와 독립이다. 진입 때만 몸 배치와 같은 시간으로 섞는다.
	PoseAlpha = bEnabled ? (PoseBlendTime > SMALL_NUMBER
		? FMath::FInterpConstantTo(PoseAlpha, 1.0f, Context.GetDeltaTime(), 1.0f / PoseBlendTime) : 1.0f) : 0.0f;
}

void FAnimNode_VBLadderIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Ends[Index].Initialize(RequiredBones);
		Contacts[Index].Initialize(RequiredBones);
		const FCompactPoseBoneIndex End = Ends[Index].GetCompactPoseIndex(RequiredBones);
		Middles[Index] = End != INDEX_NONE ? RequiredBones.GetParentBoneIndex(End) : FCompactPoseBoneIndex(INDEX_NONE);
		Roots[Index] = Middles[Index] != INDEX_NONE ? RequiredBones.GetParentBoneIndex(Middles[Index]) : FCompactPoseBoneIndex(INDEX_NONE);
		States[Index] = FVBLadderIKContact();
	}
	Elapsed = 0.0f;
	DiagnosticElapsed = 0.0f;
	PoseAlpha = 0.0f;
}

bool FAnimNode_VBLadderIK::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Ends[Index].IsValidToEvaluate(RequiredBones) && Contacts[Index].IsValidToEvaluate(RequiredBones)
			&& Roots[Index] != INDEX_NONE && Middles[Index] != INDEX_NONE) return true;
	}
	return false;
}

void FAnimNode_VBLadderIK::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms)
{
	if (!bEnabled) return;
	const float Dt = Elapsed;
	Elapsed = 0.0f;
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	const FTransform& MeshToWorld = Output.AnimInstanceProxy->GetComponentTransform();
	const int32 Telemetry = CVarLadderIKTelemetry.GetValueOnAnyThread();
	++DiagnosticEvaluation;
	DiagnosticElapsed += Dt;
	const bool bSample = Telemetry > 1 || DiagnosticElapsed >= 0.1f;
	if (bSample) DiagnosticElapsed = 0.0f;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FVBLadderIKContact& State = States[Index];
		const float Strength = Index < 2 ? HandWeight : FootWeight;
		const float LimbMaxCorrection = Index < 2 ? MaxCorrection : FootMaxCorrection;
		if (Strength <= 0.0f || !Ends[Index].IsValidToEvaluate(Bones) || !Contacts[Index].IsValidToEvaluate(Bones)
			|| Roots[Index] == INDEX_NONE || Middles[Index] == INDEX_NONE)
		{
			State = FVBLadderIKContact();
			continue;
		}
		const FCompactPoseBoneIndex EndIndex = Ends[Index].GetCompactPoseIndex(Bones);
		const FCompactPoseBoneIndex ContactIndex = Contacts[Index].GetCompactPoseIndex(Bones);
		const FTransform& ContactTransform = Output.Pose.GetComponentSpaceTransform(ContactIndex);
		const FVector ContactCS = Index < 2
			? ContactTransform.TransformPosition(HandGripOffsets[Index]) : ContactTransform.GetLocation();
		const FVector SourceContactWS = MeshToWorld.TransformPosition(ContactCS);
		const FVector PoseCorrectionWS = Index < 2 ? FVector::ZeroVector : FootPoseOffsetWS * PoseAlpha * Strength;
		const FVector ContactWS = SourceContactWS + PoseCorrectionWS;
		const FVector Raw = Geometry.ToWorld.InverseTransformPosition(ContactWS);
		const bool bWasPlanted = State.bPlanted;
		bool bReset = false;
		// 긴 평가 공백/순간이동 뒤에는 이전 접촉을 당겨 오지 않는다.
		if (Dt > 0.25f || (State.bPrimed && Geometry.ToWorld.TransformVector(Raw - State.PreviousRaw).Size() > 100.0))
		{
			State = FVBLadderIKContact();
			bReset = true;
		}
		const bool bSpeedValid = State.bPrimed && Dt > SMALL_NUMBER;
		const double Speed = bSpeedValid ? Geometry.ToWorld.TransformVector(Raw - State.PreviousRaw).Size() / Dt : -1.0;
		const float Clearance = Index < 2 ? 0.0f : Geometry.Radius + FootSoleOffset;
		const bool bHandContact = Index < 2;
		const float AcquireSpeed = bHandContact ? HandApproachAxisSpeed : PlantSpeed;
		const float DwellTime = bHandContact ? 0.0f : PlantDwellTime;
		const double AxisSpeed = bSpeedValid
			? FMath::Abs(FVector::DotProduct(Geometry.ToWorld.TransformVector(Raw - State.PreviousRaw),
				Geometry.ToWorld.GetUnitAxis(EAxis::Z))) / Dt : -1.0;
		if (bHandContact && PoseAlpha < 1.0f)
		{
			// 몸이 사다리로 들어오는 앞뒤 이동을 손의 착지로 오인하지 않는다.
			State = FVBLadderIKContact();
		}
		else
		{
			State.Update(Geometry, Raw, Dt, Clearance,
				AcquireSpeed, FMath::Max(ReleaseSpeed, AcquireSpeed), ContactBlendTime, DwellTime,
				LimbMaxCorrection, bHandContact, HandReleaseTravel);
		}
		const bool bLog = Telemetry > 0
			&& (bSample || bReset || !bSpeedValid || bWasPlanted != State.bPlanted);
		const auto LogLimb = [&](const FVector& SolverContactWS, const FVector& RequestedCorrectionWS)
		{
			if (!bLog) return;
			FVector Candidate;
			const bool bCandidate = Geometry.FindContact(Raw, Clearance, Candidate);
			const FVector CandidateWS = bCandidate ? Geometry.ToWorld.TransformPosition(Candidate) : ContactWS;
			const bool bAnchor = State.bPlanted || State.Weight > SMALL_NUMBER;
			const FVector AnchorWS = bAnchor ? Geometry.ToWorld.TransformPosition(State.Anchor) : ContactWS;
			const TCHAR* Event = bReset ? TEXT("Reset") : !bSpeedValid ? TEXT("Prime")
				: bWasPlanted != State.bPlanted ? (State.bPlanted ? TEXT("Plant") : TEXT("Release")) : TEXT("Sample");
			UE_LOG(LogVBLadderIK, Log,
				TEXT("%s frame=%llu eval=%llu time=%.3f actor=%s ladder=%u bone=%s event=%s dt=%.4f montage=%s pos=%.3f speed=%.2f quiet=%.3f planted=%d->%d weight=%.3f strength=%.3f nodeAlpha=%.3f candidate=%d distance=%.2f rawWS=(%s) candidateWS=(%s) anchorValid=%d anchorWS=(%s) requested=%.2f solvedMove=%.2f residual=%.2f solverWS=(%s) maxCorrection=%.2f plantSpeed=%.2f dwell=%.3f sourceWS=(%s) poseOffset=(%s) poseAlpha=%.3f lockRequested=%.2f axisSpeed=%.2f"),
				Index < 2 ? TEXT("Hand") : TEXT("Foot"), DiagnosticFrame, DiagnosticEvaluation, DiagnosticWorldTime, *DiagnosticActor, LadderId,
				*Ends[Index].BoneName.ToString(), Event, Dt, *DiagnosticMontage, DiagnosticMontagePosition,
				Speed, State.QuietTime, bWasPlanted, State.bPlanted, State.Weight, Strength, ActualAlpha,
				bCandidate, bCandidate ? FVector::Distance(ContactWS, CandidateWS) : -1.0,
				*ContactWS.ToString(), *CandidateWS.ToString(), bAnchor, *AnchorWS.ToString(),
				RequestedCorrectionWS.Size(), FVector::Distance(SourceContactWS, SolverContactWS),
				bAnchor ? FVector::Distance(SolverContactWS, AnchorWS) : -1.0, *SolverContactWS.ToString(),
				LimbMaxCorrection, AcquireSpeed, DwellTime, *SourceContactWS.ToString(), *PoseCorrectionWS.ToString(),
				PoseAlpha, (RequestedCorrectionWS - PoseCorrectionWS).Size(), AxisSpeed);
		};
		if (State.Weight <= SMALL_NUMBER && PoseCorrectionWS.IsNearlyZero())
		{
			LogLimb(ContactWS, FVector::ZeroVector);
			continue;
		}

		FTransform Root = Output.Pose.GetComponentSpaceTransform(Roots[Index]);
		FTransform Middle = Output.Pose.GetComponentSpaceTransform(Middles[Index]);
		FTransform End = Output.Pose.GetComponentSpaceTransform(EndIndex);
		const FVector LockCorrectionWS = State.Weight > SMALL_NUMBER
			? (Geometry.ToWorld.TransformPosition(State.Anchor) - ContactWS).GetClampedToMaxSize(LimbMaxCorrection)
				* State.Weight * Strength : FVector::ZeroVector;
		const FVector CorrectionWS = PoseCorrectionWS + LockCorrectionWS;
		// 말단 목표 = 접촉 목표 - 현재 손목→그립/발목→발볼 벡터. 말단 회전과 스케일을 유지한다.
		const FVector Target = End.GetLocation() + MeshToWorld.InverseTransformVector(CorrectionWS);
		const FQuat EndRotation = End.GetRotation();
		const FVector OriginalEndLocation = End.GetLocation();
		AnimationCore::SolveTwoBoneIK(Root, Middle, End, Middle.GetLocation(), Target, false, 1.0, 1.0);
		End.SetRotation(EndRotation);
		// 말단 회전/스케일이 그대로이므로 그립/발볼도 말단의 이동량만큼 이동한다.
		// 최종 메시 측정값이 아니라 이 노드의 알파 블렌드 전 solver 결과다.
		LogLimb(SourceContactWS + MeshToWorld.TransformVector(End.GetLocation() - OriginalEndLocation), CorrectionWS);
		OutBoneTransforms.Emplace(Roots[Index], Root);
		OutBoneTransforms.Emplace(Middles[Index], Middle);
		OutBoneTransforms.Emplace(EndIndex, End);
	}
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
