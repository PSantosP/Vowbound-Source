// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "VBANS_WeaponTrail.generated.h"

class UVBSwingTrailProfile;

/**
 * 휘두름 궤적 구간. 설계 .sw/weapon-trace 18절.
 *
 * 판정 창(UVBANS_AttackTraceWindow)과 따로 둔다 - 궤적은 «빠르게 휘두르는 구간» 을 덮고 판정 창은 «닿는 구간» 이라 시각이 다르다.
 * 치장이라 모든 머신에서 돈다(판정 창과 달리 서버 권위가 없다). 궤적 상태는 소유 액터의 UVBSwingTrailComponent 가 든다.
 */
UCLASS(DisplayName = "Weapon Trail")
class VOWBOUND_API UVBANS_WeaponTrail : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UVBANS_WeaponTrail();

	// 어느 날을 따라 그릴지. 판정 창과 같은 태그 - 모양은 공격자의 UVBMeleeTraceComponent 가 든다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(Categories="Melee.Shape"))
	FGameplayTag Shape;

	// 이 스윙만의 궤적. 비우면 모양의 기본 궤적(FVBMeleeTraceShapeDef::TrailProfile).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBSwingTrailProfile> Profile;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	                         const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                       const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
