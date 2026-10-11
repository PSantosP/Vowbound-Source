// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBEncounterRules.h"

#include "Enemy/VBEncounterGateComponent.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "Vowbound/Vowbound.h"

void FVBEffect_HoldEncounter::Apply(const FVBRuleContext& Context) const
{
	if (EncounterTags.IsEmpty() || !Context.World)
	{
		VB_LOG(Warning, "효과(조우 보류): 꼬리표가 비었거나 월드가 없다 - 아무것도 안 한다");
		return;
	}
	int32 Count = 0;
	// 드물게 불리는 효과라(장면 시작·끝) 조우 문을 전부 훑어도 된다. 다른 월드(에디터·PIE 가 함께 뜬 경우)의 것은 거른다.
	for (TObjectIterator<UVBEncounterGateComponent> It; It; ++It)
	{
		UVBEncounterGateComponent* Gate = *It;
		if (!IsValid(Gate) || Gate->GetWorld() != Context.World || !Gate->GetEncounterTags().HasAny(EncounterTags))
		{
			continue;
		}
		Gate->SetHeld(bHold);
		++Count;
	}
	// 하나도 못 찾으면 장면 동안 적이 계속 움직인다 - 꼬리표 오타를 조용히 넘기지 않는다.
	VB_CLOG(Count == 0, Warning, "효과(조우 보류): 꼬리표 %s 를 가진 조우 문이 이 월드에 없다", *EncounterTags.ToStringSimple());
	VB_CLOG(Count > 0, Log, "효과(조우 보류): %d 곳 %s", Count, bHold ? TEXT("보류") : TEXT("풀기"));
}
