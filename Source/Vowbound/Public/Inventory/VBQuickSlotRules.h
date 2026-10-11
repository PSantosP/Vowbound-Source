// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UVBItemDefinition;

/** 칸에 넣기의 결과. 서버가 로그로, 시험이 판정으로 쓴다. */
enum class EVBQuickSlotAssignResult : uint8
{
	Changed,     // 칸이 바뀌었다(다른 칸에서 옮겨 왔으면 그 칸도 비었다)
	Unchanged,   // 이미 그 자리다 - 알릴 것이 없다
	OutOfRange,  // 없는 칸
	NotUsable,   // «사용» 조각이 없는 아이템
};

/**
 * 퀵슬롯 판정(순수 함수). 설계 .sw/quickslot-input 4절·4.1절.
 * 칸의 주인은 UVBInventoryComponent 다 - 여기는 «넣을 수 있나, 넣으면 칸이 어떻게 되나» 만 답한다. 월드·권한·복제를 모르므로 단위 시험이 전부 덮는다.
 * 요청(서버 RPC)과 불러오기가 같은 함수를 탄다 - 저장 줄이 넣는 규칙(사용 조각·한 아이템 한 칸)을 건너뛰지 못하게.
 */
namespace VBQuickSlot
{
	// 칸에 넣을 수 있는 아이템인가 - «사용» 조각이 있어야 한다. 빈 아이템(빼기)은 늘 된다.
	//   가진 수는 보지 않는다 - 0 개여도 칸은 그대로 두고(흐리게), 다시 주우면 그 자리에 돌아온다(4절).
	VOWBOUND_API bool CanAssign(const UVBItemDefinition* Item);

	// 칸 SlotIndex 에 Item 을 넣는다(nullptr = 빼기). 같은 아이템이 다른 칸에 있으면 그 칸을 비운다 - 한 아이템은 한 칸에만.
	//   거절(OutOfRange·NotUsable)이면 칸을 하나도 안 바꾼다.
	VOWBOUND_API EVBQuickSlotAssignResult Assign(TArray<TObjectPtr<const UVBItemDefinition>>& Slots, int32 SlotIndex, const UVBItemDefinition* Item);

	// 로그용 이름.
	VOWBOUND_API const TCHAR* LexToString(EVBQuickSlotAssignResult Result);
}
