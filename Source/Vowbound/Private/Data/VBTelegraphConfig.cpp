// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBTelegraphConfig.h"

const FVBTelegraphAnswerCue* UVBTelegraphConfig::FindAnswerCue(const FGameplayTag& Answer) const
{
	// 정확 일치만 본다. 부모 태그 폴백을 두지 않는 이유: Attack.Answer 자체에 행을 두면 «답이 안 붙은 공격» 이
	//   그 행으로 조용히 흡수돼 누락을 못 본다. 없으면 없다고 돌려준다.
	return Answer.IsValid() ? AnswerCues.Find(Answer) : nullptr;
}
