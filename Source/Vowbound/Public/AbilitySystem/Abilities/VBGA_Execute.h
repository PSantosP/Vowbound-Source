// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGA_MedicalAbilityBase.h"
#include "VBGA_Execute.generated.h"

/**
 * 처치 어빌리티
 * 빈사 상태(State.Combat.LowHealth) 적에게만 사용 가능
 * 즉사 데미지 -> 명성 감소
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_Execute : public UVBGA_MedicalAbilityBase
{
	GENERATED_BODY()
	
public:
	UVBGA_Execute();
};
