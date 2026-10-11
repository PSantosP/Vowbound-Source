// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNotifyState_MotionWarping.h"
#include "VBAnimNotifyState_InteractionWarp.generated.h"

class UMotionWarpingComponent;
class URootMotionModifier;

/**
 * 처형 상호작용의 «두 몸 맞춤» 을 모션워핑 목표로 내려놓는 노티파이 스테이트.
 *
 * 무엇: 이 창이 열리는 순간 「이 애니가 끝날 때 내 몸이 어디 있어야 하는가」를 엔진에 묻고,
 *   그 자리를 워프 목표로 넣는다. 그 뒤의 이동은 모션워핑이 루트모션을 휘어서 만든다.
 *
 * 왜 여기인가(정렬의 주인이 왜 컴포넌트가 아닌가): GASP 5.8 이 그렇게 만들어져 있다.
 *   2026-09-19 실측 - `BP_NotifyState_MMI_MotionWarping::OnWarpBegin` 이
 *   `Get_MMIResult` -> `CalculateFullAlignedTransform(Result, EndTime - SelectedTime, bWarpUsingRootBone=true)`
 *   -> `AddOrUpdateWarpTargetFromLocationAndRotation` 세 걸음을 한다. 이 클래스는 그 셋의 C++ 판이다.
 *   액터를 코드가 직접 옮기면 루트모션·모션워핑과 «미는 주인» 이 둘이 된다(규칙 26).
 *
 * 왜 C++ 로 다시 쓰나: 부모의 OnWarpBegin/OnWarpUpdate 는 BlueprintImplementableEvent 라 C++ 가
 *   재정의할 수 없다. 대신 부모가 모디파이어에 델리게이트를 걸 때 이미 걸려 있으면 건너뛰므로
 *   (엔진 AnimNotifyState_MotionWarping.cpp::OnBecomeRelevant 의 `if (!IsBound())`),
 *   AddRootMotionModifier 를 재정의해 «먼저» 우리 것을 걸면 우리 쪽이 이긴다.
 *   그래서 엔진을 고치지 않고도 BP 없이 같은 일을 한다.
 *
 * 전제: 이 창이 붙은 애니를 재생하는 액터의 AnimInstance 가 IVBInteractionAnimInterface 를 구현하고,
 *   그 안에 이번 상호작용 결과가 들어 있어야 한다(UVBFinisherComponent 가 시작할 때 넣는다).
 *   비어 있으면 아무 목표도 안 넣는다 - 워프가 없는 평범한 재생이 된다.
 */
UCLASS(meta = (DisplayName = "VB Interaction Warp"))
class VOWBOUND_API UVBAnimNotifyState_InteractionWarp : public UAnimNotifyState_MotionWarping
{
	GENERATED_BODY()

public:
	/** Optional project policy: keep this role's search-time pose as the scene origin.
	 * None preserves GASP's shared alignment. Generated weapon clips use Attacker. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	FName AlignmentAnchorRole = NAME_None;

	/**
	 * 부모가 만든 모디파이어에 «우리» 콜백을 먼저 건다.
	 *
	 * 부모(OnBecomeRelevant)는 이 함수가 돌아온 뒤에 자기 콜백을 걸려고 시도하는데, 이미 걸려 있으면
	 * 건너뛴다. 즉 여기서 거는 것이 BP 이벤트를 «대체» 한다 - 둘이 같이 돌지 않는다.
	 */
	virtual URootMotionModifier* AddRootMotionModifier_Implementation(
		UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation,
		float StartTime, float EndTime) const override;

private:
	/** 창이 열렸다. 여기서 한 번 목표를 계산해 넣는다. */
	UFUNCTION()
	void HandleWarpActivate(UMotionWarpingComponent* MotionWarpingComp, URootMotionModifier* Modifier);

	/** Observe the completed movement on the next root-motion update; never move the actor here. */
	UFUNCTION()
	void HandleWarpDeactivate(UMotionWarpingComponent* MotionWarpingComp, URootMotionModifier* Modifier);
};
