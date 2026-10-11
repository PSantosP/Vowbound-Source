#include "VBInteractionGraphAuthoring.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/VBAnimNotify_InteractionRagdoll.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_Inertialization.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

bool UVBInteractionGraphAuthoring::InstallInteractionRig(UAnimBlueprint* Blueprint,
                                                        UAnimBlueprint* ReferenceBlueprint,
                                                        FName TargetNodeName)
{
	if (!Blueprint || !ReferenceBlueprint || !Blueprint->ParentClass) return false;
	UEdGraph* TargetGraph = nullptr;
	UEdGraph* ReferenceGraph = nullptr;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetFName() == TEXT("AnimGraph")) TargetGraph = Graph;
	}
	Graphs.Reset();
	ReferenceBlueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetFName() == TEXT("Procedural_FullBody")) ReferenceGraph = Graph;
	}
	if (!TargetGraph || !ReferenceGraph) return false;

	UEdGraphNode* Target = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeComment == TEXT("VBInteractionFullBodyIK")) return false;
		if (Node->GetFName() == TargetNodeName) Target = Node;
	}
	UEdGraphPin* Input = Target ? Target->FindPin(TEXT("Source"), EGPD_Input) : nullptr;
	if (!Input || Input->LinkedTo.Num() != 1) return false;
	UEdGraphPin* Source = Input->LinkedTo[0];

	TSet<UObject*> Exported;
	for (UEdGraphNode* Node : ReferenceGraph->Nodes)
	{
		const FString ClassName = Node->GetClass()->GetName();
		if (ClassName != TEXT("AnimGraphNode_Root") && ClassName != TEXT("AnimGraphNode_LinkedInputPose")
		    && ClassName != TEXT("EdGraphNode_Comment")) Exported.Add(Node);
	}
	FString Text;
	FEdGraphUtilities::ExportNodesToText(Exported, Text);
	if (!FEdGraphUtilities::CanImportNodesFromText(TargetGraph, Text)) return false;
	Blueprint->Modify();
	TargetGraph->Modify();
	TSet<UEdGraphNode*> Imported;
	FEdGraphUtilities::ImportNodesFromText(TargetGraph, Text, Imported);
	TArray<UEdGraphNode*> Added = Imported.Array();
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	bool bValid = true;
	UAnimGraphNode_SaveCachedPose* Cache = nullptr;
	UEdGraphNode* Blend = nullptr;
	int32 ReplacedVariables = 0;
	int32 RigCount = 0;
	for (UEdGraphNode* Node : Imported)
	{
		Node->CreateNewGuid();
		Node->NodePosX += Target->NodePosX - 500;
		Node->NodePosY += Target->NodePosY + 350;
		if (UAnimGraphNode_SaveCachedPose* Save = Cast<UAnimGraphNode_SaveCachedPose>(Node))
		{
			Cache = Save;
			Save->CacheName = TEXT("VBInteractionInput");
		}
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_BlendListByBool")) Blend = Node;
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_ControlRig")) ++RigCount;
		UK2Node_VariableGet* Variable = Cast<UK2Node_VariableGet>(Node);
		if (!Variable) continue;
		FString Name = Variable->GetVarName().ToString().ToLower();
		Name.ReplaceInline(TEXT("_"), TEXT(""));
		Name.ReplaceInline(TEXT(" "), TEXT(""));
		FName FunctionName;
		if (Name == TEXT("enablemmiik")) FunctionName = TEXT("IsInteractionIKEnabled");
		else if (Name == TEXT("mmiikhandlalpha")) FunctionName = TEXT("GetInteractionIKLeftAlpha");
		else if (Name == TEXT("mmiikhandralpha")) FunctionName = TEXT("GetInteractionIKRightAlpha");
		else if (Name == TEXT("mmiikhandltarget")) FunctionName = TEXT("GetInteractionIKLeftTarget");
		else if (Name == TEXT("mmiikhandrtarget")) FunctionName = TEXT("GetInteractionIKRightTarget");
		UFunction* Function = Blueprint->ParentClass->FindFunctionByName(FunctionName);
		if (!Function) { bValid = false; break; }
		UK2Node_CallFunction* Getter = NewObject<UK2Node_CallFunction>(TargetGraph, NAME_None, RF_Transactional);
		TargetGraph->AddNode(Getter, false, false);
		Added.Add(Getter);
		Getter->CreateNewGuid();
		Getter->SetFromFunction(Function);
		Getter->AllocateDefaultPins();
		Getter->NodePosX = Variable->NodePosX;
		Getter->NodePosY = Variable->NodePosY;
		UEdGraphPin* OldOutput = Variable->FindPin(Variable->GetVarName(), EGPD_Output);
		UEdGraphPin* NewOutput = Getter->FindPin(TEXT("ReturnValue"), EGPD_Output);
		if (!OldOutput || !NewOutput) { bValid = false; break; }
		const TArray<UEdGraphPin*> Consumers = OldOutput->LinkedTo;
		OldOutput->BreakAllPinLinks();
		for (UEdGraphPin* Consumer : Consumers)
		{
			bValid &= Schema->TryCreateConnection(NewOutput, Consumer);
		}
		Variable->BreakAllNodeLinks();
		TargetGraph->RemoveNode(Variable);
		Added.Remove(Variable);
		++ReplacedVariables;
	}
	UEdGraphPin* CacheInput = Cache ? Cache->FindPin(TEXT("Pose"), EGPD_Input) : nullptr;
	UEdGraphPin* Output = Blend ? Blend->FindPin(TEXT("Pose"), EGPD_Output) : nullptr;
	bValid &= CacheInput && Output && RigCount == 1 && ReplacedVariables == 5;
	if (bValid)
	{
		for (UEdGraphNode* Node : Added)
		{
			if (UAnimGraphNode_UseCachedPose* Use = Cast<UAnimGraphNode_UseCachedPose>(Node))
			{
				Use->SaveCachedPoseNode = Cache;
			}
		}
		Source->GetOwningNode()->Modify();
		Target->Modify();
		Input->BreakAllPinLinks();
		bValid = Schema->TryCreateConnection(Source, CacheInput) && Schema->TryCreateConnection(Output, Input);
	}
	if (!bValid)
	{
		for (UEdGraphNode* Node : Added)
		{
			Node->BreakAllNodeLinks();
			TargetGraph->RemoveNode(Node);
		}
		Schema->TryCreateConnection(Source, Input);
		return false;
	}
	Blend->NodeComment = TEXT("VBInteractionFullBodyIK");
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

int32 UVBInteractionGraphAuthoring::ReplaceInteractionRagdollNotifies(UAnimSequenceBase* Animation)
{
	if (!Animation) return 0;
	int32 Replaced = 0;
	for (FAnimNotifyEvent& Event : Animation->Notifies)
	{
		if (!Event.Notify || Event.Notify->GetClass()->GetFName() != TEXT("BP_AnimNotify_TriggerRagdoll_C")) continue;
		Animation->Modify();
		Event.Notify = NewObject<UVBAnimNotify_InteractionRagdoll>(Animation, NAME_None, RF_Transactional);
		++Replaced;
	}
	if (Replaced > 0)
	{
		Animation->RefreshCacheData(); // native branching-point markers must also update in the loaded montage.
		Animation->MarkPackageDirty();
	}
	return Replaced;
}

TArray<float> UVBInteractionGraphAuthoring::GetInteractionRagdollNotifyTimes(UAnimSequenceBase* Animation)
{
	TArray<float> Times;
	if (Animation)
	{
		for (const FAnimNotifyEvent& Event : Animation->Notifies)
		{
			if (Cast<UVBAnimNotify_InteractionRagdoll>(Event.Notify)) Times.Add(Event.GetTriggerTime());
		}
	}
	return Times;
}

TArray<FName> UVBInteractionGraphAuthoring::GetPhysicsBodyNames(USkeletalMesh* Mesh)
{
	TArray<FName> Names;
	if (Mesh && Mesh->GetPhysicsAsset())
	{
		for (const USkeletalBodySetup* Body : Mesh->GetPhysicsAsset()->SkeletalBodySetups)
		{
			if (Body) Names.Add(Body->BoneName);
		}
	}
	return Names;
}

bool UVBInteractionGraphAuthoring::EnsureFinalInertialization(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) return false;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph || Graph->GetFName() != TEXT("AnimGraph")) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->GetClass()->GetFName() != TEXT("AnimGraphNode_Root")) continue;
			UEdGraphPin* Input = Node->FindPin(TEXT("Result"), EGPD_Input);
			if (!Input || Input->LinkedTo.Num() != 1) return false;
			UEdGraphPin* Source = Input->LinkedTo[0];
			if (Cast<UAnimGraphNode_Inertialization>(Source->GetOwningNode())) return true;
			Blueprint->Modify(); Graph->Modify(); Node->Modify(); Source->GetOwningNode()->Modify();
			auto* Inertia = NewObject<UAnimGraphNode_Inertialization>(Graph, NAME_None, RF_Transactional);
			Graph->AddNode(Inertia, false, false);
			Inertia->CreateNewGuid();
			Inertia->PostPlacedNewNode();
			Inertia->AllocateDefaultPins();
			Inertia->NodeComment = TEXT("VBFinalMontageInertialization");
			Inertia->NodePosX = Node->NodePosX - 150;
			Inertia->NodePosY = Node->NodePosY + 150;
			Input->BreakAllPinLinks();
			const UEdGraphSchema* Schema = Graph->GetSchema();
			if (!Schema->TryCreateConnection(Source, Inertia->FindPin(TEXT("Source"), EGPD_Input)) ||
				!Schema->TryCreateConnection(Inertia->FindPin(TEXT("Pose"), EGPD_Output), Input))
			{
				Inertia->BreakAllNodeLinks(); Graph->RemoveNode(Inertia);
				Schema->TryCreateConnection(Source, Input);
				return false;
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return true;
		}
	}
	return false;
}
