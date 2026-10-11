// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBRuleTypes.generated.h"

class AActor;
class UWorld;
class APlayerState;
class UVBStoryStateComponent;

/**
 * 규칙 한 번의 문맥. 설계 .sw/narrative 2.2절. 부르는 쪽의 스택에만 산다(평가·적용 한 번 동안) - 그래서 UObject 참조를 붙들지 않는다.
 * 규칙은 이 문맥이 가리키는 것만 읽고 쓴다 - 스스로 월드를 뒤져 대상을 찾지 않는다(대화·퀘스트·상점이 같은 규칙을 다른 대상에 쓴다).
 */
struct VOWBOUND_API FVBRuleContext
{
	UWorld* World = nullptr;

	// 규칙을 부른 플레이어(선택지를 고른 사람, 퀘스트를 가진 사람). 평판처럼 플레이어의 것을 읽고 쓸 때 쓴다.
	APlayerState* Instigator = nullptr;

	// 세계 사실의 주인. 보통 AVBGameState 의 것(UVBStoryStateComponent::Get).
	UVBStoryStateComponent* Story = nullptr;

	// 부른 플레이어가 상대하는 액터(말을 건 NPC). 대화가 넣고, 상점 열기 효과가 상인으로 쓴다. 상대가 없는 자리(퀘스트 보상·아이템 사용)는 비어 있다.
	AActor* Counterpart = nullptr;

	// 효과를 적용해도 되는가(서버·싱글). 클라에서 효과가 돌면 사실이 서버와 갈린다. 월드가 없으면(단위 시험) 적용한다 - 복제할 곳도 없다.
	bool CanApplyEffects() const;

	// 보통의 문맥: 그 월드 + 그 월드의 세계 사실(GameState) + 부른 플레이어. 인벤토리·대화가 같은 것을 만든다.
	static FVBRuleContext Make(const UObject* WorldContextObject, APlayerState* InInstigator);
};

/** «참인가». 바탕형 자체는 참이다 - 조건 없는 자리의 기본. 파생형이 Evaluate 를 덮는다. */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBCondition
{
	GENERATED_BODY()

	virtual ~FVBCondition() = default;
	virtual bool Evaluate(const FVBRuleContext& Context) const { return true; }
};

/** «바꿔라». 서버에서만 불린다(VBRules::ApplyAll 이 거른다). 바탕형은 아무것도 안 한다. */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBEffect
{
	GENERATED_BODY()

	virtual ~FVBEffect() = default;
	virtual void Apply(const FVBRuleContext& Context) const {}
};

// ---------------------------------------------------------------- 조건

/** 사실이 켜져 있나(bNegate 면 꺼져 있나). */
USTRUCT(BlueprintType, meta=(DisplayName="사실"))
struct VOWBOUND_API FVBCondition_Fact : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story", meta=(Categories="Story"))
	FGameplayTag Fact;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	bool bNegate = false;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

UENUM(BlueprintType)
enum class EVBRuleCompare : uint8
{
	AtLeast,
	AtMost,
	Equal,
};

/** 카운터가 값과 비교해 맞나(없는 카운터 = 0). */
USTRUCT(BlueprintType, meta=(DisplayName="카운터"))
struct VOWBOUND_API FVBCondition_Counter : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story", meta=(Categories="Story"))
	FGameplayTag Counter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	EVBRuleCompare Compare = EVBRuleCompare::AtLeast;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	int32 Value = 1;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

// 평판 조건은 Player/VBPlayerRules 에 있다 - 규칙 핵심이 GAS 를 보지 않게(설계 13.1절 2번).

// ---------------------------------------------------------------- 효과

/** 사실을 켜거나 끈다. */
USTRUCT(BlueprintType, meta=(DisplayName="사실 바꾸기"))
struct VOWBOUND_API FVBEffect_SetFact : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story", meta=(Categories="Story"))
	FGameplayTag Fact;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	bool bOn = true;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

/** 카운터에 더한다(음수면 뺀다). */
USTRUCT(BlueprintType, meta=(DisplayName="카운터 더하기"))
struct VOWBOUND_API FVBEffect_AddCounter : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story", meta=(Categories="Story"))
	FGameplayTag Counter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	int32 Delta = 1;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

// «GE 걸기» 효과도 Player/VBPlayerRules 에 있다(같은 이유).

/** 조건·효과 배열을 평가·적용하는 단 하나의 길. 대화·퀘스트·상점이 모두 이것을 부른다. */
namespace VBRules
{
	// 비교 하나. 카운터·평판·아이템 개수 조건이 같이 쓴다(셋째 쓰임에서 뽑았다).
	VOWBOUND_API bool Compare(EVBRuleCompare Op, double Lhs, double Rhs);

	// 전부 참이면 참. 빈 배열 = 참. 바탕형이 아닌 원소(다른 구조체가 들어간 것)는 거짓으로 친다 - 알 수 없는 조건을 참으로 흘리지 않는다.
	VOWBOUND_API bool EvaluateAll(const TArray<FInstancedStruct>& Conditions, const FVBRuleContext& Context);

	// 차례대로 적용한다. 효과를 적용할 수 없는 문맥(클라)이면 아무것도 안 바꾸고 경고한다. 반환 = 적용했는가.
	VOWBOUND_API bool ApplyAll(const TArray<FInstancedStruct>& Effects, const FVBRuleContext& Context);

#if WITH_EDITOR
	// 자산 검증(IsDataValid)용: 칸마다 Base 의 파생 구조체가 들었나. 빈 칸·엉뚱한 구조체는 오류로 적는다(Where = 자리 이름).
	//   실행 때 조건은 거짓, 효과는 건너뛰므로 저작 때 먼저 잡는다. 대화·퀘스트·아이템 조각이 같이 쓴다.
	VOWBOUND_API bool ValidateStructArray(const TArray<FInstancedStruct>& Items, const UScriptStruct* Base, const FText& Where,
	                                      class FDataValidationContext& Context);
#endif
}
