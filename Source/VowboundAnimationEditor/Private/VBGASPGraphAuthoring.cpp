#include "VBGASPGraphAuthoring.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/UnrealType.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/VBGASPBridgeLibrary.h"
#include "Animation/VBAnimInstance.h"
#include "AnimGraphNode_LinkedAnimGraph.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Self.h"
#include "K2Node_Message.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"

namespace
{
	UEdGraph* FindGraph(UBlueprint* Blueprint, FName Name)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs) if (Graph && Graph->GetFName() == Name) return Graph;
		return nullptr;
	}

	UK2Node_CallFunction* AddCall(UEdGraph* Graph, FName Name)
	{
		UFunction* Function = UVBGASPBridgeLibrary::StaticClass()->FindFunctionByName(Name);
		if (!Function) return nullptr;
		auto* Call = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Call, false, false);
		Call->CreateNewGuid();
		Call->SetFromFunction(Function);
		Call->AllocateDefaultPins();
		return Call;
	}

	UK2Node_Self* AddSelf(UEdGraph* Graph)
	{
		auto* Self = NewObject<UK2Node_Self>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Self, false, false);
		Self->CreateNewGuid();
		Self->AllocateDefaultPins();
		return Self;
	}
}

bool UVBGASPGraphAuthoring::InstallCharacterInterface(UBlueprint* Blueprint, UBlueprint* Interface)
{
	if (!Blueprint || !Interface || !Interface->GeneratedClass) return false;
	Blueprint->Modify();
	if (!Blueprint->GeneratedClass->ImplementsInterface(Interface->GeneratedClass)
		&& !FBlueprintEditorUtils::ImplementNewInterface(Blueprint, FTopLevelAssetPath(Interface->GeneratedClass))) return false;
	for (FName Name : {FName(TEXT("Get_PropertiesForAnimation")), FName(TEXT("Get_PropertiesForCamera"))})
	{
		UEdGraph* Graph = FindGraph(Blueprint, Name);
		if (!Graph) return false;
		UK2Node_FunctionEntry* Entry = nullptr;
		UK2Node_FunctionResult* Result = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Value = Cast<UK2Node_FunctionEntry>(Node)) Entry = Value;
			if (auto* Value = Cast<UK2Node_FunctionResult>(Node)) Result = Value;
		}
		// Refuse to replace a previously authored implementation.
		if (!Entry || !Result || Graph->Nodes.Num() != 2) return false;
		UEdGraphPin* Return = Result->FindPin(TEXT("ReturnValue"));
		if (!Return || Return->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct) return false;
		Graph->Modify();
		auto* Call = AddCall(Graph, TEXT("ReadCharacterProperties"));
		auto* Self = AddSelf(Graph);
		if (!Call) return false;
		UEdGraphPin* Properties = Call->FindPinChecked(TEXT("Properties"));
		Properties->PinType = Return->PinType;
		const UEdGraphSchema* Schema = Graph->GetSchema();
		Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
		bool bValid = Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self), Call->FindPinChecked(TEXT("Character")));
		bValid &= Schema->TryCreateConnection(Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then), Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		bValid &= Schema->TryCreateConnection(Call->FindPinChecked(UEdGraphSchema_K2::PN_Then), Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		bValid &= Schema->TryCreateConnection(Properties, Return);
		Call->NodePosX = 300;
		Self->NodePosY = 180;
		Result->NodePosX = 650;
		if (!bValid) return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UVBGASPGraphAuthoring::AppendWeaponDatabaseOverride(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) return false;
	UEdGraph* Graph = FindGraph(Blueprint, TEXT("Update_MotionMatching"));
	if (!Graph) return false;
	UK2Node_FunctionEntry* Entry = nullptr;
	UK2Node_FunctionResult* Result = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (auto* Value = Cast<UK2Node_FunctionEntry>(Node)) Entry = Value;
		if (auto* Value = Cast<UK2Node_FunctionResult>(Node)) Result = Value;
	}
	if (!Entry || !Result) return false;
	UEdGraphPin* End = Result->FindPin(UEdGraphSchema_K2::PN_Execute);
	if (!End || End->LinkedTo.Num() != 1 || !Entry->FindPin(TEXT("Node"))) return false;
	UEdGraphPin* Previous = End->LinkedTo[0];
	Blueprint->Modify();
	Graph->Modify();
	auto* Call = AddCall(Graph, TEXT("ApplyWeaponDatabases"));
	auto* Self = AddSelf(Graph);
	const UEdGraphSchema* Schema = Graph->GetSchema();
	End->BreakAllPinLinks();
	bool bValid = Schema->TryCreateConnection(Previous, Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
	bValid &= Schema->TryCreateConnection(Call->FindPinChecked(UEdGraphSchema_K2::PN_Then), End);
	bValid &= Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self), Call->FindPinChecked(TEXT("LinkedInstance")));
	bValid &= Schema->TryCreateConnection(Entry->FindPinChecked(TEXT("Node")), Call->FindPinChecked(TEXT("Node")));
	Call->NodePosX = 450;
	Result->NodePosX = 750;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return bValid;
}

bool UVBGASPGraphAuthoring::InstallLinkedLocomotion(UAnimBlueprint* Blueprint, UAnimBlueprint* Locomotion)
{
	if (!Blueprint || !Locomotion || !Locomotion->GeneratedClass) return false;
	UEdGraph* Graph = FindGraph(Blueprint, TEXT("AnimGraph"));
	if (!Graph) return false;
	auto Pin = [Graph](const TCHAR* NodeName, const TCHAR* PinName) -> UEdGraphPin*
	{
		for (UEdGraphNode* Node : Graph->Nodes)
			if (Node->GetFName() == NodeName) return Node->FindPin(PinName);
		return nullptr;
	};
	UEdGraphPin* Base = Pin(TEXT("AnimGraphNode_BlendListByBool_5"), TEXT("BlendPose_1"));
	UEdGraphPin* Slot = Pin(TEXT("AnimGraphNode_Slot_0"), TEXT("Source"));
	UEdGraphPin* Traversal = Pin(TEXT("AnimGraphNode_BlendListByBool_4"), TEXT("Pose"));
	UEdGraphPin* Local = Pin(TEXT("AnimGraphNode_LocalToComponentSpace_0"), TEXT("LocalPose"));
	UEdGraphPin* Overlays = Pin(TEXT("AnimGraphNode_Inertialization_0"), TEXT("Pose"));
	UEdGraphPin* Hands = Pin(TEXT("AnimGraphNode_TwoBoneIK_0"), TEXT("ComponentPose"));
	UEdGraphPin* Component = Pin(TEXT("AnimGraphNode_LocalToComponentSpace_0"), TEXT("ComponentPose"));
	if (!Base || !Slot || !Traversal || !Local || !Overlays || !Hands || !Component) return false;
	Blueprint->Modify(); Graph->Modify();
	auto* Linked = NewObject<UAnimGraphNode_LinkedAnimGraph>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Linked, false, false);
	Linked->CreateNewGuid();
	Linked->Node.InstanceClass = Locomotion->GeneratedClass;
	Linked->Node.bPropagateNotifiesToLinkedInstances = true;
	Linked->PostPlacedNewNode();
	Linked->AllocateDefaultPins();
	Linked->SetTag(TEXT("GASPLocomotion"));
	Linked->NodeComment = TEXT("GASP 5.8 locomotion / Chooser / state controller / foot placement");
	Linked->NodePosX = -900; Linked->NodePosY = -100;
	const UEdGraphSchema* Schema = Graph->GetSchema();
	Base->BreakAllPinLinks(); Slot->BreakAllPinLinks(); Local->BreakAllPinLinks(); Hands->BreakAllPinLinks();
	bool bValid = Schema->TryCreateConnection(Linked->FindPinChecked(TEXT("Pose")), Base);
	bValid &= Schema->TryCreateConnection(Traversal, Slot);
	bValid &= Schema->TryCreateConnection(Overlays, Local);
	bValid &= Schema->TryCreateConnection(Component, Hands);
	if (!bValid) return false;
	// Retain only the reachable gameplay overlays, following saved-pose references too.
	// The previous asset is the rollback copy; dead legacy locomotion must not execute here.
	TSet<UEdGraphNode*> Reachable;
	TFunction<void(UEdGraphNode*)> Visit = [&](UEdGraphNode* Node)
	{
		if (!Node || Reachable.Contains(Node)) return;
		Reachable.Add(Node);
		if (auto* Use = Cast<UAnimGraphNode_UseCachedPose>(Node)) Visit(Use->SaveCachedPoseNode.Get());
		for (UEdGraphPin* Input : Node->Pins)
			if (Input->Direction == EGPD_Input)
				for (UEdGraphPin* Link : Input->LinkedTo) Visit(Link->GetOwningNode());
	};
	for (UEdGraphNode* Node : Graph->Nodes)
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_Root")) Visit(Node);
	const TArray<TObjectPtr<UEdGraphNode>> Nodes = Graph->Nodes;
	for (UEdGraphNode* Node : Nodes)
		if (!Reachable.Contains(Node)) FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UVBGASPGraphAuthoring::InstallRootOffsetOverride(UAnimBlueprint* Blueprint, FName FunctionName)
{
	UEdGraph* Graph = Blueprint ? FindGraph(Blueprint, FunctionName) : nullptr;
	if (!Graph) return false;
	UEdGraphPin* Return = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
		if (Cast<UK2Node_FunctionResult>(Node)) Return = Node->FindPin(TEXT("ReturnValue"));
	if (!Return || Return->LinkedTo.Num() != 1) return false;
	UEdGraphPin* ParentValue = Return->LinkedTo[0];
	Blueprint->Modify(); Graph->Modify();
	auto* Call = AddCall(Graph, TEXT("ConstrainRootOffsetMode"));
	auto* Self = AddSelf(Graph);
	Return->BreakAllPinLinks();
	const UEdGraphSchema* Schema = Graph->GetSchema();
	bool bValid = Schema->TryCreateConnection(ParentValue, Call->FindPinChecked(TEXT("Mode")));
	bValid &= Schema->TryCreateConnection(Call->FindPinChecked(TEXT("ReturnValue")), Return);
	bValid &= Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self), Call->FindPinChecked(TEXT("LinkedInstance")));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return bValid;
}

bool UVBGASPGraphAuthoring::InstallAnimationInterfaceForwarding(UAnimBlueprint* Blueprint, UBlueprint* Interface)
{
	if (!Blueprint || !Interface || !Interface->GeneratedClass) return false;
	UClass* Contract = Interface->GeneratedClass;
	if (Blueprint->GeneratedClass->ImplementsInterface(Contract)) return false;
	Blueprint->Modify();
	if (!FBlueprintEditorUtils::ImplementNewInterface(Blueprint, FTopLevelAssetPath(Contract))) return false;
	for (TFieldIterator<UFunction> It(Contract, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		UFunction* Function = *It;
		if (!Function->HasAnyFunctionFlags(FUNC_BlueprintEvent)) continue;
		// The CMC source leaves this event unimplemented. Vowbound's finisher owns
		// multi-character constraints through IVBInteractionAnimInterface instead.
		if (Function->GetFName() == TEXT("Update_MMIConstraints")) continue;
		UEdGraph* Graph = FindGraph(Blueprint, Function->GetFName());
		if (!Graph)
		{
			Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Function->GetFName(), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, false, Function);
		}
		UK2Node_FunctionEntry* Entry = nullptr;
		UK2Node_FunctionResult* Result = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Value = Cast<UK2Node_FunctionEntry>(Node)) Entry = Value;
			if (auto* Value = Cast<UK2Node_FunctionResult>(Node)) Result = Value;
		}
		if (!Entry || Graph->Nodes.Num() > 2) return false;
		auto* Lookup = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Lookup, false, false);
		Lookup->CreateNewGuid();
		Lookup->SetFromFunction(UVBAnimInstance::StaticClass()->FindFunctionByName(TEXT("GetGASPLocomotionInstance")));
		Lookup->AllocateDefaultPins();
		auto* Message = NewObject<UK2Node_Message>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Message, false, false);
		Message->CreateNewGuid();
		Message->FunctionReference.SetExternalMember(Function->GetFName(), Contract);
		Message->AllocateDefaultPins();
		const UEdGraphSchema* Schema = Graph->GetSchema();
		bool bValid = Schema->TryCreateConnection(Lookup->FindPinChecked(TEXT("ReturnValue")), Message->FindPinChecked(UEdGraphSchema_K2::PN_Self));
		for (UEdGraphPin* Pin : Entry->Pins)
		{
			if (Pin->Direction != EGPD_Output) continue;
			FName TargetName = Pin->PinName == UEdGraphSchema_K2::PN_Then ? UEdGraphSchema_K2::PN_Execute : Pin->PinName;
			if (UEdGraphPin* Target = Message->FindPin(TargetName, EGPD_Input))
			{
				Pin->BreakAllPinLinks();
				bValid &= Schema->TryCreateConnection(Pin, Target);
			}
		}
		if (Result)
			for (UEdGraphPin* Pin : Result->Pins)
			{
				if (Pin->Direction != EGPD_Input) continue;
				FName SourceName = Pin->PinName == UEdGraphSchema_K2::PN_Execute ? UEdGraphSchema_K2::PN_Then : Pin->PinName;
				if (UEdGraphPin* Source = Message->FindPin(SourceName, EGPD_Output))
				{
					Pin->BreakAllPinLinks();
					bValid &= Schema->TryCreateConnection(Source, Pin);
				}
			}
		Lookup->NodePosY = 200; Message->NodePosX = 350;
		if (!bValid) return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

FString UVBGASPGraphAuthoring::DescribeGraph(UBlueprint* Blueprint, FName GraphName)
{
	if (!Blueprint) return TEXT("Missing Blueprint");
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	FString Result;
	for (const UEdGraph* Graph : Graphs)
	{
		if (!Graph || Graph->GetFName() != GraphName) continue;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || Node->GetClass()->GetFName() == TEXT("EdGraphNode_Comment")) continue;
			Result += Node->GetName() + TEXT(" ") + Node->GetNodeTitle(ENodeTitleType::ListView).ToString() + TEXT("\n");
			if (!Node->NodeComment.IsEmpty()) Result += TEXT("  comment=") + Node->NodeComment + TEXT("\n");
			if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				if (const UFunction* Function = Call->GetTargetFunction()) Result += TEXT("  function=") + Function->GetPathName() + TEXT("\n");
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				Result += FString::Printf(TEXT("  %s %s [%s] %s"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"),
					*Pin->PinName.ToString(), *GetPathNameSafe(Pin->PinType.PinSubCategoryObject.Get()), *Pin->DefaultValue);
				if (Pin->DefaultObject) Result += TEXT(" object=") + Pin->DefaultObject->GetPathName();
				for (const UEdGraphPin* Link : Pin->LinkedTo)
					Result += TEXT(" -> ") + Link->GetOwningNode()->GetName() + TEXT(".") + Link->PinName.ToString();
				Result += TEXT("\n");
			}
		}
	}
	return Result;
}

FString UVBGASPGraphAuthoring::DescribeStruct(UScriptStruct* Struct)
{
	if (!Struct) return TEXT("Missing struct");
	FString Result = Struct->GetPathName() + TEXT("\n");
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		Result += It->GetName() + TEXT(" : ") + It->GetCPPType() + TEXT("\n");
		const FByteProperty* Byte = CastField<FByteProperty>(*It);
		const FEnumProperty* EnumProperty = CastField<FEnumProperty>(*It);
		const UEnum* Enum = Byte ? Byte->Enum.Get() : (EnumProperty ? EnumProperty->GetEnum() : nullptr);
		if (Enum)
			for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
				Result += FString::Printf(TEXT("  %lld=%s\n"), Enum->GetValueByIndex(Index), *Enum->GetAuthoredNameStringByIndex(Index));
		if (const FStructProperty* Nested = CastField<FStructProperty>(*It))
			if (Nested->Struct->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))) Result += DescribeStruct(Nested->Struct);
	}
	return Result;
}
