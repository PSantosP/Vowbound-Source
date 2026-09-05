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
	// 무거운 적의 '뚝심'은 State.Combat.SuperArmor 태그가 아니라 이 값의 크기로 표현한다.
	//   태그로도 표현하면 같은 일을 두 메커니즘이 하게 되고, 어느 쪽을 튜닝해도 다른 쪽이 조용히 상쇄한다.
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxPoise, Category="Vowbound|Combat")
	FGameplayAttributeData MaxPoise;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, MaxPoise)

	// Meta Attribute (비복제, 계산용 - IncomingDamage 와 동형).
	// GE_PhysicalDamage 가 SetByCaller(Data.PoiseDamage)로 여기에 더하면 PostGameplayEffectExecute 가 소비한다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Combat")
	FGameplayAttributeData IncomingPoiseDamage;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, IncomingPoiseDamage)

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
