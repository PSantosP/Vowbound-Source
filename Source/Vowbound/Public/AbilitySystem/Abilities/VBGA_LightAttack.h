// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGA_MeleeAttackBase.h"
#include "VBGA_LightAttack.generated.h"

/**
 * 약공격 Ability
 * CommitAbility -> SphereTrace -> GE_Damage 적용
 * BP 서브클래스에서 DamageEffect, AttackRange 등 설정
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_LightAttack : public UVBGA_MeleeAttackBase
{
	GENERATED_BODY()
	
public:
	UVBGA_LightAttack();
};