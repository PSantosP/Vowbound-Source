// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UVBItemDefinition;
struct FVBInventoryEntry;

/** 인벤토리 화면의 정렬. 화면 상태라 저장하지 않는다. */
enum class EVBInventorySort : uint8
{
	Acquired,  // 가진 순서 그대로(획득 순)
	Name,      // 표시 이름
	Count,     // 많은 것 먼저
	Kind,      // 아이템 ID 태그 이름 - 같은 종류(Item.Medical...)끼리 모인다
};

/**
 * 인벤토리 화면이 무엇을 어떤 순서로 보이나(순수 함수). 설계 .sw/remaining-features/a1-inventory.md.
 * 가진 것의 주인은 UVBInventoryComponent 다 - 여기는 보기만 정한다. 월드·권한을 모르므로 단위 시험이 전부 덮는다.
 * 필터는 아이템 ID 태그의 계층을 쓴다(Item.Medical 필터 -> Item.Medical.Gauze) - 따로 «종류» 칸을 두면 같은 사실의 주인이 둘이 된다.
 */
namespace VBInventoryView
{
	// 필터에 맞나. 빈 태그 = 전체.
	VOWBOUND_API bool PassesFilter(const UVBItemDefinition* Item, const FGameplayTag& Filter);

	// 보일 줄 순서. 같은 값끼리는 가진 순서를 지킨다(안정 정렬). 정의가 빈 줄은 뺀다.
	VOWBOUND_API TArray<const UVBItemDefinition*> BuildRows(const TArray<FVBInventoryEntry>& Entries, const FGameplayTag& Filter, EVBInventorySort Sort);

	// 정렬 버튼이 누를 때마다 넘어갈 다음 정렬(끝에서 처음으로).
	VOWBOUND_API EVBInventorySort Next(EVBInventorySort Sort);
}
