#include "VBGASPLocomotionAuthoring.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "InputMappingContext.h"
#include "InputAction.h"

// 에디터 모듈은 게임 모듈의 LogVB 를 못 쓴다(내보내지 않는다). 이름은 파일마다 달라야 unity 묶음에서 겹치지 않는다.
DEFINE_LOG_CATEGORY_STATIC(LogVBGASPLocomotionAuthoring, Log, All);

bool UVBGASPLocomotionAuthoring::CompileAndValidateMixer(UAnimBlueprint* Blueprint)
{
	if (!Blueprint || !IsInGameThread()) return false;
	UScriptStruct* MixerType = FindObject<UScriptStruct>(nullptr, TEXT("/Script/MovieSceneAnimMixer.AnimSubsystem_SequencerMixer"));
	if (!MixerType) return false;
	Blueprint->RequestRefreshExtensions();
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UAnimInstance* Default = Blueprint->GeneratedClass ? Cast<UAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	return Blueprint->Status != BS_Error && Default && Default->FindSubsystem(MixerType);
}

TArray<FName> UVBGASPLocomotionAuthoring::GetNativeFunctions(TSubclassOf<UBlueprintFunctionLibrary> Library)
{
	TArray<FName> Result;
	if (Library)
		for (TFieldIterator<UFunction> It(Library, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			if (It->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintCallable)) Result.Add(It->GetFName());
	return Result;
}

bool UVBGASPLocomotionAuthoring::InstallNativeOverride(UAnimBlueprint* Blueprint, FName FunctionName, TSubclassOf<UBlueprintFunctionLibrary> Library, UObject* ConstantObject)
{
	if (!Blueprint || !Library) return false;
	UFunction* Native = Library->FindFunctionByName(FunctionName);
	if (!Native) return false;
	UEdGraph* Graph = nullptr;
	for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
		if (Candidate && Candidate->GetFName() == FunctionName) { Graph = Candidate; break; }
	if (!Graph) return false;
	UK2Node_FunctionEntry* Entry = nullptr;
	UK2Node_FunctionResult* Result = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (auto* E = Cast<UK2Node_FunctionEntry>(Node)) Entry = E;
		if (auto* R = Cast<UK2Node_FunctionResult>(Node)) Result = R;
	}
	if (!Entry) return false;
	Blueprint->Modify(); Graph->Modify();
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	// Override generation may split a UDS return into member pins. Native thunks
	// consume the complete struct, retaining the original UDS type identity.
	for (UEdGraphNode* Boundary : { static_cast<UEdGraphNode*>(Entry), static_cast<UEdGraphNode*>(Result) })
	{
		if (!Boundary) continue;
		// Recombining destroys child pins; keep only surviving parent pointers.
		const TArray<UEdGraphPin*> Pins = Boundary->Pins.FilterByPredicate([](const UEdGraphPin* Pin) { return Pin && !Pin->ParentPin; });
		for (UEdGraphPin* Pin : Pins)
			if (Pin && !Pin->ParentPin && !Pin->SubPins.IsEmpty()) Schema->RecombinePin(Pin->SubPins[0]);
	}
	// Preflight every exposed entry/output type before replacing the body.
	auto* Call = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Call, false, false); Call->CreateNewGuid(); Call->SetFromFunction(Native); Call->AllocateDefaultPins();
	TArray<FString> WildcardNames;
	Native->GetMetaData(TEXT("CustomStructureParam")).ParseIntoArray(WildcardNames, TEXT(","));
	for (UEdGraphPin* Pin : Call->Pins)
	{
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->PinName == TEXT("self") || Pin->PinName == TEXT("Instance")) continue;
		UEdGraphPin* Other = Pin->Direction == EGPD_Input ? Entry->FindPin(Pin->PinName) : Result ? Result->FindPin(Pin->PinName) : nullptr;
		if (!Other && Pin->Direction == EGPD_Input && (Pin->PinName == TEXT("Chooser") || Pin->PinName == TEXT("StrafeCurve")) && ConstantObject)
		{
			Schema->TrySetDefaultObject(*Pin, ConstantObject);
			if (Pin->DefaultObject != ConstantObject) { Graph->RemoveNode(Call); return false; }
			continue;
		}
		// CustomThunk UDS parameters preserve the original data-only struct contract.
		if (Other && WildcardNames.Contains(Pin->PinName.ToString()))
			Pin->PinType = Other->PinType;
		// An untyped native byte consumes the original UDE's numeric value.
		if (Other && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte && !Pin->PinType.PinSubCategoryObject.IsValid() && Other->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
			Pin->PinType = Other->PinType;
		if (!Other || !Schema->ArePinTypesCompatible(Pin->PinType, Other->PinType))
		{
			Graph->RemoveNode(Call);
			UE_LOG(LogVBGASPLocomotionAuthoring, Error, TEXT("GASP native override %s: incompatible %s"), *FunctionName.ToString(), *Pin->PinName.ToString());
			return false;
		}
	}
	const TArray<TObjectPtr<UEdGraphNode>> OldNodes = Graph->Nodes;
	for (UEdGraphNode* Node : OldNodes)
		if (Node != Entry && Node != Result && Node != Call) FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
	for (UEdGraphPin* Pin : Entry->Pins) Pin->BreakAllPinLinks();
	if (Result) for (UEdGraphPin* Pin : Result->Pins) Pin->BreakAllPinLinks();
	auto* Self = NewObject<UK2Node_Self>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Self, false, false); Self->CreateNewGuid(); Self->AllocateDefaultPins();
	bool Valid = true;
	if (UEdGraphPin* Instance = Call->FindPin(TEXT("Instance")))
		Valid &= Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self), Instance);
	for (UEdGraphPin* Pin : Call->Pins)
	{
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->PinName == TEXT("self") || Pin->PinName == TEXT("Instance")) continue;
		UEdGraphPin* Other = Pin->Direction == EGPD_Input ? Entry->FindPin(Pin->PinName) : Result ? Result->FindPin(Pin->PinName) : nullptr;
		if (!Other && (Pin->PinName == TEXT("Chooser") || Pin->PinName == TEXT("StrafeCurve")) && ConstantObject) continue;
		Valid &= Other && Schema->TryCreateConnection(Pin, Other);
	}
	UEdGraphPin* End = Result ? Result->FindPin(UEdGraphSchema_K2::PN_Execute) : nullptr;
	UEdGraphPin* Start = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
	if (Native->HasAnyFunctionFlags(FUNC_BlueprintPure))
	{
		if (Start && End) Valid &= Schema->TryCreateConnection(Start, End);
	}
	else
	{
		Valid &= Start && Schema->TryCreateConnection(Start, Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		if (End) Valid &= Schema->TryCreateConnection(Call->FindPinChecked(UEdGraphSchema_K2::PN_Then), End);
	}
	Entry->NodePosX = 0; Self->NodePosY = 200; Call->NodePosX = 300;
	if (Result) Result->NodePosX = 650;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Valid;
}

FString UVBGASPLocomotionAuthoring::DescribeEnum(UEnum* Enum)
{
	FString Result;
	if (Enum) for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		Result += FString::Printf(TEXT("%lld:%s=%s\n"), Enum->GetValueByIndex(Index), *Enum->GetNameStringByIndex(Index), *Enum->GetAuthoredNameStringByIndex(Index));
	return Result;
}

bool UVBGASPLocomotionAuthoring::MapUnassignedInput(UInputMappingContext* Context, UInputAction* Action, FName KeyName)
{
	const FKey Key(KeyName);
	if (!Context || !Action || !Key.IsValid()) return false;
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		if (Mapping.Key != Key) continue;
		if (Mapping.Action == Action) return true;
		UE_LOG(LogVBGASPLocomotionAuthoring, Error, TEXT("GASP input %s: %s is already assigned to %s"), *Context->GetName(), *KeyName.ToString(), *GetNameSafe(Mapping.Action));
		return false;
	}
	Context->Modify();
	Context->MapKey(Action, Key);
	Context->MarkPackageDirty();
	return true;
}
