// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "VBAttributeSetBase.h"
#include "VBReputationAttributeSet.generated.h"

/**
 * 명성(Reputation) AttributeSet
 * 치로 -> 명성 증가, 처치 -> 명성 감소
 * GDD 12: 명성 축 -100(학살자) ~ +100(치료사)
 */
UCLASS()
class VOWBOUND_API UVBReputationAttributeSet : public UVBAttributeSetBase
{
	GENERATED_BODY()
	
public:
	UVBReputationAttributeSet();
	
	// 명성 수치 (-100 ~ +100)
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Reputation, Category="Vowbound|Reputation")
	FGameplayAttributeData Reputation;
	ATTRIBUTE_ACCESSORS(UVBReputationAttributeSet, Reputation)
	
	// Meta Attribute: 명성 변동값(복제 안함)
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Reputation")
	FGameplayAttributeData IncomingReputationChange;
	ATTRIBUTE_ACCESSORS(UVBReputationAttributeSet, IncomingReputationChange)
	
protected:
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	UFUNCTION()
	void OnRep_Reputation(const FGameplayAttributeData& OldReputation);
	
public:
	// 명성 축의 끝점. GDD 12 가 정의하는 제품 상수이지 디자이너 튜너블이 아니다.
	// 어트리뷰트로 승격하지 않는다 - 2026-08-09 기각, 재제안 금지. 근거 둘:
	//  1) 페이싱("몇 번 만에 학살자가 되는가")은 이미 데이터다 - UVBEnemyConfig::DeathReputationChange 와
	//     UVBMedicalConfig::ReputationChangeOnMedical. 끝점을 튜너블로 올리면 같은 판단을 하는 홈이 둘이 된다.
	//  2) 승격하면 초기화 GE 가 Min/Max 를 안 채웠을 때 Clamp(x, 0, 0) 으로 명성이 영구 0 이 되는데,
	//     그것이 조용하다. 얻는 것 없이 조용한 파국만 들인다.
	//
	// 2026-08-19 접근 범위만 private -> public 으로 바꿨다(승격이 아니다).
	//   종전 근거 1번 "읽는 곳이 클램프 2개뿐이고 외부 소비자가 없다"는 이제 거짓이다 -
	//   UVBReputationBarWidget 이 -100~+100 축을 0~1 비율로 옮기려면 끝점을 알아야 한다.
	//   위젯이 숫자를 다시 적으면 축의 홈이 둘이 되고, 한쪽만 바뀌는 날 바가 조용히 틀린 비율을 그린다.
	static constexpr float ReputationMin = -100.0f;
	static constexpr float ReputationMax = 100.0f;
};
