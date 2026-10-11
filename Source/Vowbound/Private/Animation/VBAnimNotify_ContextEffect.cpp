// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAnimNotify_ContextEffect.h"

#include "Animation/AnimNode_VBMuteEffects.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Character/VBContextEffectComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBContextEffectsLibrary.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"

void UVBAnimNotify_ContextEffect::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                         const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	// 빠져나가는 중인 애니의 표시는 안 튼다 - 다음 동작의 발걸음과 겹쳐 두 번 울린다(옛 Foley 노티파이와 같은 규칙).
	//   숨은 갈래(화면에 안 나가고 갱신만 하는 포즈, FAnimNode_VBMuteEffects 아래)의 표시도 같은 이유로 안 튼다.
	if (!IsValid(MeshComp) || !EffectTag.IsValid() || UAnimNotifyLibrary::IsBlendingOut(EventReference)
		|| VBAnimEffects::IsMuted(EventReference))
	{
		return;
	}
	AActor* Owner = MeshComp->GetOwner();
	if (UVBContextEffectComponent* Effects = Owner ? Owner->FindComponentByClass<UVBContextEffectComponent>() : nullptr)
	{
		Effects->PlayEffect(EffectTag, MeshComp, Bone, VolumeMultiplier, PitchMultiplier);
		return;
	}
	// 컴포넌트가 없다 = 애님 에디터 미리보기 같은 액터. 미리보기 표가 있으면 같은 트는 함수로 튼다(배율 1, 상황 없음).
	//   바닥 트레이스 값은 컴포넌트 기본값을 읽는다 - 숫자를 여기 다시 적으면 두 곳이 된다.
	if (PreviewLibrary)
	{
		if (const FVBContextEffectRow* Row = PreviewLibrary->FindRow(EffectTag, FGameplayTagContainer()))
		{
			const UVBContextEffectComponent* Defaults = GetDefault<UVBContextEffectComponent>();
			UVBContextEffectComponent::PlayRow(*Row, MeshComp, Bone, 1.0f, VolumeMultiplier, PitchMultiplier,
			                                   Defaults->GroundTraceChannel, Defaults->GroundTraceDistance);
		}
	}
}

FString UVBAnimNotify_ContextEffect::GetNotifyName_Implementation() const
{
	return EffectTag.IsValid() ? EffectTag.ToString() : Super::GetNotifyName_Implementation();
}
