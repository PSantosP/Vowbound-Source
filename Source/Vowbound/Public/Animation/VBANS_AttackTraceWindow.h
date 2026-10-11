// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "VBANS_AttackTraceWindow.generated.h"

class UVBSwingImpactProfile;
class UVBSwingCombatProfile;

/**
 * 무기 궤적 판정 창. 스윙 하나 = 창 하나. 설계 .sw/weapon-trace 2절.
 *
 * 시작에서 Event.Montage.AttackTrace 를, 끝에서 Event.Montage.AttackTraceEnd 를 보낸다.
 *   시작 태그를 옛 한 프레임 노티(UVBAN_SendAttackTraceEvent)와 같게 둔 이유: 서버·클라 양쪽이 그 태그를 «닿는 프레임» 신호로 쓴다
 *   (콤보 조기 전환·회피 오프셋·ChainDelay). 창 시작이 곧 그 약속이다. 옛 노티와는 InstigatorTags 의 Melee.Window 로 가른다.
 * 창 동안의 스윕은 서버의 공격 GA 가 UVBMeleeTraceComponent 로 한다. 이 노티는 알리기만 한다.
 */
UCLASS(DisplayName = "Attack Trace Window")
class VOWBOUND_API UVBANS_AttackTraceWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UVBANS_AttackTraceWindow();

	// 어느 날로 쓸지. 모양은 공격자의 UVBMeleeTraceComponent 가 든다 - 같은 몽타주를 다른 무기가 써도 그 무기의 날로 맞는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(Categories="Melee.Shape"))
	FGameplayTag Shape;

	// 창 안에서 같은 표적을 다시 맞히는 간격(초, 애니 시간). 0 = 표적당 한 번(기본).
	//   0 보다 크면 반응 등급 이벤트가 타격마다 간다 - 반응 쪽(vowbound-5e)에 빈도를 알린 뒤 쓴다(설계 6절).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0"))
	float RehitInterval = 0.0f;

	// 옛 노티와 같은 두 프로필(스윙 1:1 저작). 비우면 공격 leaf 로 폴백한다 - 옛 노티와 같은 규칙.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBSwingImpactProfile> SwingImpactProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBSwingCombatProfile> SwingCombatProfile;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	                         const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                       const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
