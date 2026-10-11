#include "VBLookAtPOIAuthoring.h"
#include "Animation/VBLookAtPOIAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_LinkedAnimGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraphUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/DataAsset.h"
#include "Factories/AnimBlueprintFactory.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

namespace
{
	UEdGraph* AnimGraph(UAnimBlueprint* BP)
	{
		TArray<UEdGraph*> Graphs;
		if (BP) BP->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs) if (Graph->GetFName() == TEXT("AnimGraph")) return Graph;
		return nullptr;
	}
	template<class T> T* AddNode(UEdGraph* Graph)
	{
		T* Node = NewObject<T>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		return Node;
	}
	bool Link(UEdGraph* Graph, UEdGraphPin* A, UEdGraphPin* B)
	{
		return A && B && Graph->GetSchema()->TryCreateConnection(A, B);
	}
}

UAnimBlueprint* UVBLookAtPOIAuthoring::CreateLookAtLayer(UAnimBlueprint* Reference, UDataAsset* Settings, const FString& PackagePath)
{
	UEdGraph* ReferenceGraph = AnimGraph(Reference);
	if (!ReferenceGraph || !Settings || !PackagePath.StartsWith(TEXT("/Game/Vowbound/"))) return nullptr;
	if (UAnimBlueprint* Existing = LoadObject<UAnimBlueprint>(nullptr, *(PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath))))
		return Existing->ParentClass == UVBLookAtPOIAnimInstance::StaticClass() ? Existing : nullptr;
	TSet<UObject*> SourceNodes;
	for (UEdGraphNode* Node : ReferenceGraph->Nodes)
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_ControlRig") || Node->GetFName() == TEXT("K2Node_MakeStruct_0")) SourceNodes.Add(Node);
	if (SourceNodes.Num() != 2) return nullptr;
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->ParentClass = UVBLookAtPOIAnimInstance::StaticClass();
	Factory->TargetSkeleton = Reference->TargetSkeleton;
	UAnimBlueprint* BP = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), CreatePackage(*PackagePath),
		FName(FPackageName::GetLongPackageAssetName(PackagePath)), RF_Public | RF_Standalone, nullptr, GWarn));
	UEdGraph* Graph = AnimGraph(BP);
	if (!Graph) return nullptr;
	FString NodeText;
	FEdGraphUtilities::ExportNodesToText(SourceNodes, NodeText);
	TSet<UEdGraphNode*> Imported;
	FEdGraphUtilities::ImportNodesFromText(Graph, NodeText, Imported);
	UEdGraphNode* Rig = nullptr;
	UAnimGraphNode_Root* Root = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_ControlRig")) Rig = Node;
		if (UAnimGraphNode_Root* Candidate = Cast<UAnimGraphNode_Root>(Node)) Root = Candidate;
	}
	if (!Rig || !Root) return nullptr;
	for (UEdGraphNode* Node : Imported) Node->CreateNewGuid();
	UAnimGraphNode_LinkedInputPose* Input = AddNode<UAnimGraphNode_LinkedInputPose>(Graph);
	Input->AllocateDefaultPins();
	bool bValid = Link(Graph, Input->FindPin(TEXT("Pose"), EGPD_Output), Rig->FindPin(TEXT("Source")))
		&& Link(Graph, Rig->FindPin(TEXT("Pose"), EGPD_Output), Root->FindPin(TEXT("Result")));
	UEdGraphPin* SettingsPin = Rig->FindPin(TEXT("DA Asset"));
	if (!SettingsPin) return nullptr;
	Graph->GetSchema()->TrySetDefaultObject(*SettingsPin, Settings);
	const TPair<FName, FName> Properties[] = { {TEXT("LookAtTarget"), TEXT("LookAt Target")}, {TEXT("bLookAtEnabled"), TEXT("bAlphaBoolEnabled")} };
	for (const auto& Property : Properties)
	{
		UK2Node_VariableGet* Getter = AddNode<UK2Node_VariableGet>(Graph);
		Getter->VariableReference.SetSelfMember(Property.Key);
		Getter->AllocateDefaultPins();
		bValid &= Link(Graph, Getter->FindPin(Property.Key, EGPD_Output), Rig->FindPin(Property.Value));
	}
	const TPair<FName, FName> Functions[] = { {TEXT("GetLookAtWeightOverrides"), TEXT("Ovrrd Weights")}, {TEXT("GetLookAtLimitOverrides"), TEXT("Ovrrd Limits")} };
	for (const auto& Function : Functions)
	{
		UK2Node_CallArrayFunction* Call = AddNode<UK2Node_CallArrayFunction>(Graph);
		Call->SetFromFunction(UVBLookAtPOIAnimInstance::StaticClass()->FindFunctionByName(Function.Key));
		Call->AllocateDefaultPins();
		bValid &= Link(Graph, Call->FindPin(TEXT("Values"), EGPD_Output), Rig->FindPin(Function.Value));
	}
	if (!bValid) return nullptr;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	if (BP->Status == BS_Error || !BP->GeneratedClass) return nullptr;
	CastChecked<UVBLookAtPOIAnimInstance>(BP->GeneratedClass->GetDefaultObject())->LookAtSettings = Settings;
	BP->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(BP);
	return BP;
}

bool UVBLookAtPOIAuthoring::ConfigureLookAtLayerForSkeleton(UAnimBlueprint* Layer, USkeleton* Skeleton, UDataAsset* Settings, FVector BaselineOffset, const TArray<FName>& DrivenBones)
{
	if (!Layer || !Skeleton || !Settings || BaselineOffset.ContainsNaN() || BaselineOffset.IsNearlyZero()
		|| Layer->ParentClass != UVBLookAtPOIAnimInstance::StaticClass()
		|| !Layer->GetPathName().StartsWith(TEXT("/Game/Vowbound/"))) return false;
	UEdGraph* Graph = AnimGraph(Layer);
	if (!Graph) return false;
	const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
	if (!DrivenBones.Contains(FName(TEXT("head")))) return false;
	for (FName Bone : DrivenBones)
		if (Ref.FindBoneIndex(Bone) == INDEX_NONE) return false;
	UEdGraphPin* UseJoint = nullptr;
	UEdGraphPin* Baseline = nullptr;
	UEdGraphPin* SettingsPin = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_ControlRig")) SettingsPin = Node->FindPin(TEXT("DA Asset"));
		if (Node->GetClass()->GetFName() == TEXT("K2Node_MakeStruct"))
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin->PinName.ToString().StartsWith(TEXT("UseJoint_"))) UseJoint = Pin;
				if (Pin->PinName.ToString().StartsWith(TEXT("Vector_"))) Baseline = Pin;
			}
	}
	if (!UseJoint || !Baseline || !SettingsPin) return false;
	FTransform Head = FTransform::Identity;
	for (int32 BoneIndex = Ref.FindBoneIndex(TEXT("head")); BoneIndex != INDEX_NONE; BoneIndex = Ref.GetParentIndex(BoneIndex))
		Head *= Ref.GetRefBonePose()[BoneIndex];
	const FVector Point = Head.GetTranslation() + BaselineOffset;
	Layer->Modify();
	Graph->Modify();
	Layer->TargetSkeleton = Skeleton;
	Graph->GetSchema()->TrySetDefaultValue(*UseJoint, TEXT("false"));
	Graph->GetSchema()->TrySetDefaultValue(*Baseline, FString::Printf(TEXT("%f,%f,%f"), Point.X, Point.Y, Point.Z));
	Graph->GetSchema()->TrySetDefaultObject(*SettingsPin, Settings);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Layer);
	FKismetEditorUtilities::CompileBlueprint(Layer);
	if (Layer->Status == BS_Error || !Layer->GeneratedClass) return false;
	CastChecked<UVBLookAtPOIAnimInstance>(Layer->GeneratedClass->GetDefaultObject())->LookAtSettings = Settings;
	Layer->MarkPackageDirty();
	return true;
}

bool UVBLookAtPOIAuthoring::ConnectLookAtLayer(UAnimBlueprint* Target, UAnimBlueprint* Layer, bool bRequirePoseHistory)
{
	UEdGraph* Graph = AnimGraph(Target);
	if (!Graph || !Layer || Layer->ParentClass != UVBLookAtPOIAnimInstance::StaticClass() || !Layer->GeneratedClass || Target->TargetSkeleton != Layer->TargetSkeleton) return false;
	UAnimGraphNode_Root* Root = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (const UAnimGraphNode_LinkedAnimGraph* Existing = Cast<UAnimGraphNode_LinkedAnimGraph>(Node); Existing && Existing->Node.InstanceClass == Layer->GeneratedClass)
			return InspectLookAtLayer(Target, Layer, bRequirePoseHistory).StartsWith(TEXT("OK"));
		if (UAnimGraphNode_Root* Candidate = Cast<UAnimGraphNode_Root>(Node)) Root = Candidate;
	}
	UEdGraphPin* Destination = Root ? Root->FindPin(TEXT("Result"), EGPD_Input) : nullptr;
	if (!Destination || Destination->LinkedTo.Num() != 1) return false;
	// Preserve final PoseHistory as the consumer of the final procedural pose.
	UEdGraphNode* History = Destination->LinkedTo[0]->GetOwningNode();
	if (History->GetClass()->GetFName() == TEXT("AnimGraphNode_PoseSearchHistoryCollector"))
		Destination = History->FindPin(TEXT("Source"), EGPD_Input);
	else if (bRequirePoseHistory) return false;
	if (!Destination || Destination->LinkedTo.Num() != 1) return false;
	UEdGraphPin* Source = Destination->LinkedTo[0];
	Target->Modify();
	Graph->Modify();
	UAnimGraphNode_LinkedAnimGraph* Linked = AddNode<UAnimGraphNode_LinkedAnimGraph>(Graph);
	// Match editor asset placement: InstanceClass alone leaves FunctionReference
	// empty, so AllocateDefaultPins cannot discover the linked InPose parameter.
	Linked->Node.InstanceClass = Layer->GeneratedClass;
	Linked->NodeComment = TEXT("VBLookAtPOI");
	Linked->AllocateDefaultPins();
	FProperty* InstanceClassProperty = FAnimNode_LinkedAnimGraph::StaticStruct()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(FAnimNode_LinkedAnimGraph, InstanceClass));
	FPropertyChangedEvent ClassChanged(InstanceClassProperty, EPropertyChangeType::ValueSet);
	Linked->PostEditChangeProperty(ClassChanged);
	Linked->NodePosX = Root->NodePosX - 250;
	Linked->NodePosY = Root->NodePosY;
	UEdGraphPin* Input = Linked->FindPin(FAnimNode_LinkedInputPose::DefaultInputPoseName, EGPD_Input);
	if (!Input) { Graph->RemoveNode(Linked); return false; }
	Destination->BreakAllPinLinks();
	if (!Link(Graph, Source, Input) || !Link(Graph, Linked->FindPin(TEXT("Pose"), EGPD_Output), Destination))
	{
		Linked->BreakAllNodeLinks();
		Graph->RemoveNode(Linked);
		Link(Graph, Source, Destination);
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
	FKismetEditorUtilities::CompileBlueprint(Target);
	return Target->Status != BS_Error;
}

FString UVBLookAtPOIAuthoring::InspectLookAtLayer(UAnimBlueprint* Target, UAnimBlueprint* Layer, bool bRequirePoseHistory)
{
	if (!Target || !Layer || Target->Status == BS_Error || Layer->Status == BS_Error
		|| Layer->ParentClass != UVBLookAtPOIAnimInstance::StaticClass() || !Layer->GeneratedClass)
		return TEXT("ERROR: Blueprint class/compile status");
	const auto* Defaults = Cast<UVBLookAtPOIAnimInstance>(Layer->GeneratedClass->GetDefaultObject());
	if (!Defaults || !Defaults->LookAtSettings) return TEXT("ERROR: missing native LookAt settings");
	UEdGraph* LayerGraph = AnimGraph(Layer);
	UEdGraphNode* Rig = nullptr;
	UAnimGraphNode_Root* LayerRoot = nullptr;
	if (LayerGraph) for (UEdGraphNode* Node : LayerGraph->Nodes)
	{
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_ControlRig")) Rig = Node;
		if (UAnimGraphNode_Root* Candidate = Cast<UAnimGraphNode_Root>(Node)) LayerRoot = Candidate;
	}
	UEdGraphPin* RigSource = Rig ? Rig->FindPin(TEXT("Source"), EGPD_Input) : nullptr;
	UEdGraphPin* LayerResult = LayerRoot ? LayerRoot->FindPin(TEXT("Result"), EGPD_Input) : nullptr;
	UEdGraphPin* SettingsPin = Rig ? Rig->FindPin(TEXT("DA Asset"), EGPD_Input) : nullptr;
	if (!RigSource || RigSource->LinkedTo.Num() != 1 || !Cast<UAnimGraphNode_LinkedInputPose>(RigSource->LinkedTo[0]->GetOwningNode())
		|| !LayerResult || LayerResult->LinkedTo.Num() != 1 || LayerResult->LinkedTo[0]->GetOwningNode() != Rig
		|| !SettingsPin || SettingsPin->DefaultObject != Defaults->LookAtSettings)
		return TEXT("ERROR: LookAt layer input/rig/settings/output connection mismatch");
	for (FName Name : { FName(TEXT("LookAt Target")), FName(TEXT("bAlphaBoolEnabled")), FName(TEXT("Ovrrd Weights")), FName(TEXT("Ovrrd Limits")) })
	{
		const UEdGraphPin* Pin = Rig->FindPin(Name, EGPD_Input);
		if (!Pin || Pin->LinkedTo.Num() != 1) return TEXT("ERROR: missing native LookAt input: ") + Name.ToString();
	}
	UEdGraph* Graph = AnimGraph(Target);
	if (!Graph) return TEXT("ERROR: missing main AnimGraph");
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Cast<UAnimGraphNode_Root>(Node)) continue;
		UEdGraphPin* RootInput = Node->FindPin(TEXT("Result"), EGPD_Input);
		if (!RootInput || RootInput->LinkedTo.Num() != 1) break;
		UEdGraphNode* History = RootInput->LinkedTo[0]->GetOwningNode();
		const bool bHasHistory = History->GetClass()->GetFName() == TEXT("AnimGraphNode_PoseSearchHistoryCollector");
		if (!bHasHistory && bRequirePoseHistory) break;
		UEdGraphPin* HistoryInput = bHasHistory ? History->FindPin(TEXT("Source"), EGPD_Input) : RootInput;
		if (!HistoryInput || HistoryInput->LinkedTo.Num() != 1) break;
		const UAnimGraphNode_LinkedAnimGraph* Linked = Cast<UAnimGraphNode_LinkedAnimGraph>(HistoryInput->LinkedTo[0]->GetOwningNode());
		if (!Linked || Linked->Node.InstanceClass != Layer->GeneratedClass) break;
		const UEdGraphPin* LayerInput = Linked->FindPin(FAnimNode_LinkedInputPose::DefaultInputPoseName, EGPD_Input);
		if (!LayerInput || LayerInput->LinkedTo.Num() != 1) break;
		return TEXT("OK: ") + LayerInput->LinkedTo[0]->GetOwningNode()->GetName()
			+ TEXT(" -> ") + Layer->GetPathName() + TEXT(" -> ")
			+ (bHasHistory ? History->GetName() + TEXT(" -> ") : FString())
			+ TEXT("Root; settings=") + Defaults->LookAtSettings->GetPathName();
	}
	return TEXT("ERROR: linked input/LookAt/final PoseHistory/Root connection mismatch");
}
