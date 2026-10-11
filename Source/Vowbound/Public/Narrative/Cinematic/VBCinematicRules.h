#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBCinematicRules.generated.h"

class UVBCinematicDefinition;

USTRUCT(BlueprintType, meta=(DisplayName="시네마틱 재생"))
struct VOWBOUND_API FVBEffect_PlayCinematic : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Cinematic")
	TObjectPtr<UVBCinematicDefinition> Definition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Cinematic")
	FName PlayerBindingTag = TEXT("Player");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Cinematic",
		meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnCompletedEffects;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

USTRUCT(BlueprintType, meta=(DisplayName="시네마틱 조종 복귀 완료"))
struct VOWBOUND_API FVBCondition_CinematicCompleted : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Cinematic")
	TObjectPtr<UVBCinematicDefinition> Definition;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};
