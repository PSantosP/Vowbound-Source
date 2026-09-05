// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBTurnInPlaceConfig.h"

#include "Animation/AnimMontage.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimCompositeBase.h"   // FAnimTrack::GetAnimationPose - 몽타주 시간축으로 포즈를 평가한다
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/Skeleton.h"
#include "BoneContainer.h"
#include "BoneIndices.h"
#include "BonePose.h"
#include "Misc/MemStack.h"
#include "AnimNotifyState_MotionWarping.h"
#include "RootMotionModifier.h"
#endif

#define LOCTEXT_NAMESPACE "VBTurnInPlaceConfig"

const FVBTurnInPlaceEntry* UVBTurnInPlaceConfig::ResolveRequest(EVBWeaponType InPoseWeapon, bool bInCrouching,
                                                                float InRequestedYaw) const
{
	// 부호가 0 이면 방향이 없다 - 고를 근거가 없으므로 시작하지 않는다.
	if (FMath::IsNearlyZero(InRequestedYaw))
	{
		return nullptr;
	}

	const float RequestedSign = FMath::Sign(InRequestedYaw);
	const float RequestedMag  = FMath::Abs(InRequestedYaw);

	for (const FVBTurnInPlaceEntry& Entry : Entries)
	{
		if (Entry.PoseWeapon != InPoseWeapon || Entry.bCrouching != bInCrouching || !Entry.Montage)
		{
			continue;
		}
		// 방향이 다른 행은 후보가 아니다. 이 한 줄이 «오른쪽인데 왼쪽으로 도는» 결함을 규격에서 없앤다.
		//   예외는 양방향 행(180 전용) - 어느 쪽으로 돌든 같은 방위라 반대 요청도 받는다.
		if (FMath::Sign(Entry.ReferenceYaw) != RequestedSign && !Entry.bServesBothDirections)
		{
			continue;
		}
		// 담당 구간 밖이면 이 행의 일이 아니다. 겹침이 없으면 답은 최대 하나뿐이고,
		//   겹침 자체는 IsDataValid 가 자산 단계에서 거부한다.
		if (RequestedMag < Entry.RequestMin || RequestedMag >= Entry.RequestMax)
		{
			continue;
		}
		return &Entry;
	}

	return nullptr;
}

bool UVBTurnInPlaceConfig::HasProfile(EVBWeaponType InPoseWeapon, bool bInCrouching) const
{
	for (const FVBTurnInPlaceEntry& Entry : Entries)
	{
		if (Entry.PoseWeapon == InPoseWeapon && Entry.bCrouching == bInCrouching && Entry.Montage)
		{
			return true;
		}
	}
	return false;
}

bool UVBTurnInPlaceConfig::TryGetEnterYaw(EVBWeaponType InPoseWeapon, bool bInCrouching, float InRequestedYaw,
                                          float& OutEnterYaw) const
{
	if (FMath::IsNearlyZero(InRequestedYaw))
	{
		return false;
	}

	const float RequestedSign = FMath::Sign(InRequestedYaw);
	bool bFound = false;
	float Smallest = TNumericLimits<float>::Max();

	for (const FVBTurnInPlaceEntry& Entry : Entries)
	{
		if (Entry.PoseWeapon != InPoseWeapon || Entry.bCrouching != bInCrouching || !Entry.Montage)
		{
			continue;
		}
		if (FMath::Sign(Entry.ReferenceYaw) != RequestedSign && !Entry.bServesBothDirections)
		{
			continue;
		}
		Smallest = FMath::Min(Smallest, Entry.RequestMin);
		bFound   = true;
	}

	if (bFound)
	{
		OutEnterYaw = Smallest;
	}
	return bFound;
}

bool UVBTurnInPlaceConfig::ResolveStepRequest(EVBWeaponType InPoseWeapon, bool bInCrouching, float InRemainingYaw, float& OutStepYaw) const
{
	float EnterYaw = 0.0f;
	if (!TryGetEnterYaw(InPoseWeapon, bInCrouching, InRemainingYaw, EnterYaw))
	{
		return false;
	}
	const float RequestedSign = FMath::Sign(InRemainingYaw);
	float Largest = 0.0f;
	for (const FVBTurnInPlaceEntry& Entry : Entries)
	{
		if (Entry.PoseWeapon != InPoseWeapon || Entry.bCrouching != bInCrouching || !Entry.Montage)
		{
			continue;
		}
		if (FMath::Sign(Entry.ReferenceYaw) != RequestedSign && !Entry.bServesBothDirections)
		{
			continue;
		}
		Largest = FMath::Max(Largest, Entry.RequestMax);
	}

	const float RemainingMag = FMath::Abs(InRemainingYaw);
	if (RemainingMag < Largest)
	{
		OutStepYaw = InRemainingYaw;
		return true;
	}
	// 상한을 넘는다: 첫 조각은 «상한 바로 아래» 와 «나머지가 진입 각 이상이 되게 남기는 값» 중 작은 쪽.
	//   예) 상한 225, 진입 90, 남은 250 -> 첫 조각 160, 나머지 90. 남은 320 -> 첫 조각 224.9, 나머지 95.
	//   RequestMax 는 배타(미만) 라 상한에서 조금 뺀다.
	const float StepMag = FMath::Max(EnterYaw, FMath::Min(Largest - 0.1f, RemainingMag - EnterYaw));
	OutStepYaw = RequestedSign * StepMag;
	return true;
}

#if WITH_EDITOR
namespace VBTurnInPlaceAnalysis
{
	// 몽타주가 실제로 «무엇을 하는지» 를 잰다. 설정에 적힌 값이 아니라 자산에서 읽는다.
	// 왜 이것이 필요한가 (2026-08-30, 값을 치르고 배운 것): 종전 검증은 구간 연속·방향 쌍·워프 배율
	//   도달만 봤는데 셋 다 «내가 적은 값끼리» 의 정합이라, 루트가 골반의 복사본이라 200도까지
	//   넘어갔다 되돌아오는 클립 넷이 초록불로 통과했다. 설정을 설정끼리만 비교하면 장식이다.
	struct FRootMotionProfile
	{
		bool  bMeasured    = false;
		bool  bTimeStretched = false; // TimeStretchCurve 가 걸려 있으면 트리거 시각을 셈할 수 없다 - 측정 불가로 닫는다
		float TotalYaw     = 0.0f;   // 부호 있는 총 회전
		float PeakYaw      = 0.0f;   // 진행 방향으로 가장 멀리 간 지점
		float BacktrackYaw = 0.0f;   // 진행과 반대로 되돌아간 양의 합
		float TimeToTarget = 0.0f;   // 총 회전에 도달한 시각
		float BlendOutStart = 0.0f;
		float LostYaw      = 0.0f;   // 블렌드아웃 시작 시점에 «모자란» 각 = 캡슐이 덜 도는 양
		float ExcessYaw    = 0.0f;   // 그 시점에 «넘친» 각 = 캡슐이 지나쳐 선 채로 끊기는 양
		float NetYawAfterWarp = 0.0f; // 워프 창이 닫힌 뒤 클립 끝까지의 순회전. 창 밖은 워프가 못 보정해 원값으로 캡슐에 실린다
		const UAnimSequenceBase* Sequence = nullptr;
		int32 SegmentCount = 0;
	};

	// InPlayRate = 행의 PlayRate x 몽타주 RateScale. 엔진의 블렌드아웃 트리거는 «실시간으로 남은 재생 시간»
	//   (AnimMontage.cpp::FMontageSubStepper::GetRemainingPlayTimeToSectionEnd = 위치차 / PlayRate) 을 비교하므로,
	//   몽타주 시간축의 블렌드아웃 시각은 트리거 x 재생속도만큼 끝에서 앞이다 (2026-09-02, Codex 교차검토).
	// InWarpEnd = 모션 워프 창이 닫히는 시각(몽타주 시간). 그 뒤의 순회전을 따로 잰다.
	FRootMotionProfile Measure(const UAnimMontage& Montage, float InPlayRate, float InWarpEnd)
	{
		FRootMotionProfile P;
		const float Rate = FMath::Max(FMath::Abs(InPlayRate), UE_KINDA_SMALL_NUMBER);
		P.bTimeStretched = Montage.TimeStretchCurve.IsValid();
		// 블렌드아웃이 «언제» 시작하는지는 BlendOutTriggerTime 이 정한다. 종전에는 길이에서 블렌드
		//   시간을 빼는 한 가지 경우만 셈했는데, 그것은 이 값이 음수일 때의 규칙이다 (2026-09-01 정정).
		// 엔진 근거: AnimMontage.h::UAnimMontage::BlendOutTriggerTime 주석과
		//   AnimMontage.cpp::FAnimMontageInstance::Advance 의 bCustomBlendOutTriggerTime 분기 -
		//   음수면 「블렌드가 몽타주 끝에서 함께 끝나도록」 미리 시작하고,
		//   0 이상이면 「끝에서 그만큼 앞」에 걸어 블렌드가 클립 밖으로 나간다.
		// 왜 중요한가: 이 값을 틀리게 셈하면 검사가 「블렌드아웃 뒤에 회전이 남는다」고 거짓 경보를
		//   내거나, 반대로 진짜 손실을 놓친다. 안전망이 조용한 것보다 거짓말하는 것이 나쁘다.
		// 관성 블렌드아웃(BlendModeOut=Inertialization)이면 Stop 이 가중치 블렌드를 0 으로 바꾸고 관성 요청을
		//   보낸다(AnimMontage.cpp::FAnimMontageInstance::Stop) - 즉 이 시각의 자세가 «관성 전환의 출발 자세» 다.
		const float Length = Montage.GetPlayLength();
		P.BlendOutStart = (Montage.BlendOutTriggerTime >= 0.0f)
			? Length - Montage.BlendOutTriggerTime * Rate
			: Length - Montage.BlendOut.GetBlendTime() * Rate;
		P.BlendOutStart = FMath::Clamp(P.BlendOutStart, 0.0f, Length);
		for (const FSlotAnimationTrack& Slot : Montage.SlotAnimTracks)
		{
			for (const FAnimSegment& Seg : Slot.AnimTrack.AnimSegments)
			{
				++P.SegmentCount;
				if (!P.Sequence) { P.Sequence = Seg.GetAnimReference(); }
			}
		}

		if (Length <= 0.0f) { return P; }

		// 키 간격보다 촘촘히 훑는다. 성긴 표본은 오버슈트를 통째로 건너뛴다.
		const int32 Steps = FMath::Max(60, FMath::CeilToInt(Length * 120.0f));
		TArray<float> StepYaw;
		StepYaw.Reserve(Steps);
		float Accum = 0.0f;
		float PeakSigned = 0.0f;
		for (int32 i = 1; i <= Steps; ++i)
		{
			const float T0 = Length * (i - 1) / Steps;
			const float T1 = Length * i / Steps;
			const FTransform Delta = Montage.ExtractRootMotionFromTrackRange(T0, T1, FAnimExtractContext());
			const float Step = FRotator::NormalizeAxis(Delta.GetRotation().Rotator().Yaw);
			StepYaw.Add(Step);
			Accum += Step;
			if (FMath::Abs(Accum) > FMath::Abs(PeakSigned)) { PeakSigned = Accum; }
		}
		P.bMeasured = true;
		P.TotalYaw  = Accum;
		P.PeakYaw   = PeakSigned;

		// 되돌아간 양과 도달 시각은 총회전을 알아야 판정할 수 있어서 두 번째 훑기에서 낸다.
		float Run = 0.0f;
		float RunAtWarpEnd = 0.0f;
		float LastTimeBeforeWarpEnd = 0.0f;
		P.TimeToTarget = Length;
		bool bReached = false;
		for (int32 i = 0; i < StepYaw.Num(); ++i)
		{
			if (P.TotalYaw != 0.0f && (StepYaw[i] * P.TotalYaw) < 0.0f)
			{
				P.BacktrackYaw += FMath::Abs(StepYaw[i]);
			}
			Run += StepYaw[i];
			const float TimeHere = Length * (i + 1) / StepYaw.Num();
			if (!bReached && FMath::Abs(Run) >= FMath::Abs(P.TotalYaw) - 0.01f)
			{
				bReached = true;
				P.TimeToTarget = TimeHere;
			}
			if (TimeHere <= InWarpEnd) { RunAtWarpEnd = Run; LastTimeBeforeWarpEnd = TimeHere; }
			// 블렌드아웃이 시작되는 시점까지 실제로 실린 양. 그 뒤는 캡슐에 안 간다.
			if (TimeHere <= P.BlendOutStart)
			{
				// 부호 있는 «남은 각» 이다. 오버슈트 구간에서는 음수가 되는데, 그 음수를 그대로
				//   두면 아래 클램프가 0 으로 만들어 「손실 없음」이 된다 - 오버슈트 클립일수록
				//   조용해지는 판정이었다 (2026-09-01 정정, Codex 지적을 실측으로 확인).
				// 지금은 «부족분» 과 «초과분» 을 나눠 담는다. 캡슐이 목표에 못 미치는 것과
				//   지나치는 것은 둘 다 결함이지만 원인이 다르므로 따로 본다.
				P.LostYaw     = FMath::Max(0.0f, FMath::Abs(P.TotalYaw) - FMath::Abs(Run));
				P.ExcessYaw   = FMath::Max(0.0f, FMath::Abs(Run) - FMath::Abs(P.TotalYaw));
			}
		}
		// 표본 간격(약 8ms) 안에 남은 조각까지 더한다. 워프 창이 «도달» 시각에 딱 맞게 닫힌 자산에서 마지막 표본을
		//   빼먹으면 6~7도가 «창 밖 회전» 으로 잘못 잡힌다 (2026-09-02 실측: 카타나 180 이 -6.8도로 거짓 발화).
		if (InWarpEnd > LastTimeBeforeWarpEnd && InWarpEnd <= Length)
		{
			const FTransform Tail = Montage.ExtractRootMotionFromTrackRange(LastTimeBeforeWarpEnd, InWarpEnd, FAnimExtractContext());
			RunAtWarpEnd += FRotator::NormalizeAxis(Tail.GetRotation().Rotator().Yaw);
		}
		P.NetYawAfterWarp = P.TotalYaw - RunAtWarpEnd;
		return P;
	}

	// 발 위치(루트 기준 XY, cm)와 발끝 방향(도) 표본.
	struct FFeetSample
	{
		FVector2D FootL = FVector2D::ZeroVector;
		FVector2D FootR = FVector2D::ZeroVector;
		float YawL = 0.0f;
		float YawR = 0.0f;
		float IkRootLocalDeg = 0.0f; // IK 발 부모 뼈의 로컬 회전 크기. 항등이어야 한다
	};

	// 한 시각의 포즈를 평가해 발을 잰다. 엔진 AnimPose.cpp::UAnimPoseExtensions::GetAnimPoseAtTimeIntervals 와
	//   같은 경로다(AnimationBlueprintLibrary 모듈 의존을 피하려고 직접 쓴다).
	// 컴포넌트 공간은 루트락 상태의 런타임과 같게 둔다(bIgnoreRootLock=false) - 회전 클립은 루트가 180도 돌아
	//   있으므로 루트를 풀고 재면 발이 반대편에 있다고 읽는다. 루트 자체는 인덱스 0 - UE 스켈레톤은 루트가 항상
	//   첫 뼈다. 발·발끝 뼈 이름은 규칙(데이터)이 준다.
	bool SampleFeet(const USkeleton& Skeleton,
	                TFunctionRef<void(FAnimationPoseData&, const FAnimExtractContext&)> Evaluate,
	                float Time, const FVBTurnInPlaceValidationRules& Rules, FFeetSample& Out, FText& OutError)
	{
		// FCompactPose·FBlendedCurve·FCSPose 는 메모리 스택 할당자를 쓴다. 마크 없이 할당하면
		//   «!bShouldEnforceAllocMarks || NumMarks > 0» 단언으로 에디터가 죽는다 - 2026-09-02 에 저장 직후
		//   자동 검증에서 실제로 죽였다. 엔진 AnimPose.cpp 도 같은 자리에 마크를 세운다.
		FMemMark Mark(FMemStack::Get());

		const int32 NumBones = Skeleton.GetReferenceSkeleton().GetNum();
		TArray<FBoneIndexType> Required;
		Required.Reserve(NumBones);
		for (int32 Index = 0; Index < NumBones; ++Index) { Required.Add(static_cast<FBoneIndexType>(Index)); }

		FBoneContainer Bones;
		Bones.InitializeTo(Required, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), Skeleton);

		FCompactPose Pose;
		Pose.SetBoneContainer(&Bones);
		Pose.ResetToRefPose();
		FBlendedCurve Curve;
		Curve.InitFrom(Bones);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(Pose, Curve, Attributes);

		FAnimExtractContext Context(static_cast<double>(Time), false);
		Context.bIgnoreRootLock = false;
		Evaluate(PoseData, Context);

		FCSPose<FCompactPose> ComponentPose;
		ComponentPose.InitPose(Pose);
		const FTransform RootCS = ComponentPose.GetComponentSpaceTransform(FCompactPoseBoneIndex(0));

		auto RelativeToRoot = [&](const FName& BoneName, FVector& OutLocation) -> bool
		{
			const int32 PoseIndex = Bones.GetPoseBoneIndexForBoneName(BoneName);
			if (PoseIndex == INDEX_NONE)
			{
				OutError = FText::Format(LOCTEXT("LandingBoneMissing", "뼈 '{0}' 이 스켈레톤에 없다. 검증 규칙의 뼈 이름을 확인하라."),
				                         FText::FromName(BoneName));
				return false;
			}
			const FCompactPoseBoneIndex Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PoseIndex));
			OutLocation = ComponentPose.GetComponentSpaceTransform(Compact).GetRelativeTransform(RootCS).GetLocation();
			return true;
		};
		auto ToeYaw = [&](const FVector& Foot, const FVector& Ball, float& OutYaw) -> bool
		{
			const FVector2D Dir(Ball.X - Foot.X, Ball.Y - Foot.Y);
			// 1mm 미만이면 발끝 방향이 정의되지 않는다 - 0 으로 두면 «정면» 으로 읽혀 조용히 통과한다.
			if (Dir.SizeSquared() < 0.01f)
			{
				OutError = LOCTEXT("LandingToeDegenerate", "발과 발끝 뼈가 겹쳐 발끝 방향을 잴 수 없다.");
				return false;
			}
			OutYaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
			return true;
		};

		// IK 부모 뼈의 «로컬» 회전. 컴포넌트 공간이 아니라 로컬을 보는 것이 이 검사의 존재 이유다.
		{
			const int32 IkRootPose = Bones.GetPoseBoneIndexForBoneName(Rules.IkRootBone);
			if (IkRootPose == INDEX_NONE)
			{
				OutError = FText::Format(LOCTEXT("IkRootBoneMissing", "IK 부모 뼈 '{0}' 이 스켈레톤에 없다."), FText::FromName(Rules.IkRootBone));
				return false;
			}
			const FCompactPoseBoneIndex IkRootCompact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(IkRootPose));
			Out.IkRootLocalDeg = FMath::RadiansToDegrees(Pose[IkRootCompact].GetRotation().AngularDistance(FQuat::Identity));
		}

		FVector FootL, FootR, BallL, BallR;
		if (!RelativeToRoot(Rules.FootBoneL, FootL) || !RelativeToRoot(Rules.FootBoneR, FootR)
			|| !RelativeToRoot(Rules.BallBoneL, BallL) || !RelativeToRoot(Rules.BallBoneR, BallR))
		{
			return false;
		}
		Out.FootL = FVector2D(FootL.X, FootL.Y);
		Out.FootR = FVector2D(FootR.X, FootR.Y);
		return ToeYaw(FootL, BallL, Out.YawL) && ToeYaw(FootR, BallR, Out.YawR);
	}

	// 두 표본의 차 - 발 위치(cm)와 발끝 각(도). 왼발·오른발을 따로 재고 큰 쪽을 낸다. 한쪽만 넘어도 발화한다.
	void FeetDelta(const FFeetSample& A, const FFeetSample& B, float& OutCm, float& OutDeg)
	{
		OutCm  = FMath::Max(FVector2D::Distance(A.FootL, B.FootL), FVector2D::Distance(A.FootR, B.FootR));
		OutDeg = FMath::Max(FMath::Abs(FRotator::NormalizeAxis(A.YawL - B.YawL)),
		                    FMath::Abs(FRotator::NormalizeAxis(A.YawR - B.YawR)));
	}

	// 규칙이 «재는 데 필요한 값» 을 다 갖고 있나. 비어 있으면 그 이유를 돌려준다.
	// 임계 0 은 «모든 자산 통과» 가 아니라 «검사가 안 걸린 것» 이므로 통과로 떨어뜨리지 않는다.
	FText RulesProblem(const FVBTurnInPlaceValidationRules& R)
	{
		if (R.FootBoneL.IsNone() || R.FootBoneR.IsNone() || R.BallBoneL.IsNone() || R.BallBoneR.IsNone())
		{
			return LOCTEXT("RulesBones", "발·발끝 뼈 이름이 비었다");
		}
		auto Bad = [](float Warn, float Error) { return Warn <= 0.0f || Error <= 0.0f || Warn > Error; };
		if (Bad(R.EntryWarnCm, R.EntryErrorCm) || Bad(R.EntryWarnDeg, R.EntryErrorDeg))
		{
			return LOCTEXT("RulesEntry", "진입 임계가 비었거나 경고가 오류보다 크다");
		}
		if (Bad(R.LandingWarnCm, R.LandingErrorCm) || Bad(R.LandingWarnDeg, R.LandingErrorDeg))
		{
			return LOCTEXT("RulesLanding", "착지 임계가 비었거나 경고가 오류보다 크다");
		}
		if (R.MaxNetYawAfterWarpDeg <= 0.0f)
		{
			return LOCTEXT("RulesNetYaw", "워프 뒤 순회전 허용치가 비었다");
		}
		if (R.IkRootBone.IsNone() || R.MaxIkRootLocalDeg <= 0.0f)
		{
			return LOCTEXT("RulesIkRoot", "IK 부모 뼈 이름이나 로컬 회전 허용각이 비었다");
		}
		return FText::GetEmpty();
	}
}

EDataValidationResult UVBTurnInPlaceConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 조합(무기 x 자세)별로 모아서 방향 둘을 각각 본다.
	//   왜 여기서 막나: 반쪽 프로필은 런타임에 «그 방향으로 못 도는 캐릭터» 를 만든다. 막을 수 있는
	//   실수를 런타임 폴백으로 넘기면 폴백이 정상 경로가 되고, 그러면 데이터가 틀린 것을 아무도 모른다.
	struct FCombo
	{
		EVBWeaponType PoseWeapon;
		bool bCrouching;
		bool operator==(const FCombo& Other) const { return PoseWeapon == Other.PoseWeapon && bCrouching == Other.bCrouching; }
	};

	// 검증 규칙이 비어 있으면 자세·순회전 검사는 «못 잰 것» 이다. 오류를 내고 그 검사만 건너뛴다 -
	//   나머지 검사는 규칙과 무관하므로 계속 돈다.
	const FText RulesError = VBTurnInPlaceAnalysis::RulesProblem(ValidationRules);
	const bool bRulesOK = RulesError.IsEmpty();
	if (!bRulesOK)
	{
		Context.AddError(FText::Format(
			LOCTEXT("RulesUnset", "검증 규칙 미설정: {0}. 재지 못하는 검사는 통과가 아니라 오류다 - DA_TurnInPlace 의 ValidationRules 를 저작하라."),
			RulesError));
		Result = EDataValidationResult::Invalid;
	}

	auto FindLandingReference = [this](EVBWeaponType InWeapon, bool bInCrouching) -> const FVBTurnInPlaceLandingPoseReference*
	{
		for (const FVBTurnInPlaceLandingPoseReference& Ref : LandingPoseReferences)
		{
			if (Ref.PoseWeapon == InWeapon && Ref.bCrouching == bInCrouching) { return &Ref; }
		}
		return nullptr;
	};

	TArray<FCombo> Combos;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FVBTurnInPlaceEntry& Entry = Entries[Index];

		if (!Entry.Montage)
		{
			Context.AddError(FText::Format(
				LOCTEXT("NoMontage", "행 {0}: 몽타주가 비었다. 루트모션이 켜진 몽타주가 있어야 회전이 캡슐에 실린다."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		if (FMath::IsNearlyZero(Entry.ReferenceYaw))
		{
			Context.AddError(FText::Format(
				LOCTEXT("NoSign", "행 {0}: 기준각이 0 이다. 부호가 곧 방향이라 0 은 어느 쪽도 담당하지 못한다."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.RequestMax <= Entry.RequestMin)
		{
			Context.AddError(FText::Format(
				LOCTEXT("EmptyRange", "행 {0}: 담당 구간이 비었다 ({1} ~ {2})."),
				FText::AsNumber(Index), FText::AsNumber(Entry.RequestMin), FText::AsNumber(Entry.RequestMax)));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.PlayRate <= 0.0f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("BadPlayRate", "행 {0}: 재생 속도가 0 이하다."), FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		// 양방향은 |기준각| 180 에서만 방위가 같다. 90 행에 켜면 반대 요청의 목표가 180 남아 워프 방향이 미정이다.
		if (Entry.bServesBothDirections && FMath::Abs(FMath::Abs(Entry.ReferenceYaw) - 180.0f) > 1.0f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("BothDirectionsNot180", "행 {0}: 양방향 행은 기준각이 ±180 이어야 한다 (지금 {1}). 그 밖의 각은 반대 요청의 방위가 다르다."),
				FText::AsNumber(Index), FText::AsNumber(Entry.ReferenceYaw)));
			Result = EDataValidationResult::Invalid;
		}

		// 자산이 실제로 무엇을 하는지 단언한다. 위의 검사들은 전부 «설정끼리» 의 정합이라,
		//   자산이 못 쓸 물건이어도 초록불이 난다. 2026-08-30 에 실제로 그렇게 넷이 통과했다.
		if (Entry.Montage)
		{
			const FText Name = FText::FromString(Entry.Montage->GetName());

			// 워프 창을 먼저 읽는다. 루트 측정(창 뒤 순회전)과 블렌드아웃 판정이 둘 다 이 값을 쓴다.
			int32 WarpCount = 0;
			float WarpEnd = 0.0f;
			for (const FAnimNotifyEvent& Ev : Entry.Montage->Notifies)
			{
				const UAnimNotifyState_MotionWarping* Warp = Cast<UAnimNotifyState_MotionWarping>(Ev.NotifyStateClass);
				if (!Warp) { continue; }
				++WarpCount;
				WarpEnd = FMath::Max(WarpEnd, Ev.GetTriggerTime() + Ev.GetDuration());
				if (const URootMotionModifier_Warp* Mod = Cast<URootMotionModifier_Warp>(Warp->RootMotionModifier))
				{
					// 회전 방식은 Slerp 여야 한다 (2026-09-04, Codex 지적). Scale·ConstantRate 는 WarpMaxRotationRate 로 회전을
					//   잘라 담당 구간 상한(180 초과)을 실제로 못 낼 수 있는데, 그 상한 도달은 이 검증기가 재지 않는다.
					//   Slerp 는 클립의 남은 루트 회전과 목표 사이를 최단 호로 잇고(엔진 URootMotionModifier_Warp::WarpRotation),
					//   그 차이가 180 미만이면 같은 방향으로 늘어난다 - 담당 상한 <= 기준각 x 배율 상한 검사가 그것을 보장한다.
					if (Mod->RotationMethod != EMotionWarpRotationMethod::Slerp)
					{
						Context.AddError(FText::Format(LOCTEXT("WarpRotationMethod",
							"행 {0} ({1}): 워프 회전 방식이 Slerp 가 아니다. 다른 방식은 회전율 상한이 담당 구간 상한을 잘라낼 수 있고 검증기가 그것을 재지 않는다."),
							FText::AsNumber(Index), Name));
						Result = EDataValidationResult::Invalid;
					}
					if (!Mod->bWarpRotation || Mod->bWarpTranslation)
					{
						Context.AddError(FText::Format(LOCTEXT("WarpFlags",
							"행 {0} ({1}): 워프 설정이 제자리 회전 규격이 아니다 (회전={2} 위치={3}). 위치 워프가 켜지면 캐릭터가 목표 지점으로 끌려간다."),
							FText::AsNumber(Index), Name,
							FText::FromString(Mod->bWarpRotation ? TEXT("켜짐") : TEXT("꺼짐")),
							FText::FromString(Mod->bWarpTranslation ? TEXT("켜짐") : TEXT("꺼짐"))));
						Result = EDataValidationResult::Invalid;
					}
				}
			}
			if (WarpCount != 1)
			{
				Context.AddError(FText::Format(LOCTEXT("WarpCount",
					"행 {0} ({1}): 모션 워프 노티가 {2}개다. 정확히 하나여야 요청각이 클립 각도로 늘어난다 - 0개면 클립의 원래 각도만 나온다."),
					FText::AsNumber(Index), Name, FText::AsNumber(WarpCount)));
				Result = EDataValidationResult::Invalid;
			}

			const VBTurnInPlaceAnalysis::FRootMotionProfile P =
				VBTurnInPlaceAnalysis::Measure(*Entry.Montage, Entry.PlayRate * Entry.Montage->RateScale, WarpEnd);

			if (P.SegmentCount != 1)
			{
				Context.AddError(FText::Format(LOCTEXT("SegCount",
					"행 {0} ({1}): 슬롯 세그먼트가 {2}개다. 제자리 회전은 클립 하나여야 한다 - 이어붙이면 어느 구간이 회전을 내는지 잴 수 없다."),
					FText::AsNumber(Index), Name, FText::AsNumber(P.SegmentCount)));
				Result = EDataValidationResult::Invalid;
			}
			if (P.bTimeStretched)
			{
				// 시간 늘림 커브가 걸리면 «몽타주 시간 ↔ 실시간» 이 상수가 아니라 블렌드아웃 시각을 셈할 수 없다.
				//   셈하지 못하는 것을 통과시키지 않는다.
				Context.AddError(FText::Format(LOCTEXT("TimeStretched",
					"행 {0} ({1}): TimeStretchCurve 가 걸려 있어 블렌드아웃 시각을 셈할 수 없다. 제자리 회전 몽타주에는 쓰지 않는다."),
					FText::AsNumber(Index), Name));
				Result = EDataValidationResult::Invalid;
			}
			if (const UAnimSequence* AsSeq = Cast<UAnimSequence>(P.Sequence))
			{
				if (!AsSeq->bEnableRootMotion)
				{
					Context.AddError(FText::Format(LOCTEXT("NoRootMotion",
						"행 {0} ({1}): 시퀀스에 루트모션이 꺼져 있다. 회전이 포즈에만 남고 캡슐은 안 돈다."),
						FText::AsNumber(Index), Name));
					Result = EDataValidationResult::Invalid;
				}
			}
			if (P.bMeasured)
			{
				if (FMath::Sign(P.TotalYaw) != FMath::Sign(Entry.ReferenceYaw)
					|| FMath::Abs(FMath::Abs(P.TotalYaw) - FMath::Abs(Entry.ReferenceYaw)) > 1.0f)
				{
					Context.AddError(FText::Format(LOCTEXT("RefYawDrift",
						"행 {0} ({1}): 기준각은 {2} 인데 자산의 실측 총회전은 {3} 이다. 기준각은 자산의 사본이라 어긋나면 담당 구간과 워프 배율이 전부 틀어진다."),
						FText::AsNumber(Index), Name, FText::AsNumber(Entry.ReferenceYaw),
						FText::AsNumber(FMath::RoundToFloat(P.TotalYaw * 10.0f) / 10.0f)));
					Result = EDataValidationResult::Invalid;
				}
				// 오버슈트를 «양» 으로 본다. 금지가 아니다 (2026-08-31 개정).
				// 종전 규칙은 「한 방향으로만 돌 것」이었는데 그것이 틀렸다 - 넘어갔다 되돌아오는 것은
				//   결함이 아니라 저작된 «가라앉는 동작» 이고, 다리의 발놀림이 바로 그 움직임과 짝을 이룬다.
				//   그 규칙을 믿고 루트를 단조로 펴자 몸은 멈췄는데 다리만 그 발놀림을 계속했다
				//   (user: 「다 돌았다 싶었을 때 다리가 막 돈다」).
				// 대조: 손대지 않은 격투 180 은 같은 조건에서 멀쩡했다 - 그것이 이 판정의 근거다.
				// 그래도 상한은 둔다. 목표를 크게 넘겨 도는 것은 캡슐이 눈에 띄게 과회전한다는 뜻이다.
				constexpr float MaxOvershootDegrees = 45.0f;
				const float Overshoot = FMath::Abs(P.PeakYaw) - FMath::Abs(P.TotalYaw);
				if (Overshoot > MaxOvershootDegrees)
				{
					Context.AddError(FText::Format(LOCTEXT("RootOvershootTooLarge",
						"행 {0} ({1}): 루트가 목표를 {2}도 넘겨 돈다 (최대 {3}, 총 {4}). 허용 {5}도를 넘으면 캡슐이 눈에 띄게 과회전한다."),
						FText::AsNumber(Index), Name,
						FText::AsNumber(FMath::RoundToFloat(Overshoot * 10.0f) / 10.0f),
						FText::AsNumber(FMath::RoundToFloat(P.PeakYaw * 10.0f) / 10.0f),
						FText::AsNumber(FMath::RoundToFloat(P.TotalYaw * 10.0f) / 10.0f),
						FText::AsNumber(FMath::RoundToInt(MaxOvershootDegrees))));
					Result = EDataValidationResult::Invalid;
				}
				// 블렌드아웃이 시작되면 엔진이 몽타주를 ActiveMontagesMap 에서 빼고 RootMotionMontageInstance
				//   를 지운다(AnimInstance.cpp::ClearMontageInstanceReferences). 그 뒤 회전은 캡슐에 안 실린다.
				// 시간을 비교하지 않고 «실제로 잃는 각» 을 잰다 (2026-08-30 개정).
				//   종전에는 「회전 종료 시각 > 블렌드아웃 시작」이면 무조건 막았는데, 그 판정은
				//   3도를 잃는 클립과 30도를 잃는 클립을 구별하지 못한다. 회전이 클립 끝까지 이어지는
				//   자산은 정상적으로 존재하고(원본 셋이 그렇다), 그것을 억지로 맞추려고 루트를
				//   시간 압축하면 그 차이가 골반과 IK 기준틀로 옮겨가 훨씬 큰 문제가 된다 - 실제로 그랬다.
				//   그래서 규칙을 「얼마나 잃는가」로 바꾼다. 5% 를 넘으면 막고, 그 아래는 말만 한다.
				// 워프 창이 블렌드아웃까지 닿아 있으면 최종 각은 워프가 정한다 - 클립이 그 뒤에
				//   얼마를 더 돌든 「덜 돈다」가 아니다. 이 조건을 안 보면 안전망이 거짓말을 한다
				//   (2026-09-01: 정상 설정에서 「6.8도 덜 돈다」고 7건이 울었는데 전부 거짓이었다).
				const bool bWarpGovernsFinalYaw = (WarpEnd >= P.BlendOutStart - 0.01f);
				const float LostRatio = bWarpGovernsFinalYaw
					? 0.0f
					: P.LostYaw / FMath::Max(1.0f, FMath::Abs(P.TotalYaw));
				if (LostRatio > 0.05f)
				{
					Context.AddError(FText::Format(LOCTEXT("RotationAfterBlendOut",
						"행 {0} ({1}): 블렌드아웃({2}초 시작) 뒤에 회전이 {3}도 남아 캡슐에 실리지 않는다 (총 {4}도의 {5}%). 요청보다 그만큼 덜 돈다 - 블렌드아웃을 줄이거나 워프 창을 그 앞으로 옮겨라."),
						FText::AsNumber(Index), Name,
						FText::AsNumber(FMath::RoundToFloat(P.BlendOutStart * 1000.0f) / 1000.0f),
						FText::AsNumber(FMath::RoundToFloat(P.LostYaw * 10.0f) / 10.0f),
						FText::AsNumber(FMath::RoundToFloat(FMath::Abs(P.TotalYaw))),
						FText::AsNumber(FMath::RoundToInt(LostRatio * 100.0f))));
					Result = EDataValidationResult::Invalid;
				}
				else if (!bWarpGovernsFinalYaw && P.LostYaw > 0.5f)
				{
					Context.AddWarning(FText::Format(LOCTEXT("RotationAfterBlendOutMinor",
						"행 {0} ({1}): 블렌드아웃 뒤에 회전이 {2}도 남는다 (총 {3}도의 {4}%). 허용 범위지만 요청보다 그만큼 덜 돈다."),
						FText::AsNumber(Index), Name,
						FText::AsNumber(FMath::RoundToFloat(P.LostYaw * 10.0f) / 10.0f),
						FText::AsNumber(FMath::RoundToFloat(FMath::Abs(P.TotalYaw))),
						FText::AsNumber(FMath::RoundToInt(LostRatio * 100.0f))));
				}

				// 넘친 각도 본다. 재놓고 판정에 안 쓰면 없는 것만 못하다 - 덮여 있다는 착각을 준다.
				//   2026-09-01 에 이 파일에서 그런 값이 셋 나왔다(되돌아간 양·도달 시각·손실각).
				// 왜 결함인가: 블렌드아웃이 시작되면 캡슐은 그 순간의 각에 선 채로 끊긴다.
				//   그때 이미 목표를 넘겨 서 있으면 캐릭터가 요청보다 더 돈 자리에서 멈춘다.
				const float ExcessRatio = bWarpGovernsFinalYaw
					? 0.0f
					: P.ExcessYaw / FMath::Max(1.0f, FMath::Abs(P.TotalYaw));
				if (ExcessRatio > 0.05f)
				{
					Context.AddError(FText::Format(LOCTEXT("RotationExcessAtBlendOut",
						"행 {0} ({1}): 블렌드아웃 시작 시점에 캡슐이 목표를 {2}도 지나쳐 있다 (총 {3}도의 {4}%). 그 자리에서 끊기므로 요청보다 그만큼 더 돈다."),
						FText::AsNumber(Index), Name,
						FText::AsNumber(FMath::RoundToFloat(P.ExcessYaw * 10.0f) / 10.0f),
						FText::AsNumber(FMath::RoundToFloat(FMath::Abs(P.TotalYaw))),
						FText::AsNumber(FMath::RoundToInt(ExcessRatio * 100.0f))));
					Result = EDataValidationResult::Invalid;
				}

				// 워프 창이 닫힌 뒤 클립 끝까지의 순회전 (2026-09-02, Codex 교차검토). 워프는 창 안에서만
				//   보정하므로 창 밖의 회전은 원값 그대로 캡슐에 실린다. 저작된 되돌림(넘었다 돌아옴)은 순회전이
				//   0 이라 괜찮고, 0 이 아니면 요청각에서 그만큼 벗어난 자리에 선다. 지금 자산 12개는 «도달» 에서
				//   창을 닫아 순회전이 0 이지만, 미래 자산을 실측이 아니라 규칙으로 막는다.
				if (bRulesOK && FMath::Abs(P.NetYawAfterWarp) > ValidationRules.MaxNetYawAfterWarpDeg)
				{
					Context.AddError(FText::Format(LOCTEXT("NetYawAfterWarp",
						"행 {0} ({1}): 워프 창이 {2}초에 닫힌 뒤 클립 끝까지 루트가 {3}도 더 돈다 (허용 {4}). 창 밖 회전은 워프가 못 보정해 요청각에서 그만큼 벗어난다 - 창을 도달 시점까지 늘려라."),
						FText::AsNumber(Index), Name,
						FText::AsNumber(FMath::RoundToFloat(WarpEnd * 1000.0f) / 1000.0f),
						FText::AsNumber(FMath::RoundToFloat(P.NetYawAfterWarp * 10.0f) / 10.0f),
						FText::AsNumber(ValidationRules.MaxNetYawAfterWarpDeg)));
					Result = EDataValidationResult::Invalid;
				}
			}

			if (WarpCount == 1 && WarpEnd > P.BlendOutStart + 0.02f)
			{
				Context.AddError(FText::Format(LOCTEXT("WarpWindowLate",
					"행 {0} ({1}): 워프 창이 {2}초에 닫히는데 블렌드아웃은 {3}초에 시작한다. 창 끝부분의 보정이 감쇠돼 요청각에 못 미친다."),
					FText::AsNumber(Index), Name,
					FText::AsNumber(FMath::RoundToFloat(WarpEnd * 1000.0f) / 1000.0f),
					FText::AsNumber(FMath::RoundToFloat(P.BlendOutStart * 1000.0f) / 1000.0f)));
				Result = EDataValidationResult::Invalid;
			}

			// 착지 자세 (2026-09-02, FIND-105). 위의 검사들은 전부 «루트» 만 본다. 회전 끝에 다리가 더 도는 증상의
			//   원인은 루트가 아니라 «몽타주가 대기로 넘기는 순간의 발 자세» 였다 - 미러 클립은 모든 프레임이 대기와
			//   86~132cm 어긋났고, 이른 페이드는 디딤 중 자세(100cm)에서 넘겼다. 블렌드는 그 거리를 발 미끄러짐으로
			//   만들고 LegIK 가 그것을 다리 회전으로 바꾼다. 아홉 가설이 전부 «넘기는 방법» 을 바꿨고 이 거리를 재지
			//   않았다. 세 시각(첫 프레임 / 블렌드아웃 시작 / 마지막 프레임)의 발을 대기 참조 0초와 비교한다.
			//   첫 프레임은 진입 임계, 나머지 둘은 착지 임계다.
			if (bRulesOK && P.SegmentCount == 1 && !P.bTimeStretched)
			{
				const FVBTurnInPlaceLandingPoseReference* Ref = FindLandingReference(Entry.PoseWeapon, Entry.bCrouching);
				const USkeleton* Skeleton = Entry.Montage->GetSkeleton();
				if (!Ref)
				{
					Context.AddError(FText::Format(LOCTEXT("NoLandingReference",
						"행 {0} ({1}): 무기 {2} / 앉기 {3} 의 착지 자세 참조가 없다. 대기 자세를 모르면 착지를 잴 수 없다 - LandingPoseReferences 에 넣어라."),
						FText::AsNumber(Index), Name,
						FText::AsNumber(static_cast<int32>(Entry.PoseWeapon)), FText::AsNumber(Entry.bCrouching ? 1 : 0)));
					Result = EDataValidationResult::Invalid;
				}
				else if (!Ref->Idle)
				{
					Context.AddError(FText::Format(LOCTEXT("LandingReferenceEmpty",
						"행 {0} ({1}): 착지 자세 참조의 대기 클립이 비었다."), FText::AsNumber(Index), Name));
					Result = EDataValidationResult::Invalid;
				}
				else if (!Skeleton || Ref->Idle->GetSkeleton() != Skeleton)
				{
					Context.AddError(FText::Format(LOCTEXT("LandingSkeleton",
						"행 {0} ({1}): 회전 몽타주와 대기 참조 '{2}' 의 스켈레톤이 다르다. 다른 스켈레톤의 발은 비교할 수 없다."),
						FText::AsNumber(Index), Name, FText::FromString(Ref->Idle->GetName())));
					Result = EDataValidationResult::Invalid;
				}
				else
				{
					using namespace VBTurnInPlaceAnalysis;
					FFeetSample IdleFeet;
					FText SampleError;
					const UAnimSequence* IdleSeq = Ref->Idle;
					const bool bIdleOK = SampleFeet(*Skeleton,
						[IdleSeq](FAnimationPoseData& PoseData, const FAnimExtractContext& Ctx) { IdleSeq->GetAnimationPose(PoseData, Ctx); },
						0.0f, ValidationRules, IdleFeet, SampleError);
					if (!bIdleOK)
					{
						Context.AddError(FText::Format(LOCTEXT("LandingIdleSampleFail",
							"행 {0} ({1}): 대기 참조 '{2}' 의 발을 재지 못했다 - {3}"),
							FText::AsNumber(Index), Name, FText::FromString(IdleSeq->GetName()), SampleError));
						Result = EDataValidationResult::Invalid;
					}
					else if (IdleFeet.IkRootLocalDeg > ValidationRules.MaxIkRootLocalDeg)
					{
						Context.AddError(FText::Format(LOCTEXT("IdleIkRootConvention",
							"행 {0} ({1}): 대기 참조 '{2}' 의 IK 부모 뼈 로컬 회전이 {3}도다 (허용 {4}). 항등이 아니면 회전 클립과 섞일 때 IK 목표가 돈다."),
							FText::AsNumber(Index), Name, FText::FromString(IdleSeq->GetName()),
							FText::AsNumber(FMath::RoundToFloat(IdleFeet.IkRootLocalDeg * 10.0f) / 10.0f),
							FText::AsNumber(ValidationRules.MaxIkRootLocalDeg)));
						Result = EDataValidationResult::Invalid;
					}
					else
					{
						const FAnimTrack& Track = Entry.Montage->SlotAnimTracks[0].AnimTrack;
						struct FPoint { float Time; FText Label; bool bEntry; };
						const FPoint Points[3] =
						{
							{ 0.0f,                            LOCTEXT("PointEntry",    "첫 프레임"),      true  },
							{ P.BlendOutStart,                 LOCTEXT("PointBlendOut", "블렌드아웃 시작"), false },
							{ Entry.Montage->GetPlayLength(),  LOCTEXT("PointLast",     "마지막 프레임"),   false },
						};
						for (const FPoint& Point : Points)
						{
							FFeetSample Feet;
							if (!SampleFeet(*Skeleton,
								[&Track](FAnimationPoseData& PoseData, const FAnimExtractContext& Ctx) { Track.GetAnimationPose(PoseData, Ctx); },
								Point.Time, ValidationRules, Feet, SampleError))
							{
								Context.AddError(FText::Format(LOCTEXT("LandingSampleFail",
									"행 {0} ({1}): {2}의 발을 재지 못했다 - {3}"),
									FText::AsNumber(Index), Name, Point.Label, SampleError));
								Result = EDataValidationResult::Invalid;
								break;
							}
							// IK 부모 뼈 관례 (FIND-105 의 진짜 원인). 컴포넌트 공간의 |ik_foot - foot| 이 0 이어도
							//   이 뼈가 루트를 되감는 180 을 로컬로 들고 있으면 대기와 섞일 때 IK 목표가 360도를 쓴다.
							if (Feet.IkRootLocalDeg > ValidationRules.MaxIkRootLocalDeg)
							{
								Context.AddError(FText::Format(LOCTEXT("IkRootConvention",
									"행 {0} ({1}): {2}({3}초)에서 IK 부모 뼈의 로컬 회전이 {4}도다 (허용 {5}). 대기 클립(항등)과 섞일 때 부모 180 과 자식 180 이 각자 최단 호를 골라 IK 목표가 한 바퀴 돈다 - bake_ik_foot_tracks 로 정규화하라."),
									FText::AsNumber(Index), Name, Point.Label,
									FText::AsNumber(FMath::RoundToFloat(Point.Time * 1000.0f) / 1000.0f),
									FText::AsNumber(FMath::RoundToFloat(Feet.IkRootLocalDeg * 10.0f) / 10.0f),
									FText::AsNumber(ValidationRules.MaxIkRootLocalDeg)));
								Result = EDataValidationResult::Invalid;
							}
							float Cm = 0.0f, Deg = 0.0f;
							FeetDelta(Feet, IdleFeet, Cm, Deg);
							const float WarnCm  = Point.bEntry ? ValidationRules.EntryWarnCm   : ValidationRules.LandingWarnCm;
							const float ErrCm   = Point.bEntry ? ValidationRules.EntryErrorCm  : ValidationRules.LandingErrorCm;
							const float WarnDeg = Point.bEntry ? ValidationRules.EntryWarnDeg  : ValidationRules.LandingWarnDeg;
							const float ErrDeg  = Point.bEntry ? ValidationRules.EntryErrorDeg : ValidationRules.LandingErrorDeg;
							if (Cm > ErrCm || Deg > ErrDeg)
							{
								Context.AddError(FText::Format(LOCTEXT("LandingPoseFar",
									"행 {0} ({1}): {2}({3}초)의 발이 대기 자세에서 {4}cm / {5}도 떨어져 있다 (허용 {6}cm / {7}도). 블렌드가 그 거리를 발 미끄러짐으로 만들고 LegIK 가 다리 회전으로 바꾼다 - 미러 클립이거나 페이드가 정착 구간을 자른 것이다 (FIND-105)."),
									FText::AsNumber(Index), Name, Point.Label,
									FText::AsNumber(FMath::RoundToFloat(Point.Time * 1000.0f) / 1000.0f),
									FText::AsNumber(FMath::RoundToFloat(Cm * 10.0f) / 10.0f),
									FText::AsNumber(FMath::RoundToFloat(Deg * 10.0f) / 10.0f),
									FText::AsNumber(ErrCm), FText::AsNumber(ErrDeg)));
								Result = EDataValidationResult::Invalid;
							}
							else if (Cm > WarnCm || Deg > WarnDeg)
							{
								Context.AddWarning(FText::Format(LOCTEXT("LandingPoseNear",
									"행 {0} ({1}): {2}({3}초)의 발이 대기 자세에서 {4}cm / {5}도 떨어져 있다 (경고 {6}cm / {7}도). 허용 안이지만 블렌드가 그만큼 발을 옮긴다."),
									FText::AsNumber(Index), Name, Point.Label,
									FText::AsNumber(FMath::RoundToFloat(Point.Time * 1000.0f) / 1000.0f),
									FText::AsNumber(FMath::RoundToFloat(Cm * 10.0f) / 10.0f),
									FText::AsNumber(FMath::RoundToFloat(Deg * 10.0f) / 10.0f),
									FText::AsNumber(WarnCm), FText::AsNumber(WarnDeg)));
							}
						}
					}
				}
			}
		}

		Combos.AddUnique(FCombo{Entry.PoseWeapon, Entry.bCrouching});
	}

	// 착지 참조 장부 - 조합당 정확히 하나. 중복이면 어느 것이 기준인지가 배열 순서에 달리고, 쓰이지 않는 참조는
	//   낡은 채로 남아 나중에 엉뚱한 조합의 기준이 된다. 둘 다 오류다.
	for (int32 RefIndex = 0; RefIndex < LandingPoseReferences.Num(); ++RefIndex)
	{
		const FVBTurnInPlaceLandingPoseReference& Ref = LandingPoseReferences[RefIndex];
		int32 SameCombo = 0;
		for (const FVBTurnInPlaceLandingPoseReference& Other : LandingPoseReferences)
		{
			if (Other.PoseWeapon == Ref.PoseWeapon && Other.bCrouching == Ref.bCrouching) { ++SameCombo; }
		}
		if (SameCombo > 1)
		{
			Context.AddError(FText::Format(LOCTEXT("LandingReferenceDuplicate",
				"착지 참조 {0}: 무기 {1} / 앉기 {2} 의 참조가 {3}개다. 조합당 하나여야 기준이 하나다."),
				FText::AsNumber(RefIndex), FText::AsNumber(static_cast<int32>(Ref.PoseWeapon)),
				FText::AsNumber(Ref.bCrouching ? 1 : 0), FText::AsNumber(SameCombo)));
			Result = EDataValidationResult::Invalid;
		}
		if (!Combos.Contains(FCombo{Ref.PoseWeapon, Ref.bCrouching}))
		{
			Context.AddError(FText::Format(LOCTEXT("LandingReferenceUnused",
				"착지 참조 {0}: 무기 {1} / 앉기 {2} 에는 회전 행이 없다. 쓰이지 않는 참조는 지운다."),
				FText::AsNumber(RefIndex), FText::AsNumber(static_cast<int32>(Ref.PoseWeapon)),
				FText::AsNumber(Ref.bCrouching ? 1 : 0)));
			Result = EDataValidationResult::Invalid;
		}
	}

	for (const FCombo& Combo : Combos)
	{
		for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
		{
			const float Sign = (SideIndex == 0) ? -1.0f : 1.0f;
			const FText SideText = (SideIndex == 0) ? LOCTEXT("Left", "왼쪽") : LOCTEXT("Right", "오른쪽");

			TArray<const FVBTurnInPlaceEntry*> Side;
			for (const FVBTurnInPlaceEntry& Entry : Entries)
			{
				// 양방향 행은 두 쪽 목록에 다 들어간다 - 그쪽 구간을 실제로 그 행이 낸다.
				if (Entry.PoseWeapon == Combo.PoseWeapon && Entry.bCrouching == Combo.bCrouching
					&& (FMath::Sign(Entry.ReferenceYaw) == Sign || Entry.bServesBothDirections))
				{
					Side.Add(&Entry);
				}
			}

			if (Side.Num() == 0)
			{
				// 이것이 이 검사의 존재 이유다 - 한쪽만 등록하면 그 조합은 반대쪽으로 회전 자체를 못 한다.
				Context.AddError(FText::Format(
					LOCTEXT("HalfProfile", "무기 {0} / 앉기 {1}: {2} 방향 행이 없다. 프로필은 방향 쌍으로만 등록한다."),
					FText::AsNumber(static_cast<int32>(Combo.PoseWeapon)),
					FText::AsNumber(Combo.bCrouching ? 1 : 0), SideText));
				Result = EDataValidationResult::Invalid;
				continue;
			}

			Side.Sort([](const FVBTurnInPlaceEntry& A, const FVBTurnInPlaceEntry& B)
			{
				return A.RequestMin < B.RequestMin;
			});

			// 구간이 이어지는지 - 빈틈이면 그 각도 요청이 폴백으로 떨어지고, 겹치면 어느 행이 이길지가
			//   배열 순서에 달리게 된다(데이터를 재정렬하면 조용히 동작이 바뀐다).
			for (int32 Index = 1; Index < Side.Num(); ++Index)
			{
				const float PrevMax = Side[Index - 1]->RequestMax;
				const float CurMin  = Side[Index]->RequestMin;
				if (!FMath::IsNearlyEqual(PrevMax, CurMin, 0.01f))
				{
					Context.AddError(FText::Format(
						LOCTEXT("RangeSeam", "무기 {0} / 앉기 {1} / {2}: 구간이 안 맞는다 ({3} 다음이 {4})."),
						FText::AsNumber(static_cast<int32>(Combo.PoseWeapon)),
						FText::AsNumber(Combo.bCrouching ? 1 : 0), SideText,
						FText::AsNumber(PrevMax), FText::AsNumber(CurMin)));
					Result = EDataValidationResult::Invalid;
				}
			}

			// 각 행이 «자기 담당 구간을 실제로 낼 수 있나» - 클립 각도에 워프 배율을 곱한 범위 안에
			//   구간 전체가 들어와야 한다. 안 그러면 데이터는 담당한다고 적어놓고 런타임에는 못 낸 만큼
			//   조용히 모자란 회전을 낸다(90 클립 x 상한 1.4 = 126도인데 130도 구간을 적는 식).
			for (const FVBTurnInPlaceEntry* Entry : Side)
			{
				const float RefMag = FMath::Abs(Entry->ReferenceYaw);
				const float MinOut = RefMag * Entry->WarpScaleRange.X;
				const float MaxOut = RefMag * Entry->WarpScaleRange.Y;
				if (Entry->RequestMin < MinOut - 0.01f || Entry->RequestMax > MaxOut + 0.01f)
				{
					Context.AddError(FText::Format(
						LOCTEXT("RangeUnreachable", "무기 {0} / 앉기 {1} / {2}: 담당 구간 [{3}, {4}) 를 이 클립이 못 낸다 (기준각 {5} x 배율 -> [{6}, {7}])."),
						FText::AsNumber(static_cast<int32>(Combo.PoseWeapon)),
						FText::AsNumber(Combo.bCrouching ? 1 : 0), SideText,
						FText::AsNumber(Entry->RequestMin), FText::AsNumber(Entry->RequestMax),
						FText::AsNumber(RefMag), FText::AsNumber(MinOut), FText::AsNumber(MaxOut)));
					Result = EDataValidationResult::Invalid;
				}
			}

			// 위쪽 끝은 180 «초과» 여야 한다. ResolveRequest 의 비교가 `>= RequestMax` 로 배타적이라
			//   RequestMax 를 정확히 180 으로 두면 «정확히 180도» 요청이 어느 행에도 안 걸린다.
			if (Side.Last()->RequestMax <= 180.0f)
			{
				Context.AddError(FText::Format(
					LOCTEXT("NoTop", "무기 {0} / 앉기 {1} / {2}: 구간의 끝이 180 을 넘지 않는다 (끝 {3}). 비교가 배타적이라 정확히 180 요청이 빠진다."),
					FText::AsNumber(static_cast<int32>(Combo.PoseWeapon)),
					FText::AsNumber(Combo.bCrouching ? 1 : 0), SideText,
					FText::AsNumber(Side.Last()->RequestMax)));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
