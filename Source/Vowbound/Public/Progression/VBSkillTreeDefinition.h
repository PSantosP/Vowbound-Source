// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBSkillTreeDefinition.generated.h"

class UGameplayAbility;
class UTexture2D;

/**
 * 스킬 트리의 칸 하나. 설계 .sw/remaining-features/b2-growth.md 1절 4번.
 * 칸이 하는 일은 둘 중 하나 또는 둘 다 - 능력치 덧셈(Stats, 장비와 같은 SetByCaller 길) / 능력 해금(GrantedAbility).
 */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBSkillNode
{
	GENERATED_BODY()

	// 칸의 이름(저장·선행 조건·사건 대상이 이 태그로 부른다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth", meta=(Categories="Skill.Node"))
	FGameplayTag NodeId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth", meta=(MultiLine="true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth")
	TSoftObjectPtr<UTexture2D> Icon;

	// 여는 데 드는 스킬 포인트(1 이상).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth", meta=(ClampMin="1"))
	int32 Cost = 1;

	// 이 레벨 이상이어야 연다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth", meta=(ClampMin="1"))
	int32 RequiredLevel = 1;

	// 전부 열려 있어야 연다(같은 트리의 NodeId).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth", meta=(Categories="Skill.Node"))
	TArray<FGameplayTag> Prerequisites;

	// 능력치 태그 -> 덧셈값. 연 칸의 값을 모두 더해 성장 효과 하나로 건다(UVBGrowthConfig::StatBonusEffect 의 SetByCaller).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth", meta=(Categories="Data.Stat"))
	TMap<FGameplayTag, float> Stats;

	// 열면 주는 능력. 시작 능력(PlayerState DefaultAbilities)에도 있으면 주인이 둘이 된다 - 성장 컴포넌트가 경고한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth")
	TSubclassOf<UGameplayAbility> GrantedAbility;

	// 스킬 화면의 자리(캔버스 좌표, 칸 중심).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth")
	FVector2D LayoutPosition = FVector2D::ZeroVector;
};

/**
 * 스킬 트리 하나(주인공 Kael 의 것). 칸 목록이 전부다 - 칸을 더하거나 숫자를 바꾸는 데 코드가 필요 없다.
 * 판정(열 수 있나)은 VBGrowth::CanUnlock, 칸이 하는 일의 적용은 UVBProgressionComponent.
 */
UCLASS(BlueprintType, Const)
class VOWBOUND_API UVBSkillTreeDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Growth", meta=(TitleProperty="NodeId"))
	TArray<FVBSkillNode> Nodes;

	const FVBSkillNode* FindNode(const FGameplayTag& NodeId) const;

	// 데이터 문제 목록(비면 정상). IsDataValid 와 단위 시험이 같은 것을 본다:
	//   빈 NodeId, 겹친 NodeId, 트리에 없는 선행, 선행 고리, Cost < 1, RequiredLevel < 1, 아무 일도 안 하는 칸(Stats 비고 능력 없음).
	static TArray<FText> FindProblems(const UVBSkillTreeDefinition& Tree);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
