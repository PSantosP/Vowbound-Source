#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Animation/VBLadderIKContact.h"
#include "AnimNode_VBLadderIK.generated.h"

// 현재 입력 포즈에서 접촉을 판정하고 같은 평가 안에서 사다리 목표를 푼다.
USTRUCT(BlueprintInternalUseOnly)
struct VOWBOUND_API FAnimNode_VBLadderIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	FAnimNode_VBLadderIK();

	UPROPERTY(EditAnywhere, Category="Contact", meta=(ClampMin="0"))
	float PlantSpeed = 15.0f;
	UPROPERTY(EditAnywhere, Category="Contact", meta=(ClampMin="0"))
	float ReleaseSpeed = 40.0f;
	UPROPERTY(EditAnywhere, Category="Contact", meta=(ClampMin="0"))
	float PlantDwellTime = 0.06f;
	UPROPERTY(EditAnywhere, Category="Contact", meta=(ClampMin="0.001"))
	float ContactBlendTime = 0.08f;
	// 손은 다음 봉으로 이동하는 축 속도를 본다. 접근 끝에는 정지 대기 없이 블렌드를 시작한다.
	UPROPERTY(EditAnywhere, Category="Contact", meta=(ClampMin="0"))
	float HandApproachAxisSpeed = 30.0f;
	// 고정 뒤 이 거리(cm)를 실제로 이동해야 축 속도에 의한 해제를 허용한다.
	UPROPERTY(EditAnywhere, Category="Contact", meta=(ClampMin="0"))
	float HandReleaseTravel = 6.0f;
	// UEFN 참조 자세의 ball 높이 약 3.14cm. 발볼 뼈를 봉 속에 넣지 않고 발바닥을 올린다.
	UPROPERTY(EditAnywhere, Category="Contact", meta=(ClampMin="0"))
	float FootSoleOffset = 3.14f;

	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms) override;

private:
	FBoneReference Ends[4];
	FBoneReference Contacts[4];
	FCompactPoseBoneIndex Middles[4];
	FCompactPoseBoneIndex Roots[4];
	FVBLadderIKContact States[4];
	FVBLadderIKGeometry Geometry;
	uint32 LadderId = 0;
	uint32 EvaluatedLadderId = 0;
	float Elapsed = 0.0f;
	float MaxCorrection = 0.0f;
	float FootMaxCorrection = 0.0f;
	FVector FootPoseOffsetWS = FVector::ZeroVector;
	float PoseBlendTime = 0.0f;
	float PoseAlpha = 0.0f;
	float HandWeight = 0.0f;
	FVector HandGripOffsets[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float FootWeight = 0.0f;
	bool bEnabled = false;
	// 진단 전용 스냅샷. 병렬 평가에서는 UObject에 접근하지 않는다.
	FString DiagnosticActor;
	FString DiagnosticMontage;
	float DiagnosticMontagePosition = 0.0f;
	float DiagnosticWorldTime = 0.0f;
	float DiagnosticElapsed = 0.0f;
	uint64 DiagnosticFrame = 0;
	uint64 DiagnosticEvaluation = 0;
};
