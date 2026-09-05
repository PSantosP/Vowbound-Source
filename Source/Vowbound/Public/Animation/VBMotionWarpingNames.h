// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * VBMotionWarpingNames
 *
 * Motion Warping Target FName 공유 정의.
 * GA / 몽타주 / AnimNotify에서 사용하는 WarpTarget 이름을 한 곳에서 관리.
 *
 * 추가 규칙: 새 WarpTarget 이름이 필요하면 여기에 `inline const FName` 로 추가.
 *           cpp 로컬 namespace로 재정의 금지 (ARCH-R1 이슈 방지).
 */
namespace VBMotionWarpingNames
{
	inline const FName AttackTarget = FName(TEXT("AttackTarget"));

	// 제자리 회전 (FIND-105). 클립은 90/180 고정인데 요구 각도는 임의라, 그 차이를 회전 워프가 메운다.
	//   위치는 안 옮긴다 - 회전만 쓰는 타깃이다.
	inline const FName TurnInPlaceTarget = FName(TEXT("TurnInPlaceTarget"));
}
