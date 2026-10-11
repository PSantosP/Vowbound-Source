// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Story/VBRuleTypes.h"

#include "Engine/World.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Vowbound/Vowbound.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool VBRules::Compare(EVBRuleCompare Op, double Lhs, double Rhs)
{
	switch (Op)
	{
	case EVBRuleCompare::AtLeast: return Lhs >= Rhs;
	case EVBRuleCompare::AtMost:  return Lhs <= Rhs;
	case EVBRuleCompare::Equal:   return Lhs == Rhs;
	}
	return false;
}

bool FVBRuleContext::CanApplyEffects() const
{
	return !World || World->GetNetMode() != NM_Client;
}

FVBRuleContext FVBRuleContext::Make(const UObject* WorldContextObject, APlayerState* InInstigator)
{
	FVBRuleContext Context;
	Context.World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	Context.Instigator = InInstigator;
	Context.Story = UVBStoryStateComponent::Get(WorldContextObject);
	return Context;
}

// ---------------------------------------------------------------- 조건

bool FVBCondition_Fact::Evaluate(const FVBRuleContext& Context) const
{
	if (!Context.Story)
	{
		// 사실을 읽을 곳이 없으면 거짓이다 - 알 수 없는 것을 참으로 흘리면 잠긴 선택지가 열린다.
		VB_LOG(Warning, "조건(사실 %s): 문맥에 사실 저장소가 없다 - 거짓으로 친다", *Fact.ToString());
		return false;
	}
	return Context.Story->HasFact(Fact) != bNegate;
}

bool FVBCondition_Counter::Evaluate(const FVBRuleContext& Context) const
{
	if (!Context.Story)
	{
		VB_LOG(Warning, "조건(카운터 %s): 문맥에 사실 저장소가 없다 - 거짓으로 친다", *Counter.ToString());
		return false;
	}
	return VBRules::Compare(Compare, Context.Story->GetCounter(Counter), Value);
}

// ---------------------------------------------------------------- 효과

void FVBEffect_SetFact::Apply(const FVBRuleContext& Context) const
{
	if (Context.Story)
	{
		Context.Story->SetFact(Fact, bOn);
	}
	else
	{
		VB_LOG(Warning, "효과(사실 %s): 문맥에 사실 저장소가 없다 - 적용 못 함", *Fact.ToString());
	}
}

void FVBEffect_AddCounter::Apply(const FVBRuleContext& Context) const
{
	if (Context.Story)
	{
		Context.Story->AddToCounter(Counter, Delta);
	}
	else
	{
		VB_LOG(Warning, "효과(카운터 %s): 문맥에 사실 저장소가 없다 - 적용 못 함", *Counter.ToString());
	}
}

// ---------------------------------------------------------------- 배열

bool VBRules::EvaluateAll(const TArray<FInstancedStruct>& Conditions, const FVBRuleContext& Context)
{
	for (const FInstancedStruct& Entry : Conditions)
	{
		const FVBCondition* Condition = Entry.GetPtr<FVBCondition>();
		if (!Condition)
		{
			// 빈 칸이나 조건이 아닌 구조체. 참으로 넘기면 저작 실수가 «항상 열림» 으로 숨는다.
			VB_LOG(Warning, "조건 배열에 조건이 아닌 원소(%s) - 거짓으로 친다",
			       Entry.GetScriptStruct() ? *Entry.GetScriptStruct()->GetName() : TEXT("빈 칸"));
			return false;
		}
		if (!Condition->Evaluate(Context))
		{
			return false;
		}
	}
	return true;
}

bool VBRules::ApplyAll(const TArray<FInstancedStruct>& Effects, const FVBRuleContext& Context)
{
	if (!Context.CanApplyEffects())
	{
		VB_LOG(Warning, "효과 %d개를 클라에서 적용하려 했다 - 아무것도 안 바꾼다(효과는 서버만)", Effects.Num());
		return false;
	}
	for (const FInstancedStruct& Entry : Effects)
	{
		if (const FVBEffect* Effect = Entry.GetPtr<FVBEffect>())
		{
			Effect->Apply(Context);
		}
		else
		{
			VB_LOG(Warning, "효과 배열에 효과가 아닌 원소(%s) - 건너뛴다",
			       Entry.GetScriptStruct() ? *Entry.GetScriptStruct()->GetName() : TEXT("빈 칸"));
		}
	}
	return true;
}

#if WITH_EDITOR
bool VBRules::ValidateStructArray(const TArray<FInstancedStruct>& Items, const UScriptStruct* Base, const FText& Where,
                                  FDataValidationContext& Context)
{
	bool bValid = true;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const UScriptStruct* Type = Items[Index].GetScriptStruct();
		if (!Type || !Type->IsChildOf(Base))
		{
			Context.AddError(FText::Format(NSLOCTEXT("VBRules", "BadStruct", "{0}: {1} 번 칸이 비었거나 {2} 가 아니다."),
			                               Where, Index, Base->GetDisplayNameText()));
			bValid = false;
		}
	}
	return bValid;
}
#endif
