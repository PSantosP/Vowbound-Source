// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#include "Animation/VBAnimNotify_InteractionRagdoll.h"
#include "Character/VBFinisherComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

UVBAnimNotify_InteractionRagdoll::UVBAnimNotify_InteractionRagdoll()
{
	// 몽타주 진행 중 저작 시각에서 호출한다. 별도 타이머나 프레임 뒤의 queued notify가 아니다.
	bIsNativeBranchingPoint = true;
	MontageBlendOut.Blend.BlendTime = 0.3f;
	MontageBlendOut.Blend.BlendOption = EAlphaBlendOption::HermiteCubic;
	MontageBlendOut.BlendMode = EMontageBlendMode::Inertialization;
}

void UVBAnimNotify_InteractionRagdoll::BranchingPointNotify(FBranchingPointNotifyPayload& Payload)
{
	const TWeakObjectPtr<UAnimInstance> Anim = Payload.SkelMeshComponent ? Payload.SkelMeshComponent->GetAnimInstance() : nullptr;
	UAnimMontage* Montage = Cast<UAnimMontage>(Payload.SequenceAsset);
	// 현재 포즈의 발사 속도/피해/물리 전환을 먼저 처리한다. 중지하면 그 입력을 잃을 수 있다.
	const bool bAccepted = UVBFinisherComponent::NotifyInteractionRagdoll(
		Payload.SkelMeshComponent, Payload.SequenceAsset, Payload.MontageInstanceID);
	if (bAccepted && Anim.IsValid())
	{
		StopReleasedMontage(Anim.Get(), Montage, Payload.MontageInstanceID);
	}
}

void UVBAnimNotify_InteractionRagdoll::StopReleasedMontage(UAnimInstance* Anim, UAnimMontage* Montage,
                                                         int32 MontageInstanceId) const
{
	if (bStopActiveMontages && Anim && Montage)
	{
		// 피해 콜백이 파괴/다른 재생을 일으켰을 수 있으므로, 같은 몽타주 이름만으로 중지하지 않는다.
		const FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage);
		if (Instance && Instance->GetInstanceID() == MontageInstanceId)
		{
			Anim->Montage_StopWithBlendSettings(MontageBlendOut, Montage);
		}
	}
}
