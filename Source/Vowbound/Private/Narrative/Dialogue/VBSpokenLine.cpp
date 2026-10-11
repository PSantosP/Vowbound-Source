// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Dialogue/VBSpokenLine.h"

#include "Narrative/Dialogue/VBSpokenLineComponent.h"
#include "Vowbound/Vowbound.h"

void FVBEffect_SpeakLine::Apply(const FVBRuleContext& Context) const
{
	if (!VBRules::EvaluateAll(Conditions, Context))
	{
		return;
	}
	UVBSpokenLineComponent* Lines = UVBSpokenLineComponent::FindFor(Context.Instigator);
	if (!Lines)
	{
		// 말할 곳이 없다 - 조용히 버리면 «대사가 안 나온다» 가 유일한 증상이다.
		VB_LOG(Warning, "효과(줄 말하기): 부른 플레이어의 PC 에 UVBSpokenLineComponent 가 없다 - «%s» 를 버린다", *Line.Text.ToString());
		return;
	}
	Lines->SpeakLine(Line);
}
