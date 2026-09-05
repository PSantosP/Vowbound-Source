// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGA_MedicalAbilityBase.h"
#include "VBGA_Purify.generated.h"

/**
 * 정화 어빌리티
 * 빈사 상태 적을 의료적으로 정화
 * 즉사 데미지 -> 명성 증가
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_Purify : public UVBGA_MedicalAbilityBase
{
	GENERATED_BODY()
	
public:
	UVBGA_Purify();
};
