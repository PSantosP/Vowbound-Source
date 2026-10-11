// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "VBAttributeSetBase.h"
#include "VBCombatAttributeSet.generated.h"

/**
 * 전투 Attribute (Stamina, AttackPower 등) 데미지/회복 계산의 핵심 데이터
 */
UCLASS()
class VOWBOUND_API UVBCombatAttributeSet : public UVBAttributeSetBase
{
	GENERATED_BODY()
	
public:
	UVBCombatAttributeSet();
	
	// Stamina
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Stamina, Category="Vowbound|Combat")
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, Stamina)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxStamina, Category="Vowbound|Combat")
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, MaxStamina)
	
	// AttackPower
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_AttackPower, Category="Vowbound|Combat")
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, AttackPower)

	// === Poise (경직 저항, D8) ===
	// 남은 경직 저항. 0 에 닿으면 Event.Combat.PoiseBroken 을 쏘고 즉시 MaxPoise 로 리필한다.
	// 복제하는 이유는 미래의 적 경직 게이지 UI 다 - 지금 읽는 코드는 없다.
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Poise, Category="Vowbound|Combat")
	FGameplayAttributeData Poise;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, Poise)

	// 이 액터가 버티는 총량. 0 이하 = "이 액터는 경직 시스템을 쓰지 않는다"이고 파이프라인 전체를 건너뛴다.
	// MaxStamina 와 달리 1 로 올려 치지 않는다 - 0 이 유효한 값(미사용)이기 때문이다.
	// 무거운 적이 잘 안 끊기는 정도는 슈퍼아머 태그(State.Combat.SuperArmor)가 아니라 이 값(기절 게이지 최대치)의 크기로 표현한다.
	//   태그로도 표현하면 같은 일을 두 메커니즘이 하게 되고, 어느 쪽을 튜닝해도 다른 쪽이 조용히 상쇄한다.
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxPoise, Category="Vowbound|Combat")
	FGameplayAttributeData MaxPoise;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, MaxPoise)

	// Meta Attribute (비복제, 계산용 - IncomingDamage 와 동형).
	// GE_PhysicalDamage 가 SetByCaller(Data.PoiseDamage)로 여기에 더하면 PostGameplayEffectExecute 가 소비한다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Combat")
	FGameplayAttributeData IncomingPoiseDamage;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, IncomingPoiseDamage)

	// 받는 피해 배수. 1.0 = 그대로. UVBHealthAttributeSet::PostGameplayEffectExecute 가 가드·블록 판정 뒤에 곱한다.
	// 그로기(다운) 중 GE 가 이 값을 올려 «넘어뜨리면 보상» 을 만든다(design .sw/boss/design.md §14.3).
	// 왜 태그가 아니라 속성인가: 배수의 크기는 튜닝값이고 홈이 GE 자산 하나여야 한다. 태그로 하면 크기가 코드 상수가 된다.
	// 복제하지 않는다 - 서버가 피해를 계산하고, 클라가 이 값을 읽는 코드는 없다. 기본 1.0 이 플레이어(같은 세트를 가짐)의 회귀를 막는다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Combat")
	FGameplayAttributeData DamageTakenScale;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, DamageTakenScale)

	// 반응 등급 저항(DEC-025). 공격이 선언한 등급을 이 단계 수만큼 깎는다(반올림한 정수로 읽는다). 0 = 그대로.
	// 덩치 큰 적은 여기가 크다 - 보스 2 면 넘어뜨리는 공격도 가벼운 피격이 된다.
	// 왜 MaxPoise 로 대신하지 않는가: 기절 게이지는 «쌓여서 깨지는» 양이고 이것은 «한 대가 얼마나 먹히나» 다. 축이 다르다.
	// 왜 속성인가: 허브가 읽으므로 피격자 소유이고(FIND-057c 규칙), GE_InitStats 가 MaxPoise 와 같이 저작한다.
	//   버프 GE 로 잠깐 올릴 수도 있다(예: 돌진 중 넘어지지 않음). 복제하지 않는다 - 서버만 읽는다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Combat")
	FGameplayAttributeData ImpactResistance;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, ImpactResistance)

	// 받는 넉백 배수(DEC-025). 1.0 = 공격이 정한 세기 그대로. 가벼운 적은 1 보다 크게, 무거운 적은 작게.
	// 이 값은 «물리 넉백» 에만 걸린다 - 반응 클립에 루트모션이 있으면 거리는 클립이 정한다(design §5, 엔진이 애니 루트모션을 우선한다).
	// DamageTakenScale 과 같은 모양이고 같은 이유로 복제하지 않는다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Combat")
	FGameplayAttributeData KnockbackTakenScale;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, KnockbackTakenScale)

protected:
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
	
	UFUNCTION()
	void OnRep_Stamina(const FGameplayAttributeData& OldStamina);
	UFUNCTION()
	void OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina);
	UFUNCTION()
	void OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower);
	UFUNCTION()
	void OnRep_Poise(const FGameplayAttributeData& OldPoise);
	UFUNCTION()
	void OnRep_MaxPoise(const FGameplayAttributeData& OldMaxPoise);

	float CachedMaxStamina = 0.0f;
};
