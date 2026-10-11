#include "AnimGraphNode_VBMuteEffects.h"

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"

bool UAnimGraphNode_VBMuteEffects::InsertOnPin(UAnimBlueprint* Blueprint, FName TargetNodeName, FName TargetPinName)
{
	if (!Blueprint) return false;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph->GetFName() != TEXT("AnimGraph")) continue;
		UEdGraphNode* Target = nullptr;
		for (UEdGraphNode* Existing : Graph->Nodes)
		{
			if (Cast<UAnimGraphNode_VBMuteEffects>(Existing)) return false; // 중복 설치는 거부한다.
			if (Existing->GetFName() == TargetNodeName) Target = Existing;
		}
		UEdGraphPin* Input = Target ? Target->FindPin(TargetPinName, EGPD_Input) : nullptr;
		if (!Input || Input->LinkedTo.Num() != 1) return false;
		UEdGraphPin* Source = Input->LinkedTo[0];
		Blueprint->Modify();
		Graph->Modify();
		Target->Modify();
		Source->GetOwningNode()->Modify();
		UAnimGraphNode_VBMuteEffects* Added = NewObject<UAnimGraphNode_VBMuteEffects>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Added, false, false);
		Added->CreateNewGuid();
		Added->PostPlacedNewNode();
		Added->AllocateDefaultPins();
		Added->NodePosX = Target->NodePosX - 220;
		Added->NodePosY = Target->NodePosY + 160;
		const UEdGraphSchema* Schema = Graph->GetSchema();
		Input->BreakAllPinLinks();
		if (!Schema->TryCreateConnection(Source, Added->FindPin(TEXT("Source"), EGPD_Input))
			|| !Schema->TryCreateConnection(Added->FindPin(TEXT("Pose"), EGPD_Output), Input))
		{
			// 실패하면 원래 선을 되살리고 넣은 노드를 뺀다 - 반쯤 끊긴 그래프를 남기지 않는다.
			Added->BreakAllNodeLinks();
			Graph->RemoveNode(Added);
			Schema->TryCreateConnection(Source, Input);
			return false;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return true;
	}
	return false;
}
