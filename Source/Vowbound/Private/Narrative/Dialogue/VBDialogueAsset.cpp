// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Dialogue/VBDialogueAsset.h"

#include "Narrative/Dialogue/VBSpeakerDefinition.h"
#include "Narrative/Story/VBRuleTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBDialogueAsset"

FName UVBDialogueAsset::ChooseEntry(const FVBRuleContext& Context) const
{
	for (const FVBDialogueEntryPoint& Entry : EntryPoints)
	{
		if (VBRules::EvaluateAll(Entry.Conditions, Context))
		{
			return Entry.StartNodeId;
		}
	}
	return NAME_None;
}

const FVBDialogueNode* UVBDialogueAsset::FindNode(FName NodeId) const
{
	if (NodeId.IsNone())
	{
		return nullptr;
	}
	return Nodes.FindByPredicate([NodeId](const FVBDialogueNode& Node) { return Node.NodeId == NodeId; });
}

const UVBSpeakerDefinition* UVBDialogueAsset::FindSpeaker(FGameplayTag SpeakerId) const
{
	for (const UVBSpeakerDefinition* Speaker : Speakers)
	{
		if (Speaker && Speaker->SpeakerId == SpeakerId)
		{
			return Speaker;
		}
	}
	return nullptr;
}

#if WITH_EDITOR
EDataValidationResult UVBDialogueAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto Fail = [&Result](FDataValidationContext& InContext, const FText& Message)
	{
		InContext.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};

	if (EntryPoints.IsEmpty())
	{
		Fail(Context, LOCTEXT("NoEntry", "진입점이 없다 - 이 대화는 시작할 수 없다."));
	}
	for (int32 Index = 0; Index < Speakers.Num(); ++Index)
	{
		if (!Speakers[Index])
		{
			Fail(Context, FText::Format(LOCTEXT("NullSpeaker", "화자 {0} 번이 비었다."), Index));
		}
	}

	TSet<FName> NodeIds;
	for (const FVBDialogueNode& Node : Nodes)
	{
		if (Node.NodeId.IsNone())
		{
			Fail(Context, LOCTEXT("NoNodeId", "NodeId 가 빈 노드가 있다 - 어디서도 가리킬 수 없다."));
		}
		else if (NodeIds.Contains(Node.NodeId))
		{
			Fail(Context, FText::Format(LOCTEXT("DupNode", "노드 {0} 이 두 번 있다 - 연결이 앞의 것만 찾는다."), FText::FromName(Node.NodeId)));
		}
		NodeIds.Add(Node.NodeId);
	}

	auto CheckLink = [&](FName Target, const FText& Where)
	{
		if (!Target.IsNone() && !NodeIds.Contains(Target))
		{
			Fail(Context, FText::Format(LOCTEXT("BadLink", "{0}: 없는 노드 {1} 를 가리킨다."), Where, FText::FromName(Target)));
		}
	};

	for (int32 Index = 0; Index < EntryPoints.Num(); ++Index)
	{
		const FText Where = FText::Format(LOCTEXT("EntryWhere", "진입점 {0}"), Index);
		if (EntryPoints[Index].StartNodeId.IsNone())
		{
			Fail(Context, FText::Format(LOCTEXT("EmptyEntry", "{0}: 시작 노드가 비었다."), Where));
		}
		CheckLink(EntryPoints[Index].StartNodeId, Where);
		if (!VBRules::ValidateStructArray(EntryPoints[Index].Conditions, FVBCondition::StaticStruct(), Where, Context))
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	for (const FVBDialogueNode& Node : Nodes)
	{
		const FText Where = FText::Format(LOCTEXT("NodeWhere", "노드 {0}"), FText::FromName(Node.NodeId));
		if (Node.Speaker.IsValid() && !FindSpeaker(Node.Speaker))
		{
			Fail(Context, FText::Format(LOCTEXT("UnknownSpeaker", "{0}: 화자 {1} 가 Speakers 목록에 없다 - 이름이 비어 보인다."), Where, FText::FromName(Node.Speaker.GetTagName())));
		}
		if (!Node.Choices.IsEmpty() && !Node.NextNodeId.IsNone())
		{
			Context.AddWarning(FText::Format(LOCTEXT("IgnoredNext", "{0}: 선택지가 있어 NextNodeId({1})는 쓰이지 않는다."), Where, FText::FromName(Node.NextNodeId)));
		}
		CheckLink(Node.NextNodeId, Where);
		if (!VBRules::ValidateStructArray(Node.OnEnterEffects, FVBEffect::StaticStruct(), Where, Context))
		{
			Result = EDataValidationResult::Invalid;
		}
		for (int32 ChoiceIndex = 0; ChoiceIndex < Node.Choices.Num(); ++ChoiceIndex)
		{
			const FVBDialogueChoice& Choice = Node.Choices[ChoiceIndex];
			const FText ChoiceWhere = FText::Format(LOCTEXT("ChoiceWhere", "{0} 선택지 {1}"), Where, ChoiceIndex);
			CheckLink(Choice.NextNodeId, ChoiceWhere);
			if (!VBRules::ValidateStructArray(Choice.Conditions, FVBCondition::StaticStruct(), ChoiceWhere, Context)
				|| !VBRules::ValidateStructArray(Choice.Effects, FVBEffect::StaticStruct(), ChoiceWhere, Context))
			{
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	// 노드의 다음 = 실행과 같은 규칙(UVBDialogueComponent): 선택지가 있으면 선택지의 연결만, 없으면 NextNodeId. 빈 이름 = 끝.
	auto ForEachNext = [](const FVBDialogueNode& Node, TFunctionRef<void(FName)> Visit)
	{
		if (Node.Choices.IsEmpty())
		{
			Visit(Node.NextNodeId);
			return;
		}
		for (const FVBDialogueChoice& Choice : Node.Choices)
		{
			Visit(Choice.NextNodeId);
		}
	};

	// 어느 진입점에서도 닿지 않는 노드. 오류가 아니라 경고다 - 쓰다 만 가지일 수 있다.
	TSet<FName> Reached;
	TArray<FName> Frontier;
	for (const FVBDialogueEntryPoint& Entry : EntryPoints)
	{
		Frontier.Add(Entry.StartNodeId);
	}
	while (!Frontier.IsEmpty())
	{
		const FName Current = Frontier.Pop(EAllowShrinking::No);
		if (Current.IsNone() || Reached.Contains(Current))
		{
			continue;
		}
		Reached.Add(Current);
		if (const FVBDialogueNode* Node = FindNode(Current))
		{
			ForEachNext(*Node, [&Frontier](FName Next) { Frontier.Add(Next); });
		}
	}
	for (const FVBDialogueNode& Node : Nodes)
	{
		if (!Node.NodeId.IsNone() && !Reached.Contains(Node.NodeId))
		{
			Context.AddWarning(FText::Format(LOCTEXT("Unreached", "노드 {0} 에는 어느 진입점에서도 닿지 않는다."), FText::FromName(Node.NodeId)));
		}
	}

	// 끝으로 가는 길이 없는 노드(설계 .sw/narrative 3.2절의 «끝나지 않는 고리»). 들어서면 «떠나기» 말고는 대화를 끝낼 수 없다.
	//   끝에 닿을 수 있는 노드를 바깥에서부터 채워 간다. 조건은 보지 않는다 - 전부 고를 수 있다고 친다(조건이 막는 길은 실행 때의 일이다).
	TSet<FName> CanEnd;
	for (bool bGrew = true; bGrew;)
	{
		bGrew = false;
		for (const FVBDialogueNode& Node : Nodes)
		{
			if (Node.NodeId.IsNone() || CanEnd.Contains(Node.NodeId))
			{
				continue;
			}
			bool bEnds = false;
			ForEachNext(Node, [&bEnds, &CanEnd](FName Next) { bEnds |= Next.IsNone() || CanEnd.Contains(Next); });
			if (bEnds)
			{
				CanEnd.Add(Node.NodeId);
				bGrew = true;
			}
		}
	}
	for (const FVBDialogueNode& Node : Nodes)
	{
		if (Reached.Contains(Node.NodeId) && !CanEnd.Contains(Node.NodeId))
		{
			Fail(Context, FText::Format(LOCTEXT("NoEnd", "노드 {0} 에서는 끝으로 가는 길이 없다 - 고리에 갇힌다."), FText::FromName(Node.NodeId)));
		}
	}
	// 부모(UObject)는 NotValidated 를 돌려준다. 여기까지 오류가 없으면 «봤고 멀쩡하다» 로 올린다.
	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif

#undef LOCTEXT_NAMESPACE
