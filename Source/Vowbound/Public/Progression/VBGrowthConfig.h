// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBGrowthConfig.generated.h"

class UGameplayEffect;
class UVBSkillTreeDefinition;

/**
 * 성장 설정 하나(전역 = 플랫 DataAsset, Config 데이터 구조 표준). 설계 .sw/remaining-features/b2-growth.md 1절 2·3번.
 * 레벨은 누적 경험치에서 이 표로 센다 - 레벨도 남은 포인트도 저장하지 않는다(주인이 하나).
 */
UCLASS(BlueprintType, Const)
class VOWBOUND_API UVBGrowthConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// 칸 i = 레벨 i+1 에서 i+2 로 가는 데 필요한 경험치. 최대 레벨 = 칸 수 + 1. 최대에서 넘친 경험치는 쌓기만 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Growth", meta=(ClampMin="1"))
	TArray<int32> XPToNextLevel;

	// 레벨이 하나 오를 때마다 받는 스킬 포인트.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Growth", meta=(ClampMin="1"))
	int32 PointsPerLevel = 1;

	// 주인공의 스킬 트리.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Growth")
	TObjectPtr<const UVBSkillTreeDefinition> SkillTree;

	// 연 칸의 능력치 합을 싣는 효과(무한, 능력치마다 SetByCaller 덧셈). 장비와 같은 GE 를 쓴다(설계 2절).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Growth")
	TSubclassOf<UGameplayEffect> StatBonusEffect;

	int32 GetMaxLevel() const { return XPToNextLevel.Num() + 1; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
