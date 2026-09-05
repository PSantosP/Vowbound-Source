// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "VBAttributeSetBase.h"
#include "VBHealthAttributeSet.generated.h"

/**
 * 체력/실드 AttributeSet
 * Meta Attribute: IncomingDamage, IncomingHealing
 * PostGameplayEffectExecute에서 데미지 파이프라인 처리
 */
UCLASS()
class VOWBOUND_API UVBHealthAttributeSet : public UVBAttributeSetBase
{
	GENERATED_BODY()
	
public:
	UVBHealthAttributeSet();
	
	// 실제 Attribute (복제됨)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health, Category="Vowbound|Health")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, Health)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxHealth, Category="Vowbound|Health")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, MaxHealth)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Shield, Category="Vowbound|Health")
	FGameplayAttributeData Shield;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, Shield)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxShield, Category="Vowbound|Health")
	FGameplayAttributeData MaxShield;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, MaxShield)
	
	// Meta Attribute(복제안함, 계산용)
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Health")
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, IncomingDamage)
	
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Health")
	FGameplayAttributeData IncomingHealing;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, IncomingHealing)
	
	// 사망 컨텍스트 (마지막 데미지 정보)
	// VBEnemyBase::HandleDeath에서 참조하여 명성 처리
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LastDamageInstigator;
	
	// 마지막 데미지가 의료 행위(정화/처치)인지 여부
	bool bLastDamageWasMedical = false;
	
	float CachedMaxHealth = 0.0f;
	float CachedMaxShield = 0.0f;
	
protected:
	// Attribute 변경 직전 -> 클램핑
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	
	// GE 적용 직후 -> 데미지/힐 파이프라인
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
	
	// 복제
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	// OnRep 함수 (GAMEPLAYATTRIBUTE_REPNOTIFY 매크로 사용)
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldHealth);
	
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);
	
	UFUNCTION()
	void OnRep_Shield(const FGameplayAttributeData& OldShield);
	
	UFUNCTION()
	void OnRep_MaxShield(const FGameplayAttributeData& OldMaxShield);
};
