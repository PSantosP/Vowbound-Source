#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNodeMessages.h"
#include "AnimNode_VBMuteEffects.generated.h"

struct FAnimNotifyEventReference;

// 숨은 갈래(화면에 안 나가지만 상태를 이어 가려고 갱신만 하는 포즈)에서 나온 노티파이에 붙는 표시.
// 엔진은 «비활성» 표시(bIsActive)를 링크 그래프 경계에서 잃는다(UE5.8 FAnimationUpdateContext 복사 생성자) -
// 그래프 메시지 -> 노티파이 문맥 데이터 길은 링크 그래프와 캐시 포즈를 건너 살아남는다(FMirrorSyncScope 와 같은 길).
// 내보내기 표시는 엔진과 같이 매크로(형 이름)와 함수에만 붙인다 - 클래스에도 붙이면 형 이름 멤버와 겹쳐 C2487.
class FVBMutedEffectsNotifyContext : public UE::Anim::IAnimNotifyEventContextDataInterface
{
	DECLARE_NOTIFY_CONTEXT_INTERFACE_API(FVBMutedEffectsNotifyContext, VOWBOUND_API)
};

class FVBMuteEffectsScope : public UE::Anim::IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE_API(FVBMuteEffectsScope, VOWBOUND_API);
public:
	VOWBOUND_API virtual TUniquePtr<const UE::Anim::IAnimNotifyEventContextDataInterface> MakeUniqueEventContextData() const override;
};

namespace VBAnimEffects
{
	// 소리·화면 흔들림처럼 «보이는 포즈» 에서만 나야 하는 노티파이가 부른다. 상태를 바꾸는 노티파이(전이 신호 등)는 부르지 않는다 -
	// 숨은 갈래의 상태 머신은 그 신호로 진행해야 한다.
	VOWBOUND_API bool IsMuted(const FAnimNotifyEventReference& EventReference);
}

// 통과 노드. 이 노드 아래(링크 그래프 안까지)에서 갱신된 애셋의 노티파이에 FVBMutedEffectsNotifyContext 를 붙인다.
// 쓰는 곳: ABP_VBCharacter_58 의 «본 이동 그래프 유지» 갈래(슬라이드 중 숨은 채 갱신만 하는 쪽, 설계 .sw/slide-sprint-resume/fix-design-claude.md 5차).
USTRUCT(BlueprintInternalUseOnly)
struct VOWBOUND_API FAnimNode_VBMuteEffects : public FAnimNode_Base
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Links)
	FPoseLink Source;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
};
