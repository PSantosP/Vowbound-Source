// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAnimNotifyState_InteractionWarp.h"

#include "Animation/VBInteractionAnimInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "MotionWarpingComponent.h"
#include "MotionWarpingAdapter.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "PoseSearch/MultiAnimAsset.h"
#include "RootMotionModifier.h"
#include "Vowbound/Vowbound.h"        // VB_LOG

URootMotionModifier* UVBAnimNotifyState_InteractionWarp::AddRootMotionModifier_Implementation(
	UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation,
	float StartTime, float EndTime) const
{
	URootMotionModifier* Modifier = Super::AddRootMotionModifier_Implementation(
		MotionWarpingComp, Animation, StartTime, EndTime);

	if (!Modifier)
	{
		return nullptr;
	}

	// 부모가 뒤이어 자기 것을 걸려고 하는데 «이미 걸려 있으면 건너뛴다». 그래서 여기서 먼저 건다.
	// const_cast 는 엔진이 같은 자리에서 하는 것과 같다(AnimNotifyState_MotionWarping.cpp::OnBecomeRelevant) -
	//   노티파이는 CDO 라 const 로 다뤄지는데 델리게이트 대상은 non-const 를 요구한다.
	Modifier->OnActivateDelegate.BindDynamic(
		const_cast<UVBAnimNotifyState_InteractionWarp*>(this),
		&UVBAnimNotifyState_InteractionWarp::HandleWarpActivate);
	Modifier->OnDeactivateDelegate.BindDynamic(
		const_cast<UVBAnimNotifyState_InteractionWarp*>(this),
		&UVBAnimNotifyState_InteractionWarp::HandleWarpDeactivate);

	return Modifier;
}

void UVBAnimNotifyState_InteractionWarp::HandleWarpActivate(UMotionWarpingComponent* MotionWarpingComp,
                                                            URootMotionModifier*     Modifier)
{
	if (!MotionWarpingComp || !Modifier)
	{
		return;
	}

	// 목표 이름은 모디파이어가 든다. 여기서 문자열을 또 적으면 둘이 어긋나는 날 조용히 안 걸린다.
	URootMotionModifier_Warp* WarpModifier = Cast<URootMotionModifier_Warp>(Modifier);
	if (!WarpModifier || WarpModifier->WarpTargetName.IsNone())
	{
		VB_LOG(Warning, "InteractionWarp: 모디파이어가 워프형이 아니거나 WarpTargetName 이 비었다 (%s). "
		                "노티파이의 RootMotionModifier 설정을 확인하라", *GetNameSafe(Modifier));
		return;
	}

	// 몸 메시를 이름으로 집는다. FindComponentByClass 는 «처음 찾은» 것을 주므로 BP 가 무기 메시를
	//   하나라도 더 붙이면 그쪽이 걸릴 수 있다(컴포넌트 쪽 FindBodyMesh 와 같은 이유).
	const AActor* Owner = MotionWarpingComp->GetOwner();
	const ACharacter* Character = Cast<ACharacter>(Owner);
	USkeletalMeshComponent* Mesh = Character
		                               ? Character->GetMesh()
		                               : (Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr);
	const IVBInteractionAnimInterface* AnimInstance =
		Mesh ? Cast<IVBInteractionAnimInterface>(Mesh->GetAnimInstance()) : nullptr;
	if (!AnimInstance)
	{
		// 이 창이 붙은 애니를 IVBInteractionAnimInterface 를 구현하지 않은 AnimInstance 가 재생했다.
		//   설정 오류이므로 말한다 - 증상이 「정렬만 안 된다」라서 조용하면 애니 쪽을 의심하게 된다(규칙 27).
		VB_LOG(Warning, "InteractionWarp: 상호작용 AnimInstance 를 못 찾았다 (owner=%s)", *GetNameSafe(Owner));
		return;
	}

	const FPoseSearchBlueprintResult& Result = AnimInstance->GetInteractionResult();
	if (!Result.bIsInteraction || !Result.SelectedAnim)
	{
		// 상호작용으로 시작된 재생이 아니다. 목표를 안 넣으면 워프가 그냥 아무것도 안 한다 -
		//   막을 수 있는 자리가 아니라 정상 경로다(같은 클립을 단독 재생할 수 있다).
		return;
	}

	// 이 창이 «끝날 때» 있어야 할 자리를 묻는다. 남은 시간이 곧 앞으로 휘어야 할 양이다.
	// GASP 가 쓰는 식 그대로다(BP_NotifyState_MMI_MotionWarping::OnWarpBegin 실측 2026-09-19).
	const float TimeOffset = WarpModifier->EndTime - Result.SelectedTime;

	FTransform AlignedXf = FTransform::Identity;
	UPoseSearchInteractionLibrary::CalculateFullAlignedTransform(
		Result, TimeOffset, /*bWarpUsingRootBone=*/true, AlignedXf);
	if (!AlignmentAnchorRole.IsNone())
	{
		const UMultiAnimAsset* Asset = Cast<UMultiAnimAsset>(Result.SelectedAnim);
		int32 AnchorIndex = INDEX_NONE;
		if (Asset)
			for (int32 Index = 0; Index < Asset->GetNumRoles(); ++Index)
				if (Asset->GetRole(Index) == AlignmentAnchorRole) { AnchorIndex = Index; break; }
		if (!Result.ActorRootTransforms.IsValidIndex(AnchorIndex) ||
			!Result.ActorRootBoneTransforms.IsValidIndex(AnchorIndex) || !Result.AnimContexts.IsValidIndex(AnchorIndex))
		{
			VB_LOG(Warning, "InteractionWarp: invalid alignment anchor %s in %s", *AlignmentAnchorRole.ToString(), *GetNameSafe(Asset));
			return;
		}
		// Rebase the entire engine-aligned scene onto the anchor's original pose.
		// Sample the anchor at offset zero so its later authored root motion remains.
		// This avoids unsupported 1/0 rotation weights and never moves actors directly.
		FPoseSearchBlueprintResult AnchorResult = Result;
		AnchorResult.Role = AlignmentAnchorRole;
		AnchorResult.RoleIndex = AnchorIndex;
		FTransform AlignedAnchor;
		UPoseSearchInteractionLibrary::CalculateFullAlignedTransform(AnchorResult, 0.0f, true, AlignedAnchor);
		const FTransform OriginalAnchor = Result.ActorRootBoneTransforms[AnchorIndex] * Result.ActorRootTransforms[AnchorIndex];
		AlignedXf = AlignedXf.GetRelativeTransform(AlignedAnchor) * OriginalAnchor;
	}

	// 위치와 회전이 서로 다른 공간이다. 엔진 모디파이어가 그 둘을 다르게 읽기 때문이다 -
	//   위치: `GetVisualRootLocation()`(메시 원점 = 발 높이)와 비교하므로 «그대로» 넘긴다.
	//     GASP 도 손대지 않는다. 캡슐 중심으로 올리면 90cm 떠오른다.
	//   회전: `GetTargetRotation() * GetBaseVisualRotationOffset()` 로 메시 오프셋을 엔진이 «다시»
	//     곱하므로(RootMotionModifier.cpp::URootMotionModifier_Warp::WarpRotation) 여기서는
	//     액터 공간으로 되돌려 줘야 한다.
	// GASP 는 이 되돌림을 상수 +90 yaw 로 적었는데, 우리는 메시의 상대 변환에서 재서 쓴다.
	//   메시 상대 회전이 -90 yaw 인 관례가 깨지는 날 상수는 조용히 틀리고 이 식은 따라온다.
	const FTransform DesiredActorXf = Mesh->GetRelativeTransform().Inverse() * AlignedXf;

	MotionWarpingComp->AddOrUpdateWarpTargetFromLocationAndRotation(
		WarpModifier->WarpTargetName, AlignedXf.GetLocation(), DesiredActorXf.Rotator());

	VB_LOG(Log, "InteractionWarp: %s 워프 목표 설정 (역할=%s, 남은=%.2fs, 위치=%s, yaw=%.1f, anchor=%s)",
	       *GetNameSafe(Owner), *Result.Role.ToString(), TimeOffset, *AlignedXf.GetLocation().ToCompactString(),
	       DesiredActorXf.Rotator().Yaw, *AlignmentAnchorRole.ToString());
}

void UVBAnimNotifyState_InteractionWarp::HandleWarpDeactivate(UMotionWarpingComponent* MotionWarpingComp,
	URootMotionModifier* Modifier)
{
	const URootMotionModifier_Warp* Warp = Cast<URootMotionModifier_Warp>(Modifier);
	const UMotionWarpingBaseAdapter* Adapter = MotionWarpingComp ? MotionWarpingComp->GetOwnerAdapter() : nullptr;
	const FMotionWarpingTarget* Target = Warp && MotionWarpingComp ? MotionWarpingComp->FindWarpTarget(Warp->WarpTargetName) : nullptr;
	if (!Warp || !Adapter || !Target) return;

	// Deactivation happens before this update's movement. At a normal window end,
	// PreviousPosition describes the pose already applied, including any overshoot
	// past EndTime. Record both times rather than call this an exact endpoint error.
	const FTransform TargetTransform = Target->GetTargetTrasform();
	const FVector ActualLocation = Adapter->GetVisualRootLocation();
	const float ActualYaw = Adapter->GetCurrentRotation().Rotator().Yaw;
	VB_LOG(Log, "InteractionWarp end: actor=%s montage=%s reachedWindowEnd=%d sample=%.3f end=%.3f distance2D=%.2fcm yawError=%.2fdeg actual=%s target=%s",
		*GetNameSafe(MotionWarpingComp->GetOwner()), *GetPathNameSafe(Warp->GetAnimation()),
		Warp->PreviousPosition >= Warp->EndTime, Warp->PreviousPosition, Warp->EndTime,
		FVector::Dist2D(ActualLocation, TargetTransform.GetLocation()),
		FMath::Abs(FMath::FindDeltaAngleDegrees(ActualYaw, TargetTransform.Rotator().Yaw)),
		*ActualLocation.ToCompactString(), *TargetTransform.GetLocation().ToCompactString());
}
