// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FVBQuestLogEntry;

/**
 * 퀘스트를 글로 푸는 곳 하나. HUD 목표 줄과 일지 화면이 같이 쓴다(설계 .sw/narrative 12.5절) -
 *   «목표 n/m» 의 모양을 두 화면이 따로 만들면 한쪽만 고쳐진다.
 */
namespace VBQuestText
{
	// 지금 단계의 목표들. 한 목표 한 줄, 수가 2 이상이면 «(n/m)», 다 찼으면 «(완료)», 선택 목표는 «(선택)» 을 앞에.
	VOWBOUND_API FText DescribeObjectives(const FVBQuestLogEntry& Entry);

	// HUD 한 덩어리: 제목 / 단계 설명 / 목표들.
	VOWBOUND_API FText DescribeTracker(const FVBQuestLogEntry& Entry);
}
