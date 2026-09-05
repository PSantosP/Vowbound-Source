// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Attributes/VBAttributeSetBase.h"

#include "AbilitySystemComponent.h"

UVBAttributeSetBase::UVBAttributeSetBase()
{
}

void UVBAttributeSetBase::ClampAttributeOnChange(
	const FGameplayAttribute& Attribute,
	float& NewValue,
	float MinValue,
	float MaxValue) const
{
	NewValue = FMath::Clamp(NewValue, MinValue, MaxValue);
}

AActor* UVBAttributeSetBase::GetAvatarActorFromOwner() const
{
	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
	return ASC ? ASC->GetAvatarActor() : nullptr;
}