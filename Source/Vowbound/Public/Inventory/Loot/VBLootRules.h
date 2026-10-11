// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBLootRules.generated.h"

class UVBItemDefinition;
class UVBLootTable;
struct FRandomStream;

/** 굴린 결과 한 줄. 굴리고 넣는 한 번 동안 스택에만 산다 - 그래서 UObject 를 맨 포인터로 든다(FVBRuleContext 와 같은 판단). */
struct FVBLootDrop
{
	const UVBItemDefinition* Item = nullptr;
	int32 Count = 0;
};

/** 보상표 굴리기. 설계 .sw/narrative 14.3절. */
namespace VBLoot
{
	// 줄마다: 조건이 전부 참이면 -> FRand() < Chance 면 -> [MinCount, MaxCount] 에서 개수. 같은 아이템은 처음 나온 자리에 합친다(획득 알림 한 줄).
	//   조건으로 빠진 줄은 난수를 꺼내지 않는다. 빈 아이템 줄은 건너뛴다(IsDataValid 가 저작 때 잡는다).
	// 난수를 인자로 받는 이유: 단위 시험이 씨앗을 고정해 확률·개수 범위를 결정적으로 잰다. 실제로는 FVBEffect_GiveLoot 가 새 씨앗으로 만든다.
	VOWBOUND_API TArray<FVBLootDrop> Roll(const UVBLootTable& Table, const FVBRuleContext& Context, FRandomStream& Stream);

	// 남은 줄을 Give 로 넣는다(서랍장 남은 몫 - .sw/narrative 14.5 W1). 줄마다 넣은 만큼 빼고, 다 들어간 줄은 지운다.
	//   Give 가 0 을 주면(못 찾은 아이템·가득 참) 그 줄은 그대로 남는다 - 남은 몫을 잃지 않는 것이 이 함수의 일이다.
	//   Give 가 줄의 수보다 많이 넣었다고 해도 그 줄의 수까지만 뺀다. 0 이하인 줄은 넣지 않고 지운다. 돌려주는 값 = 이번에 넣은 총수.
	VOWBOUND_API int32 GiveStacks(TArray<FVBSavedItem>& InOutRemaining, TFunctionRef<int32(const FVBSavedItem&)> Give);
}

/**
 * 보상표를 굴려 부른 플레이어에게 넣는다. 적의 처치 효과(UVBEnemyConfig::OnKilledEffects)가 쓴다.
 * 서랍장은 이것을 쓰지 않는다 - 남은 몫을 드는 AVBLootContainer 가 표를 직접 든다(.sw/narrative 14.5 W1). 서랍장의 완료 효과에 달면 남은 몫 없이 두 번 준다.
 * 획득 알림과 Event.Item.Acquired 는 인벤토리(AddItem)가 낸다 - 여기서는 새 표시를 만들지 않는다.
 * 넘친 몫(가득 참)은 상대(Counterpart)의 발밑에 줍기로 떨군다(14.5 덧붙임, user 결정 2026-10-03). 상대 = 처치는 죽은 적, 대화는 말 건 NPC, 상점은 상인,
 *   상호작용 완료 효과는 그 대상 - «주는 쪽 발밑» 이라 어느 길이든 맞다. 상대가 없는 길(퀘스트 보상·아이템 사용)이면 버리고 경고 - 놓을 자리가 없다.
 * 한 적에 이 효과를 둘 달면 떨군 자리 벌림이 효과마다 0 번부터라 첫 줄끼리 겹친다 - 표 하나로 합쳐 단다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="보상표 주기"))
struct VOWBOUND_API FVBEffect_GiveLoot : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Loot")
	TObjectPtr<const UVBLootTable> Table;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
