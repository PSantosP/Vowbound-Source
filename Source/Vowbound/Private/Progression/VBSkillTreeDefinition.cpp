// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Progression/VBSkillTreeDefinition.h"

#include "Abilities/GameplayAbility.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBSkillTree"

const FVBSkillNode* UVBSkillTreeDefinition::FindNode(const FGameplayTag& NodeId) const
{
	return NodeId.IsValid() ? Nodes.FindByPredicate([&NodeId](const FVBSkillNode& Node) { return Node.NodeId == NodeId; }) : nullptr;
}

TArray<FText> UVBSkillTreeDefinition::FindProblems(const UVBSkillTreeDefinition& Tree)
{
	TArray<FText> Problems;
	TSet<FGameplayTag> Seen;
	for (const FVBSkillNode& Node : Tree.Nodes)
	{
		const FText Name = FText::FromString(Node.NodeId.ToString());
		if (!Node.NodeId.IsValid())
		{
			Problems.Add(LOCTEXT("EmptyId", "NodeId 가 빈 칸이 있다 - 저장·선행 조건이 부를 수 없다."));
			continue;
		}
		if (Seen.Contains(Node.NodeId))
		{
			Problems.Add(FText::Format(LOCTEXT("DuplicateId", "{0}: 같은 NodeId 가 둘이다."), Name));
		}
		Seen.Add(Node.NodeId);
		if (Node.Cost < 1)
		{
			Problems.Add(FText::Format(LOCTEXT("Cost", "{0}: Cost 는 1 이상이어야 한다(포인트를 안 쓰는 칸은 남은 포인트 셈을 흐린다)."), Name));
		}
		if (Node.RequiredLevel < 1)
		{
			Problems.Add(FText::Format(LOCTEXT("Level", "{0}: RequiredLevel 은 1 이상이어야 한다."), Name));
		}
		if (Node.Stats.IsEmpty() && !Node.GrantedAbility)
		{
			Problems.Add(FText::Format(LOCTEXT("Empty", "{0}: 능력치도 능력도 없다 - 열어도 아무 일이 없다."), Name));
		}
		for (const FGameplayTag& Prerequisite : Node.Prerequisites)
		{
			if (!Tree.FindNode(Prerequisite))
			{
				Problems.Add(FText::Format(LOCTEXT("MissingPrerequisite", "{0}: 선행 {1} 이 이 트리에 없다 - 영영 못 연다."),
					Name, FText::FromString(Prerequisite.ToString())));
			}
		}
	}

	// 선행 고리: 칸마다 선행을 따라가 자기에게 돌아오면 고리다(칸 수가 작아 깊이 우선 한 번씩이면 충분하다).
	enum class EVisit : uint8 { None, Active, Done };
	TMap<FGameplayTag, EVisit> Visit;
	TFunction<bool(const FVBSkillNode&)> HasCycle = [&](const FVBSkillNode& Node) -> bool
	{
		EVisit& State = Visit.FindOrAdd(Node.NodeId, EVisit::None);
		if (State == EVisit::Active)
		{
			return true;
		}
		if (State == EVisit::Done)
		{
			return false;
		}
		State = EVisit::Active;
		for (const FGameplayTag& Prerequisite : Node.Prerequisites)
		{
			const FVBSkillNode* Next = Tree.FindNode(Prerequisite);
			if (Next && HasCycle(*Next))
			{
				return true;
			}
		}
		Visit.FindOrAdd(Node.NodeId) = EVisit::Done;
		return false;
	};
	for (const FVBSkillNode& Node : Tree.Nodes)
	{
		if (Node.NodeId.IsValid() && HasCycle(Node))
		{
			Problems.Add(FText::Format(LOCTEXT("Cycle", "{0}: 선행 조건이 고리를 이룬다 - 고리 안의 칸은 영영 못 연다."), FText::FromString(Node.NodeId.ToString())));
			break;
		}
	}
	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBSkillTreeDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (const FText& Problem : FindProblems(*this))
	{
		Context.AddError(Problem);
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
