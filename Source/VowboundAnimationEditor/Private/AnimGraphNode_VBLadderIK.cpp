#include "AnimGraphNode_VBLadderIK.h"

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"

bool UAnimGraphNode_VBLadderIK::InsertBefore(UAnimBlueprint* Blueprint, FName TargetNodeName)
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
			if (Cast<UAnimGraphNode_VBLadderIK>(Existing)) return false; // 중복 설치는 거부한다.
			if (Existing->GetFName() == TargetNodeName) Target = Existing;
		}
		UEdGraphPin* Input = Target ? Target->FindPin(TEXT("ComponentPose"), EGPD_Input) : nullptr;
		if (!Input || Input->LinkedTo.Num() != 1) return false;
		UEdGraphPin* Source = Input->LinkedTo[0];
		Blueprint->Modify();
		Graph->Modify();
		Target->Modify();
		Source->GetOwningNode()->Modify();
		UAnimGraphNode_VBLadderIK* Added = NewObject<UAnimGraphNode_VBLadderIK>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Added, false, false);
		Added->CreateNewGuid();
		Added->PostPlacedNewNode();
		Added->AllocateDefaultPins();
		Added->NodePosX = Target->NodePosX - 200;
		Added->NodePosY = Target->NodePosY + 260;
		const UEdGraphSchema* Schema = Graph->GetSchema();
		Input->BreakAllPinLinks();
		if (!Schema->TryCreateConnection(Source, Added->FindPin(TEXT("ComponentPose"), EGPD_Input))
			|| !Schema->TryCreateConnection(Added->FindPin(TEXT("Pose"), EGPD_Output), Input))
		{
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
