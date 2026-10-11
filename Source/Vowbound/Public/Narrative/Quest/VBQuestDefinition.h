// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBQuestDefinition.generated.h"

struct FVBGameEvent;
struct FVBRuleContext;

/**
 * 퀘스트 목표의 바탕. 설계 .sw/narrative 3.3절·12.1절.
 * 조건·효과처럼 InstancedStruct 다형이다 - 새 목표 종류 = 파생 구조체 하나. 바탕형은 아무것도 세지 않는다.
 * 목표는 두 모양이다: 사건을 세는 것(처치·줍기·말하기)과 지금 상태로 판정하는 것(«거즈 5개 갖기»).
 */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBQuestObjective
{
	GENERATED_BODY()

	virtual ~FVBQuestObjective() = default;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(MultiLine=true))
	FText Description;

	// 선택 목표 - 단계 완료에 필요 없다(일지에 보이기만).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest")
	bool bOptional = false;

	// 채워야 하는 수. 상태 목표는 1(참이면 다 찬 것).
	virtual int32 GetRequiredCount() const { return 1; }

	// 사건 하나가 이 목표를 몇 칸 채우나. 0 = 이 목표와 상관없는 사건.
	virtual int32 CountEvent(const FVBGameEvent& Event) const { return 0; }

	// 지금 상태로 판정하는 목표인가. 참이면 진행 = Evaluate 의 결과(세지 않는다).
	virtual bool IsStateBased() const { return false; }
	virtual bool Evaluate(const FVBRuleContext& Context) const { return false; }
};

/** 사건을 센다. 사건 태그가 계층으로 맞고(Event.Enemy 는 Event.Enemy.Killed 를 품는다) 대상 태그가 질의를 만족하면 사건의 수만큼. */
USTRUCT(BlueprintType, meta=(DisplayName="사건 세기"))
struct VOWBOUND_API FVBQuestObjective_Event : public FVBQuestObjective
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(Categories="Event"))
	FGameplayTag EventTag;

	// 사건의 대상 태그(죽은 적의 종류·주운 아이템·대사 노드의 표지)에 거는 질의. 비면 아무 대상이나.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest")
	FGameplayTagQuery TargetQuery;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(ClampMin="1"))
	int32 RequiredCount = 1;

	virtual int32 GetRequiredCount() const override { return FMath::Max(1, RequiredCount); }
	virtual int32 CountEvent(const FVBGameEvent& Event) const override;
};

/** 지금 상태가 조건을 만족하나(2단계 규칙). 사건이 올 때마다·사실이 바뀔 때마다·단계가 시작될 때 다시 본다. */
USTRUCT(BlueprintType, meta=(DisplayName="조건"))
struct VOWBOUND_API FVBQuestObjective_Condition : public FVBQuestObjective
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(BaseStruct="/Script/Vowbound.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	virtual bool IsStateBased() const override { return true; }
	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

/** 단계 하나. 필수 목표가 다 차면 효과를 적용하고 다음 단계로 간다. 단계는 순서대로다 - 분기는 사실로 푼다(설계 3.3절). */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBQuestStage
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(MultiLine=true))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(BaseStruct="/Script/Vowbound.VBQuestObjective", ExcludeBaseStruct))
	TArray<FInstancedStruct> Objectives;

	// 이 단계를 마칠 때 적용한다(서버, 부른 플레이어 = 퀘스트 주인).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnCompleteEffects;
};

/**
 * 퀘스트 하나의 정의(실행 중에 바뀌지 않는 데이터). 설계 .sw/narrative 3.3절·12.1절.
 *
 * 자산 ID = (VBQuest, QuestId 태그 이름). 저장은 QuestId 로 하고 에셋 매니저가 /Game/Vowbound/Narrative/Quest 를 훑어 되찾는다 -
 *   아이템 정의와 같은 길이다(그래서 이 둘만 PrimaryDataAsset 이다).
 * 시작은 규칙의 효과(FVBEffect_StartQuest)로만 한다 - 대화 선택지·다른 퀘스트의 보상이 부른다. 진행은 사건으로만 한다(설계 2.2절).
 */
UCLASS(BlueprintType, Const)
class VOWBOUND_API UVBQuestDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static FPrimaryAssetType GetQuestAssetType();
	static FPrimaryAssetId MakeQuestAssetId(FGameplayTag InQuestId);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(Categories="Quest"))
	FGameplayTag QuestId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest")
	FText Title;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(MultiLine=true))
	FText Summary;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest")
	TArray<FVBQuestStage> Stages;

	// 마지막 단계를 마치면 적용한다. 보상 전용 타입을 두지 않는다 - 아이템·사실·평판이 다 효과로 적힌다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> Rewards;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
