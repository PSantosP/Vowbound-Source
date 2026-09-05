// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBAbilityActivationPolicy.generated.h"

/**
 * Ability 활성화 정책
 * GA의 EditDefaultsOnly에서 설정, 입력/스폰/이벤트 기반으로 분류
 */

UENUM(BlueprintType)
enum class EVBAbilityActivationPolicy : uint8
{
	// 입력 액션이 트리거되면 활성화 (대부분의 전투 Ability)
	OnInputTriggered,
	
	// 입력을 누르고 있는 동안 유지 (차지 공격, 블록 등)
	WhileInputActive,
	
	// Ability 부여 즉시 활성화 (패시브 효과)
	OnSpawn,
};