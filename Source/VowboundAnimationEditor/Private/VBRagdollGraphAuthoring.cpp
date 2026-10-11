#include "VBRagdollGraphAuthoring.h"

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphUtilities.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimationStateMachineGraph.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimStateTransitionNode.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

FString UVBRagdollGraphAuthoring::DescribeRagdollGraphs(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) return TEXT("Invalid AnimBlueprint");
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	FString Result;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph || !Graph->GetPathName().Contains(TEXT("Ragdoll"))) continue;
		Result += FString::Printf(TEXT("\nGRAPH %s\n"), *Graph->GetPathName());
		TSet<UObject*> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node) Nodes.Add(Node);
		}
		FString Exported;
		FEdGraphUtilities::ExportNodesToText(Nodes, Exported);
		Result += Exported;
	}
	return Result;
}

bool UVBRagdollGraphAuthoring::InstallRagdollProducer(UAnimBlueprint* Blueprint, UAnimBlueprint* ReferenceBlueprint)
{
	if (!Blueprint || !ReferenceBlueprint || !Blueprint->ParentClass) return false;
	UFunction* IsPowered = Blueprint->ParentClass->FindFunctionByName(TEXT("IsPoweredRagdollPose"));
	UFunction* Elapsed = Blueprint->ParentClass->FindFunctionByName(TEXT("GetRagdollElapsedTime"));
	if (!IsPowered || !Elapsed || !FindFProperty<FStructProperty>(Blueprint->ParentClass, TEXT("RagdollState"))) return false;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	UEdGraph* TargetGraph = nullptr;
	UEdGraphPin* Input = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph->GetFName() == TEXT("AnimGraph")) TargetGraph = Graph;
	}
	if (!TargetGraph) return false;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeComment == TEXT("VBPoweredRagdollProducer")) return false;
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_Root")) Input = Node->FindPin(TEXT("Result"), EGPD_Input);
	}
	if (!Input || Input->LinkedTo.Num() != 1) return false;
	UEdGraphPin* PreviousPose = Input->LinkedTo[0];
	Graphs.Reset();
	ReferenceBlueprint->GetAllGraphs(Graphs);
	UAnimGraphNode_StateMachine* ReferenceMachine = nullptr;
	float EntryBlendTime = -1.0f;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph->GetFName() == TEXT("In Ragdoll"))
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UAnimGraphNode_StateMachine* Machine = Cast<UAnimGraphNode_StateMachine>(Node)) ReferenceMachine = Machine;
			}
		}
		if (Graph->GetFName() == TEXT("Ragdoll"))
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node))
				{
					if (Transition->GetNodeTitle(ENodeTitleType::ListView).ToString().EndsWith(TEXT("to In Ragdoll"))) EntryBlendTime = Transition->CrossfadeDuration;
				}
			}
		}
	}
	if (!ReferenceMachine || EntryBlendTime < 0.0f) return false;
	TSet<UObject*> Exported = { ReferenceMachine };
	FString Text;
	FEdGraphUtilities::ExportNodesToText(Exported, Text);
	// Source-authored poses, curve values, timing and all state rules stay intact.
	// Only the property/enum contract changes from the sample BP to the native game state.
	Text.ReplaceInline(TEXT("RagdollProperties"), TEXT("RagdollState"));
	Text.ReplaceInline(TEXT("Ragdoll_ImpactDirection_8_CA74C6034DC9E05115DDA89B2581E1A0"), TEXT("ImpactDirection"));
	Text.ReplaceInline(TEXT("Ragdoll_TimeToImpact_5_40092CEF4820F42C2BC7CEA8B36062E3"), TEXT("TimeToImpact"));
	Text.ReplaceInline(TEXT("Ragdoll_RollAmount_16_9E403C5646E11255E74786A5BD279AE8"), TEXT("RollAmount"));
	Text.ReplaceInline(TEXT("Ragdoll_InjuryState_11_9A4EBBEC4D59B4CB035AB2BD3B7307E4"), TEXT("InjuryState"));
	Text.ReplaceInline(TEXT("/Script/Engine.UserDefinedEnum'/Game/Blueprints/Data/E_RagdollInjuryState.E_RagdollInjuryState'"), TEXT("/Script/CoreUObject.Enum'/Script/Vowbound.EVBRagdollInjuryState'"));
	Text.ReplaceInline(TEXT("E_RagdollInjuryState::"), TEXT("EVBRagdollInjuryState::"));
	const TCHAR* InjuryNames[] = {TEXT("None"), TEXT("HeadFace"), TEXT("HeadBack"), TEXT("Stunned"), TEXT("BodyFront"), TEXT("Limp"), TEXT("Groin")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(InjuryNames); ++Index)
	{
		Text.ReplaceInline(*FString::Printf(TEXT("NewEnumerator%d"), Index), InjuryNames[Index]);
	}
	if (!FEdGraphUtilities::CanImportNodesFromText(TargetGraph, Text)) return false;
	Blueprint->Modify();
	TargetGraph->Modify();
	TSet<UEdGraphNode*> Imported;
	FEdGraphUtilities::ImportNodesFromText(TargetGraph, Text, Imported);
	TArray<UEdGraphNode*> Added = Imported.Array();
	UAnimGraphNode_StateMachine* Machine = nullptr;
	for (UEdGraphNode* Node : Added)
	{
		Node->CreateNewGuid();
		if (UAnimGraphNode_StateMachine* Candidate = Cast<UAnimGraphNode_StateMachine>(Node)) Machine = Candidate;
	}
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	bool bValid = Machine != nullptr;
	int32 ElapsedGetters = 0;
	if (Machine)
	{
		TArray<UObject*> Children;
		GetObjectsWithOuter(Machine, Children, true);
		for (UObject* Child : Children)
		{
			UK2Node_AnimGetter* OldGetter = Cast<UK2Node_AnimGetter>(Child);
			if (!OldGetter) continue;
			if (OldGetter->FunctionReference.GetMemberName() != TEXT("GetInstanceCurrentStateElapsedTime")) { bValid = false; break; }
			UEdGraph* Graph = OldGetter->GetGraph();
			UK2Node_CallFunction* Getter = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
			Graph->AddNode(Getter, false, false);
			Getter->CreateNewGuid();
			Getter->SetFromFunction(Elapsed);
			Getter->AllocateDefaultPins();
			Getter->NodePosX = OldGetter->NodePosX;
			Getter->NodePosY = OldGetter->NodePosY;
			UEdGraphPin* OldOutput = OldGetter->FindPin(TEXT("ReturnValue"), EGPD_Output);
			UEdGraphPin* NewOutput = Getter->FindPin(TEXT("ReturnValue"), EGPD_Output);
			if (!OldOutput || !NewOutput) { bValid = false; break; }
			const TArray<UEdGraphPin*> Consumers = OldOutput->LinkedTo;
			OldOutput->BreakAllPinLinks();
			for (UEdGraphPin* Consumer : Consumers) bValid &= Graph->GetSchema()->TryCreateConnection(NewOutput, Consumer);
			OldGetter->BreakAllNodeLinks();
			Graph->RemoveNode(OldGetter);
			++ElapsedGetters;
		}
	}
	bValid &= ElapsedGetters == 1;
	UAnimGraphNode_BlendListByBool* Blend = NewObject<UAnimGraphNode_BlendListByBool>(TargetGraph, NAME_None, RF_Transactional);
	TargetGraph->AddNode(Blend, false, false);
	Added.Add(Blend);
	Blend->CreateNewGuid();
	Blend->PostPlacedNewNode();
	Blend->AllocateDefaultPins();
	UK2Node_CallFunction* Active = NewObject<UK2Node_CallFunction>(TargetGraph, NAME_None, RF_Transactional);
	TargetGraph->AddNode(Active, false, false);
	Added.Add(Active);
	Active->CreateNewGuid();
	Active->SetFromFunction(IsPowered);
	Active->AllocateDefaultPins();
	UEdGraphPin* TruePose = Blend->FindPin(TEXT("BlendPose_0"), EGPD_Input);
	UEdGraphPin* FalsePose = Blend->FindPin(TEXT("BlendPose_1"), EGPD_Input);
	UEdGraphPin* BlendOutput = Blend->FindPin(TEXT("Pose"), EGPD_Output);
	UEdGraphPin* MachineOutput = Machine ? Machine->FindPin(TEXT("Pose"), EGPD_Output) : nullptr;
	UEdGraphPin* ActiveOutput = Active->FindPin(TEXT("ReturnValue"), EGPD_Output);
	UEdGraphPin* ActiveInput = Blend->FindPin(TEXT("bActiveValue"), EGPD_Input);
	bValid &= TruePose && FalsePose && BlendOutput && MachineOutput && ActiveOutput && ActiveInput;
	if (bValid)
	{
		if (UEdGraphPin* Time = Blend->FindPin(TEXT("BlendTime_0"), EGPD_Input)) Schema->TrySetDefaultValue(*Time, FString::SanitizeFloat(EntryBlendTime));
		if (UEdGraphPin* Time = Blend->FindPin(TEXT("BlendTime_1"), EGPD_Input)) Schema->TrySetDefaultValue(*Time, TEXT("0.0"));
		Input->GetOwningNode()->Modify();
		PreviousPose->GetOwningNode()->Modify();
		Input->BreakAllPinLinks();
		bValid = Schema->TryCreateConnection(MachineOutput, TruePose) && Schema->TryCreateConnection(PreviousPose, FalsePose)
			&& Schema->TryCreateConnection(ActiveOutput, ActiveInput) && Schema->TryCreateConnection(BlendOutput, Input);
	}
	if (!bValid)
	{
		if (Machine && Machine->EditorStateMachineGraph)
		{
			FBlueprintEditorUtils::RemoveGraph(Blueprint, Machine->EditorStateMachineGraph, EGraphRemoveFlags::None);
			Machine->EditorStateMachineGraph = nullptr;
		}
		for (UEdGraphNode* Node : Added) { Node->BreakAllNodeLinks(); TargetGraph->RemoveNode(Node); }
		Schema->TryCreateConnection(PreviousPose, Input);
		return false;
	}
	Blend->NodeComment = TEXT("VBPoweredRagdollProducer");
	Blend->NodePosX = Input->GetOwningNode()->NodePosX - 300;
	Blend->NodePosY = Input->GetOwningNode()->NodePosY;
	Machine->NodePosX = Blend->NodePosX - 350;
	Machine->NodePosY = Blend->NodePosY + 300;
	Active->NodePosX = Blend->NodePosX - 350;
	Active->NodePosY = Blend->NodePosY - 150;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

int32 UVBRagdollGraphAuthoring::RepairGeneratedNodeBindings(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) return 0;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	int32 Repaired = 0;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph || Graph->GetFName() != TEXT("AnimGraph")) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->NodeComment != TEXT("VBPoweredRagdollProducer") &&
				Node->NodeComment != TEXT("VBFinalMontageInertialization")) continue;
			UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
			if (!AnimNode || AnimNode->GetBinding()) continue;
			Blueprint->Modify();
			Graph->Modify();
			AnimNode->Modify();
			// UE5.8은 여기서 Binding과 compiler extension을 만든다.
			// 핀 연결만 있으면 non-pose 입력의 exposed-value handler가 생성되지 않는다.
			AnimNode->PostPlacedNewNode();
			if (AnimNode->GetBinding()) ++Repaired;
		}
	}
	if (Repaired > 0) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Repaired;
}
