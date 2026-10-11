// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimMontage.h"
#include "VBAnimNotify_InteractionRagdoll.generated.h"

/** 피해자 몽타주의 저작된 물리 전환 시점을 현재 상호작용 세션에 알린다. */
UCLASS(meta=(DisplayName="VB Interaction Ragdoll"))
class VOWBOUND_API UVBAnimNotify_InteractionRagdoll : public UAnimNotify
{
	GENERATED_BODY()
public:
	UVBAnimNotify_InteractionRagdoll();
	virtual void BranchingPointNotify(FBranchingPointNotifyPayload& Payload) override;
	void StopReleasedMontage(UAnimInstance* Anim, UAnimMontage* Montage, int32 MontageInstanceId) const;

	// GASP notify 기본값. 중지 범위는 이 notify가 해제한 피해자 역할 몽타주 하나다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction")
	bool bStopActiveMontages = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction", meta=(EditCondition="bStopActiveMontages"))
	FMontageBlendSettings MontageBlendOut;
};
