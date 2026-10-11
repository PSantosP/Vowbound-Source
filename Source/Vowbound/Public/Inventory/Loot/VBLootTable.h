// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StructUtils/InstancedStruct.h"
#include "VBLootTable.generated.h"

class UVBItemDefinition;

/** 보상표의 한 줄. 줄마다 따로 굴린다 - «희귀 5% / 전설 1%» 는 줄 둘이다. 설계 .sw/narrative 14.2절. */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBLootEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Loot")
	TObjectPtr<const UVBItemDefinition> Item;

	// 나오면 이 범위에서 개수를 고른다(양끝 포함).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Loot", meta=(ClampMin="1"))
	int32 MinCount = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Loot", meta=(ClampMin="1"))
	int32 MaxCount = 1;

	// 나올 확률. 1 = 늘. 0 은 저작 실수로 본다(IsDataValid) - 안 나오게 하려면 줄을 지운다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Loot", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Chance = 1.0f;

	// 전부 참일 때만 굴린다(예: 그 퀘스트를 진행 중일 때만 나오는 열쇠). 비면 늘 굴린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Loot", meta=(BaseStruct="/Script/Vowbound.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;
};

/**
 * 보상표 - «무엇이 나오나». 설계 .sw/narrative 14.2절.
 *
 * 상자(상호작용 대상의 완료 효과)와 적(적 설정의 처치 효과)이 «보상표 주기» 효과(FVBEffect_GiveLoot)로 쓴다. 표는 누가 쓰는지 모른다.
 * 자산인 이유: 같은 병사 스무 명·같은 찬장 열 개가 한 벌을 나눠 쓰고, 값을 고치는 데 빌드가 필요 없다.
 * UPrimaryDataAsset 이 아닌 이유(reuse-catalog §6): 저장이 표를 되찾지 않는다 - 굴린 결과는 인벤토리에 들어가고 표는 남지 않는다.
 * 굴리기는 VBLoot::Roll 하나다(VBLootRules.h).
 */
UCLASS(BlueprintType, Const)
class VOWBOUND_API UVBLootTable : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Loot")
	TArray<FVBLootEntry> Entries;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
