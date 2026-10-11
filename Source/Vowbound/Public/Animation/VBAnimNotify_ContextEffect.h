// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayTagContainer.h"
#include "VBAnimNotify_ContextEffect.generated.h"

class UVBContextEffectsLibrary;

/**
 * UVBAnimNotify_ContextEffect
 * 애니의 그 프레임에 «무슨 순간인가(태그) + 어느 뼈인가» 만 적는 표시. 무엇을 틀지는 모른다 - 캐릭터의 UVBContextEffectComponent 표가 정한다.
 * design .sw/context-effects/design.md §3. 태그는 AnimEffect.*(DefaultGameplayTags.ini) - Effect.* 는 게임플레이 이펙트 몫이다.
 *
 * 왜 노티파이에 파티클을 직접 안 적나: 같은 클립을 여러 캐릭터가 쓰면 캐릭터마다 달라야 할 출력이 클립에 박힌다(설계 §2 의 1번을 버린 이유).
 * 시각은 이름이 아니라 애니에서 잰다 - 발 높이가 최저이고 속도가 0 인 프레임, 손이 땅에 닿는 프레임(설계 §6).
 */
UCLASS(meta=(DisplayName="VB Context Effect"))
class VOWBOUND_API UVBAnimNotify_ContextEffect : public UAnimNotify
{
	GENERATED_BODY()

public:
	// 이 순간의 이름(예: AnimEffect.Foot.Plant). 표의 행과 정확히 같아야 맞는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects", meta=(Categories="AnimEffect"))
	FGameplayTag EffectTag;

	// 위치를 줄 뼈(예: foot_r, hand_l). 비면 메시 위치.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	FName Bone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects", meta=(ClampMin="0.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects", meta=(ClampMin="0.01"))
	float PitchMultiplier = 1.0f;

	// 애님 에디터 미리보기용 표. 미리보기 액터에는 컴포넌트가 없어 이 표로 튼다 - 노티파이를 놓는 사람이 그 자리에서 보고 들으며 시각을 맞추게.
	//   게임에서는 캐릭터의 표를 쓰고 이것은 안 본다.
	UPROPERTY(EditAnywhere, Category="Vowbound|Effects")
	TObjectPtr<UVBContextEffectsLibrary> PreviewLibrary;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	// 타임라인에 태그 이름이 보이게 - «VB Context Effect» 여러 개가 구별되지 않으면 시각을 맞출 수 없다.
	virtual FString GetNotifyName_Implementation() const override;
};
