// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBCombatTypes.generated.h"

/**
 * 전투 그룹 안에서 적 하나가 맡는 역할.
 *
 * 왜 별도 헤더인가: 컴포넌트(UVBEnemyCombatComponent)·서브시스템(UVBCombatCoordinatorSubsystem)·
 *   블랙보드 셋이 이 값을 참조한다. 어느 한쪽 헤더에 두면 나머지가 그 헤더를 끌어와야 하고 순환이 생긴다.
 *
 * 왜 태그가 아니라 열거형인가: 상호 배타적인 단일 상태이고, BT 가 이 값으로 직접 분기한다.
 *   태그로 두면 "둘 다 가진 상태"가 표현 가능해져 트리가 그 경우를 방어해야 한다.
 *
 * 확장 규약: 역할을 늘리는 것은 여기 값을 더하고 코디네이터의 RecomputeGroup 배정 규칙에 한 줄
 *   더하는 일이다. 값의 순서는 블랙보드에 uint8 로 직렬화되므로 중간 삽입 금지 - 항상 끝에 더한다.
 */
UENUM(BlueprintType)
enum class EVBCombatRole : uint8
{
	Idle        UMETA(DisplayName = "Idle (전투 미참여)"),
	Approaching UMETA(DisplayName = "Approaching (토큰 보유, 사거리 밖)"),
	Attacker    UMETA(DisplayName = "Attacker (토큰 보유, 사거리 안)"),
	Surrounder  UMETA(DisplayName = "Surrounder (토큰 미보유, 링 슬롯 대기)"),
	// 측면 가중 역할. 값만 두고 이번 마일스톤에서는 배정하지 않는다(GDD 7.2 가 명시한 확장점).
	// 열거형 값 하나는 "미래에 쓸 코드"가 아니라 "역할 목록의 완전성"이다 - EVBTeam 과 같은 성질.
	Flanker     UMETA(DisplayName = "Flanker (측면 가중 - 미배정)"),
};
