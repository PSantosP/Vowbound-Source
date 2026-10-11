// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBInteractionIK.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/PoseSearchInteractionAsset.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Engine/SkeletalMesh.h"
#include "Vowbound/Vowbound.h"        // VB_LOG

void UVBInteractionIKLibrary::Update(UAnimInstance*                     AnimInstance,
                                     const FPoseSearchBlueprintResult&  Result,
                                     float                              DeltaSeconds,
                                     FVBInteractionIKRuntime&           InOutRuntime)
{
	// 꺼진 상태로 시작한다. 아래 조건을 다 통과해야 켜진다 - 중간에 빠져나가는 길이 여럿이라
	//   「켜고 나서 끄기」로 쓰면 한 갈래를 빠뜨리는 날 IK 가 계속 걸린 채로 남는다.
	InOutRuntime.bEnabled = false;
	InOutRuntime.Left     = FVBInteractionIKHand();
	InOutRuntime.Right    = FVBInteractionIKHand();

	const UPoseSearchInteractionAsset* InteractionAsset =
		AnimInstance ? Cast<UPoseSearchInteractionAsset>(Result.SelectedAnim) : nullptr;
	if (!InteractionAsset)
	{
		InOutRuntime.Constraints.Reset();
		return;
	}

	// 내 역할의 클립이 «지금 실제로 돌고 있는가». 결과가 남아 있어도 몽타주가 끝났으면 IK 는 끝이다.
	//   GASP 가 같은 자리에서 같은 것을 묻는다 - 결과의 수명과 재생의 수명이 다르기 때문이다.
	UAnimMontage* RoleMontage = Cast<UAnimMontage>(InteractionAsset->GetAnimationAsset(Result.Role));
	if (!RoleMontage || !AnimInstance->Montage_IsActive(RoleMontage))
	{
		InOutRuntime.Constraints.Reset();
		return;
	}

	InOutRuntime.bEnabled = true;

	// 시간은 몽타주에서 읽는다. 결과의 SelectedTime 은 «검색이 고른 시각» 이라 재생이 흐른 만큼
	//   낡는다 - 제약의 램프업이 그 낡은 시각을 보면 손이 늦게 붙는다.
	const float MontageTime = AnimInstance->Montage_GetPosition(RoleMontage);

	// 엔진 API 가 non-const 배열을 요구한다. 결과 쪽은 const 라 여기서 한 번 푼다.
	TArray<UObject*> AnimContexts;
	AnimContexts.Reserve(Result.AnimContexts.Num());
	for (const TObjectPtr<const UObject>& Context : Result.AnimContexts)
	{
		AnimContexts.Add(const_cast<UObject*>(Context.Get()));
	}

	// 참가자 전원의 골격이 스키마 선언과 같은지 다시 본다. 짝을 지을 때 이미 걸렀지만,
	//   여기서 빠뜨리면 «크래시» 로 끝나므로 같은 질문을 한 번 더 한다(비용은 포인터 비교 둘).
	//   왜 필요한지는 DoesSkeletonMatchSchema 주석 - 엔진이 안 막는 자리다.
	for (int32 RoleIndex = 0; RoleIndex < InteractionAsset->GetNumRoles(); ++RoleIndex)
	{
		const FName RoleName = InteractionAsset->GetRole(RoleIndex);
		const UAnimInstance* RoleAnim = AnimContexts.IsValidIndex(RoleIndex)
			                                ? Cast<UAnimInstance>(AnimContexts[RoleIndex])
			                                : nullptr;
		if (!DoesSkeletonMatchSchema(Result.SelectedDatabase, RoleName, RoleAnim))
		{
			VB_LOG(Warning, "InteractionIK: 역할 %s 의 골격이 스키마와 다르다 - 제약 갱신을 건너뛴다 "
			                "(그대로 부르면 엔진이 뼈 인덱스로 크래시한다)", *RoleName.ToString());
			InOutRuntime.Constraints.Reset();
			InOutRuntime.bEnabled = false;
			return;
		}
	}

	UPoseSearchInteractionLibrary::UpdateConstraints(
		InOutRuntime.Constraints, DeltaSeconds, InteractionAsset, AnimContexts,
		MontageTime, Result.bIsMirrored, Result.BlendParameters);

	const USkeletalMeshComponent* Mesh = AnimInstance->GetOwningComponent();
	if (!Mesh)
	{
		return;
	}
	const FTransform MeshToWorld = Mesh->GetComponentTransform();

	auto FillHand = [&InOutRuntime, &Result, &MeshToWorld](FName Socket, FVBInteractionIKHand& OutHand)
	{
		float      DesiredReach = 0.0f;
		FTransform WorldTarget  = FTransform::Identity;
		bool       bValid       = false;

		UPoseSearchInteractionLibrary::GetConstraint(
			InOutRuntime.Constraints, Result.Role, Socket, DesiredReach, WorldTarget, bValid);

		if (!bValid)
		{
			// 이 소켓에는 제약이 없다. 정상이다 - 한 손만 잡는 처형이 있다.
			return;
		}

		// 엔진이 주는 것은 월드 변환이고 AnimGraph 의 IK 는 컴포넌트 공간에서 일한다.
		//   GASP 도 같은 자리에서 Make Relative Transform 으로 같은 환산을 한다.
		OutHand.Target = WorldTarget.GetRelativeTransform(MeshToWorld);
		OutHand.Alpha  = FMath::Clamp(DesiredReach, 0.0f, 1.0f);
	};

	FillHand(InOutRuntime.LeftSocket,  InOutRuntime.Left);
	FillHand(InOutRuntime.RightSocket, InOutRuntime.Right);
}

bool UVBInteractionIKLibrary::DoesSkeletonMatchSchema(const UPoseSearchDatabase* Database,
                                                      FName                      Role,
                                                      const UAnimInstance*       AnimInstance)
{
	if (!Database || !Database->Schema || !AnimInstance)
	{
		return false;
	}

	const FPoseSearchRoledSkeleton* RoledSkeleton = Database->Schema->GetRoledSkeleton(Role);
	if (!RoledSkeleton || !RoledSkeleton->Skeleton)
	{
		// 스키마가 그 역할을 모른다. 역할 이름 오타이거나 다른 용도의 DB 다.
		return false;
	}

	// 골격은 메시에서 읽는다. AnimInstance 의 것을 직접 보지 않는 이유는 그 필드가 보호돼 있어
	//   접근 경로가 버전마다 다르기 때문이다 - 메시 -> 자산 -> 골격은 안 바뀐다.
	const USkeletalMeshComponent* Mesh = AnimInstance->GetOwningComponent();
	const USkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* Skeleton = MeshAsset ? MeshAsset->GetSkeleton() : nullptr;

	// 모르면 false. 아는 경우만 통과시킨다(규칙 27).
	return Skeleton != nullptr && Skeleton == RoledSkeleton->Skeleton;
}
