// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UVBItemDefinition;

/** 장착 요청의 결과. 서버가 로그로, 시험이 판정으로 쓴다. */
enum class EVBEquipResult : uint8
{
	Changed,         // 칸이 바뀌었다(같은 아이템이 다른 칸에 있었으면 그 칸은 비었다)
	Unchanged,       // 이미 그 칸에 있다
	NotEquippable,   // 장비 조각이 없다
	NoFittingSlot,   // 맞는 칸이 없다(지정한 칸이 종류가 다르거나 범위 밖)
};

/**
 * 장비 판정(순수 함수). 설계 .sw/remaining-features/b1-equipment.md.
 * 칸의 주인은 UVBEquipmentComponent 다 - 여기는 «어느 칸에 들어가나, 들어가면 칸이 어떻게 되나» 만 답한다. 월드·GAS 를 모르므로 단위 시험이 전부 덮는다.
 * 요청과 불러오기가 같은 함수를 탄다.
 */
namespace VBEquipment
{
	// 이 아이템이 이 칸 종류에 들어가나.
	VOWBOUND_API bool Fits(const UVBItemDefinition* Item, const FGameplayTag& SlotType);

	// Item 을 입는다. RequestedSlot 이 범위 안이면 그 칸(종류가 맞아야), 아니면 고른다 - 맞는 빈 칸 먼저, 다 찼으면 맞는 첫 칸을 바꾼다.
	//   같은 아이템이 다른 칸에 있으면 그 칸을 비운다(한 아이템 한 칸 - 같은 부적을 두 칸에 못 낀다).
	//   OutSlot = 들어간 칸, OutReplaced = 그 칸에서 밀려난 아이템, OutVacated = 같은 아이템이 있다 비워진 칸(없으면 INDEX_NONE).
	VOWBOUND_API EVBEquipResult Equip(const TArray<FGameplayTag>& Layout, TArray<const UVBItemDefinition*>& InOutEquipped, const UVBItemDefinition* Item,
		int32 RequestedSlot, int32& OutSlot, const UVBItemDefinition*& OutReplaced, int32& OutVacated);

	VOWBOUND_API const TCHAR* LexToString(EVBEquipResult Result);
}
