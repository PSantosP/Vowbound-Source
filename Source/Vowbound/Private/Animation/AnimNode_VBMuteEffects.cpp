#include "Animation/AnimNode_VBMuteEffects.h"

#include "Animation/AnimNotifyQueue.h"

IMPLEMENT_NOTIFY_CONTEXT_INTERFACE(FVBMutedEffectsNotifyContext)
IMPLEMENT_ANIMGRAPH_MESSAGE(FVBMuteEffectsScope)

TUniquePtr<const UE::Anim::IAnimNotifyEventContextDataInterface> FVBMuteEffectsScope::MakeUniqueEventContextData() const
{
	return MakeUnique<FVBMutedEffectsNotifyContext>();
}

bool VBAnimEffects::IsMuted(const FAnimNotifyEventReference& EventReference)
{
	return EventReference.GetContextData<FVBMutedEffectsNotifyContext>() != nullptr;
}

void FAnimNode_VBMuteEffects::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);
}

void FAnimNode_VBMuteEffects::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	Source.CacheBones(Context);
}

void FAnimNode_VBMuteEffects::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread)
	// 메시지는 이 범위 동안만 쌓인다. 아래 캐시 포즈는 지연 갱신 때 메시지 꼭대기를 복사해 간다(FMessageStack::CopyForCachedUpdate).
	UE::Anim::TScopedGraphMessage<FVBMuteEffectsScope> MuteEffects(Context);
	Source.Update(Context);
}

void FAnimNode_VBMuteEffects::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)
	Source.Evaluate(Output);
}

void FAnimNode_VBMuteEffects::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(DebugData.GetNodeName(this));
	Source.GatherDebugData(DebugData);
}
