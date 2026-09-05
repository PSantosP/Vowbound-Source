// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "VBAttributeSetBase.generated.h"


// ATTRIBUTE_ACCESSORS 매크로 정의
// Get...Attribute(), Get...(), Set...(), Init...() 4개 함수 자동생성해줌
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * Vowbound AttributeSet 기초 클래스. 공통 Clamp/Pre-Post 로직 제공.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBAttributeSetBase : public UAttributeSet
{
	GENERATED_BODY()
	
public:
	UVBAttributeSetBase();
	
protected:
	// Attribute 값을 Min~Max 범위로 클램핑하는 헬퍼
	void ClampAttributeOnChange(const FGameplayAttribute& Attribute,
		float& NewValue, float MinValue, float MaxValue) const;
	
	// OwnerActor의 ASC를 가져오는 헬퍼
	AActor* GetAvatarActorFromOwner() const;
};
