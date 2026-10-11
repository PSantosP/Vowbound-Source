#include "VBRagdollGraphAuthoring.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBRagdollGetUpTypes.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_PoseSnapshot.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_TransitionResult.h"
#include "EdGraphUtilities.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "Chooser.h"
#include "ObjectChooser_Asset.h"
#include "BoolColumn.h"
#include "EnumColumn.h"
#include "FloatRangeColumn.h"
#include "PoseSearch/Chooser/PoseSearchChooserColumn.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchIndex.h"

namespace
{
	UEdGraph* MainGraph(UAnimBlueprint* BP)
	{
		TArray<UEdGraph*> Graphs;
		BP->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs) if (Graph->GetFName() == TEXT("AnimGraph")) return Graph;
		return nullptr;
	}

	UK2Node_CallFunction* AddGetter(UEdGraph* Graph, UFunction* Function)
	{
		if (!Function) return nullptr;
		UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		Node->SetFromFunction(Function);
		Node->AllocateDefaultPins();
		return Node;
	}
}

bool UVBRagdollGraphAuthoring::InstallGASPRecovery(UAnimBlueprint* Blueprint, UAnimBlueprint* ReferenceBlueprint)
{
	if (!Blueprint || !ReferenceBlueprint || !Blueprint->ParentClass) return false;
	UEdGraph* Graph = MainGraph(Blueprint);
	UEdGraph* ReferenceGraph = MainGraph(ReferenceBlueprint);
	if (!Graph || !ReferenceGraph) return false;
	UAnimGraphNode_StateMachine* ReferenceMachine = nullptr;
	UAnimGraphNode_Base* ReferenceHistory = nullptr;
	for (UEdGraphNode* Node : ReferenceGraph->Nodes)
	{
		if (UAnimGraphNode_StateMachine* Machine = Cast<UAnimGraphNode_StateMachine>(Node))
			if (Machine->EditorStateMachineGraph->GetFName() == TEXT("Ragdoll")) ReferenceMachine = Machine;
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_PoseSearchHistoryCollector")) ReferenceHistory = Cast<UAnimGraphNode_Base>(Node);
	}
	UEdGraphNode* Slot = nullptr;
	UEdGraphNode* Root = nullptr;
	UEdGraphNode* OldBlend = nullptr;
	UEdGraphNode* Inertial = nullptr;
	UAnimGraphNode_Base* History = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node->NodeComment == TEXT("VBGASPRecovery")) return true;
		if (UAnimGraphNode_Slot* Candidate = Cast<UAnimGraphNode_Slot>(Node))
		{
			if (Candidate->Node.SlotName == TEXT("DefaultSlot"))
			{
				if (Slot) return false;
				Slot = Candidate;
			}
		}
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_Root")) Root = Node;
		if (Node->NodeComment == TEXT("VBPoweredRagdollProducer")) OldBlend = Node;
		if (Node->NodeComment == TEXT("VBFinalMontageInertialization")) Inertial = Node;
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_PoseSearchHistoryCollector")) History = Cast<UAnimGraphNode_Base>(Node);
	}
	if (!ReferenceMachine || !ReferenceHistory || !Slot || !Root || !OldBlend || !Inertial || !History) return false;
	UEdGraphPin* SlotInput = Slot->FindPin(TEXT("Source"), EGPD_Input);
	UEdGraphPin* HistoryInput = History->FindPin(TEXT("Source"), EGPD_Input);
	UEdGraphPin* OldTrue = OldBlend->FindPin(TEXT("BlendPose_0"), EGPD_Input);
	if (!SlotInput || SlotInput->LinkedTo.Num() != 1 || !HistoryInput || HistoryInput->LinkedTo.Num() != 1 || !OldTrue || OldTrue->LinkedTo.Num() != 1) return false;
	UEdGraphPin* BasePose = SlotInput->LinkedTo[0];
	UEdGraphPin* PostIKPose = HistoryInput->LinkedTo[0];
	UAnimGraphNode_StateMachine* OldMachine = Cast<UAnimGraphNode_StateMachine>(OldTrue->LinkedTo[0]->GetOwningNode());
	if (!OldMachine) return false;

	FString Text;
	FEdGraphUtilities::ExportNodesToText(TSet<UObject*>{ReferenceMachine}, Text);
	Text.ReplaceInline(TEXT("RagdollProperties"), TEXT("RagdollState"));
	Text.ReplaceInline(TEXT("Ragdoll_ImpactDirection_8_CA74C6034DC9E05115DDA89B2581E1A0"), TEXT("ImpactDirection"));
	Text.ReplaceInline(TEXT("Ragdoll_TimeToImpact_5_40092CEF4820F42C2BC7CEA8B36062E3"), TEXT("TimeToImpact"));
	Text.ReplaceInline(TEXT("Ragdoll_RollAmount_16_9E403C5646E11255E74786A5BD279AE8"), TEXT("RollAmount"));
	Text.ReplaceInline(TEXT("Ragdoll_InjuryState_11_9A4EBBEC4D59B4CB035AB2BD3B7307E4"), TEXT("InjuryState"));
	Text.ReplaceInline(TEXT("/Script/Engine.UserDefinedEnum'/Game/Blueprints/Data/E_RagdollInjuryState.E_RagdollInjuryState'"), TEXT("/Script/CoreUObject.Enum'/Script/Vowbound.EVBRagdollInjuryState'"));
	Text.ReplaceInline(TEXT("E_RagdollInjuryState::"), TEXT("EVBRagdollInjuryState::"));
	const TCHAR* InjuryNames[] = {TEXT("None"), TEXT("HeadFace"), TEXT("HeadBack"), TEXT("Stunned"), TEXT("BodyFront"), TEXT("Limp"), TEXT("Groin")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(InjuryNames); ++Index)
		Text.ReplaceInline(*FString::Printf(TEXT("NewEnumerator%d"), Index), InjuryNames[Index]);
	// Outer MovementMode rules are replaced below before compilation.
	if (!FEdGraphUtilities::CanImportNodesFromText(Graph, Text)) return false;
	Blueprint->Modify();
	Graph->Modify();
	TSet<UEdGraphNode*> Imported;
	FEdGraphUtilities::ImportNodesFromText(Graph, Text, Imported);
	UAnimGraphNode_StateMachine* Machine = nullptr;
	for (UEdGraphNode* Node : Imported)
		if (UAnimGraphNode_StateMachine* Candidate = Cast<UAnimGraphNode_StateMachine>(Node)) Machine = Candidate;
	if (!Machine) return false;
	Machine->CreateNewGuid();
	Machine->NodeComment = TEXT("VBGASPRecovery");
	Machine->NodePosX = Slot->NodePosX - 360;
	Machine->NodePosY = Slot->NodePosY + 450;
	UAnimGraphNode_SaveCachedPose* Cache = NewObject<UAnimGraphNode_SaveCachedPose>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Cache, false, false);
	Cache->CreateNewGuid();
	Cache->PostPlacedNewNode();
	Cache->CacheName = TEXT("VBPreRagdoll");
	Cache->AllocateDefaultPins();

	int32 Rules = 0;
	for (UEdGraphNode* Node : Machine->EditorStateMachineGraph->Nodes)
	{
		UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node);
		if (!Transition) continue;
		UEdGraph* Rule = Transition->BoundGraph;
		UAnimGraphNode_TransitionResult* Result = nullptr;
		const TArray<TObjectPtr<UEdGraphNode>> Nodes = Rule->Nodes;
		for (UEdGraphNode* RuleNode : Nodes)
		{
			if (UAnimGraphNode_TransitionResult* Candidate = Cast<UAnimGraphNode_TransitionResult>(RuleNode)) Result = Candidate;
			else { RuleNode->BreakAllNodeLinks(); Rule->RemoveNode(RuleNode); }
		}
		const bool bEnter = Transition->GetNodeTitle(ENodeTitleType::ListView).ToString().EndsWith(TEXT("to In Ragdoll"));
		UK2Node_CallFunction* Getter = AddGetter(Rule, Blueprint->ParentClass->FindFunctionByName(bEnter ? TEXT("IsPoweredRagdollPose") : TEXT("IsNotPoweredRagdollPose")));
		if (!Result || !Getter || !Rule->GetSchema()->TryCreateConnection(Getter->FindPin(TEXT("ReturnValue"), EGPD_Output), Result->FindPin(TEXT("bCanEnterTransition"), EGPD_Input))) return false;
		++Rules;
	}
	int32 CachedPoses = 0;
	int32 ElapsedGetters = 0;
	TArray<UObject*> Children;
	GetObjectsWithOuter(Machine, Children, true);
	for (UObject* Child : Children)
	{
		if (UAnimGraphNode_UseCachedPose* Use = Cast<UAnimGraphNode_UseCachedPose>(Child))
		{
			Use->SaveCachedPoseNode = Cache;
			++CachedPoses;
		}
		UK2Node_AnimGetter* OldGetter = Cast<UK2Node_AnimGetter>(Child);
		if (!OldGetter) continue;
		if (OldGetter->FunctionReference.GetMemberName() != TEXT("GetInstanceCurrentStateElapsedTime")) return false;
		UEdGraph* SubGraph = OldGetter->GetGraph();
		UK2Node_CallFunction* Getter = AddGetter(SubGraph, Blueprint->ParentClass->FindFunctionByName(TEXT("GetRagdollElapsedTime")));
		if (!Getter) return false;
		UEdGraphPin* OldOutput = OldGetter->FindPin(TEXT("ReturnValue"), EGPD_Output);
		const TArray<UEdGraphPin*> Consumers = OldOutput->LinkedTo;
		OldOutput->BreakAllPinLinks();
		for (UEdGraphPin* Consumer : Consumers)
			if (!SubGraph->GetSchema()->TryCreateConnection(Getter->FindPin(TEXT("ReturnValue"), EGPD_Output), Consumer)) return false;
		OldGetter->BreakAllNodeLinks();
		SubGraph->RemoveNode(OldGetter);
		++ElapsedGetters;
	}
	if (Rules != 3 || CachedPoses != 1 || ElapsedGetters != 1) return false;

	FStructProperty* NodeProperty = FindFProperty<FStructProperty>(History->GetClass(), TEXT("Node"));
	if (!NodeProperty) return false;
	NodeProperty->CopyCompleteValue(NodeProperty->ContainerPtrToValuePtr<void>(History), NodeProperty->ContainerPtrToValuePtr<void>(ReferenceHistory));
	// The sample feeds its Mover trajectory through a BP binding. Enemy CMC must
	// produce that input too; this history covers the schema's -0.5s root sample.
	FAnimNode_PoseSearchHistoryCollector* HistoryNode = NodeProperty->ContainerPtrToValuePtr<FAnimNode_PoseSearchHistoryCollector>(History);
	HistoryNode->bGenerateTrajectory = true;
	HistoryNode->SamplingInterval = 0.05f;
	HistoryNode->TrajectoryHistoryCount = 20;
	History->SetTag(TEXT("PoseHistory"));
	History->ReconstructNode();
	HistoryInput = History->FindPin(TEXT("Source"), EGPD_Input);
	SlotInput->BreakAllPinLinks();
	HistoryInput->BreakAllPinLinks();
	History->FindPin(TEXT("Pose"), EGPD_Output)->BreakAllPinLinks();
	Inertial->FindPin(TEXT("Source"), EGPD_Input)->BreakAllPinLinks();
	Root->FindPin(TEXT("Result"), EGPD_Input)->BreakAllPinLinks();
	const UEdGraphSchema* Schema = Graph->GetSchema();
	const bool bConnected = Schema->TryCreateConnection(BasePose, Cache->FindPin(TEXT("Pose"), EGPD_Input))
		&& Schema->TryCreateConnection(Machine->FindPin(TEXT("Pose"), EGPD_Output), SlotInput)
		&& Schema->TryCreateConnection(PostIKPose, Inertial->FindPin(TEXT("Source"), EGPD_Input))
		&& Schema->TryCreateConnection(Inertial->FindPin(TEXT("Pose"), EGPD_Output), HistoryInput)
		&& Schema->TryCreateConnection(History->FindPin(TEXT("Pose"), EGPD_Output), Root->FindPin(TEXT("Result"), EGPD_Input));
	if (!bConnected) return false;
	FBlueprintEditorUtils::RemoveGraph(Blueprint, OldMachine->EditorStateMachineGraph, EGraphRemoveFlags::None);
	OldMachine->EditorStateMachineGraph = nullptr;
	OldMachine->BreakAllNodeLinks();
	Graph->RemoveNode(OldMachine);
	OldBlend->BreakAllNodeLinks();
	Graph->RemoveNode(OldBlend);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

FString UVBRagdollGraphAuthoring::BuildAndDescribePoseSearchDatabase(UPoseSearchDatabase* Database, bool bStartBuild)
{
	if (!Database || !Database->Schema) return TEXT("Invalid database/schema");
	using namespace UE::PoseSearch;
	const EAsyncBuildIndexResult Result = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
		bStartBuild ? ERequestAsyncBuildFlag::NewRequest : ERequestAsyncBuildFlag::ContinueRequest);
	if (Result != EAsyncBuildIndexResult::Success) return FString::Printf(TEXT("BuildStatus=%d"), int32(Result));
	const FSearchIndex& Index = Database->GetSearchIndex();
	return FString::Printf(TEXT("Success: Poses=%d SchemaCardinality=%d"), Index.GetNumPoses(), Database->Schema->SchemaCardinality);
}

bool UVBRagdollGraphAuthoring::ConfigureGetUpChooser(UChooserTable* Chooser, const TArray<UAnimMontage*>& Montages, UPoseSearchSchema* Schema)
{
	if (!Chooser || !Schema || Montages.Num() != 4 || Chooser->ResultsStructs.Num() != 4 || Chooser->ColumnsStructs.Num() != 3) return false;
	FEnumColumn* Mode = Chooser->ColumnsStructs[0].GetMutablePtr<FEnumColumn>();
	FBoolColumn* Rolling = Chooser->ColumnsStructs[1].GetMutablePtr<FBoolColumn>();
	FPoseSearchColumn* Pose = Chooser->ColumnsStructs[2].GetMutablePtr<FPoseSearchColumn>();
	if (!Mode || !Rolling || !Pose || Pose->GetNumRows() != 4 || Mode->RowValues.Num() != 4) return false;
	for (UAnimMontage* Montage : Montages) if (!Montage || Montage->GetSkeleton() != Montages[0]->GetSkeleton()) return false;
	UPoseSearchDatabase* Database = Pose->GetDatabase(0);
	if (!Database || Database->GetOuter() != Chooser) return false;
	Chooser->Modify();
	Schema->Modify();
	FPoseSearchRoledSkeleton* Skeleton = Schema->GetRoledSkeleton(Schema->GetDefaultRole());
	if (!Skeleton) return false;
	Skeleton->Skeleton = Montages[0]->GetSkeleton();
	Schema->PostEditChange();
	Chooser->ContextData[1].GetMutable<FContextObjectTypeStruct>().Struct = FVBRagdollGetUpInputs::StaticStruct();
	Chooser->ContextData[2].GetMutable<FContextObjectTypeStruct>().Struct = FVBRagdollGetUpOutputs::StaticStruct();
	FEnumContextProperty& ModeProperty = Mode->InputValue.GetMutable<FEnumContextProperty>();
	ModeProperty.Binding.PropertyBindingChain = {TEXT("MovementMode")};
	ModeProperty.Binding.Enum = StaticEnum<EMovementMode>();
	for (FChooserEnumRowData& Row : Mode->RowValues)
	{
		Row.ValueName = TEXT("MOVE_Walking");
		Row.Value = MOVE_Walking;
	}
	Rolling->InputValue.GetMutable<FBoolContextProperty>().Binding.PropertyBindingChain = {TEXT("RollingGetup")};
	Pose->InputValue.GetMutable<FPoseHistoryContextProperty>().Binding.PropertyBindingChain = {TEXT("PoseHistory")};
	Pose->OutputStartTime.GetMutable<FFloatContextProperty>().Binding.PropertyBindingChain = {TEXT("MontageStartTime")};
	FArrayProperty* RowsProperty = FindFProperty<FArrayProperty>(FPoseSearchColumn::StaticStruct(), TEXT("RowValues"));
	if (!RowsProperty) return false;
	FScriptArrayHelper Rows(RowsProperty, RowsProperty->ContainerPtrToValuePtr<void>(Pose));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Chooser->ResultsStructs[Index].GetMutable<FAssetChooser>().Asset = Montages[Index];
		FPoseSearchColumnRow* Row = reinterpret_cast<FPoseSearchColumnRow*>(Rows.GetRawPtr(Index));
		Row->Data.AnimAsset = Montages[Index];
		Row->Database = Database;
	}
	Database->Modify();
	Database->Schema = Schema;
	// PostEditChange broadcasts the chooser's change event, rebuilding the private
	// pose-column mapping and its embedded database through the engine listener.
	Chooser->Compile(true);
	Chooser->PostEditChange();
	Database->PostEditChange();
	return true;
}
