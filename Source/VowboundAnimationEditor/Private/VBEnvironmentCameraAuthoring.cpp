#include "VBEnvironmentCameraAuthoring.h"
#include "Camera/VBEnvironmentCameraComponent.h"
#include "Data/VBEnvironmentCameraConfig.h"
#include "Core/CameraAsset.h"
#include "Directors/BlueprintCameraDirector.h"
#include "GameFramework/GameplayCameraComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

FString UVBEnvironmentCameraAuthoring::InspectCameraObject(UObject* Asset)
{
	if (!Asset) return TEXT("null");
	TArray<UObject*> Objects{Asset};
	GetObjectsWithOuter(Asset, Objects, EGetObjectsFlags::IncludeNestedObjects);
	FString Result;
	for (UObject* Object : Objects)
	{
		Result += Object->GetPathName() + TEXT(" [") + Object->GetClass()->GetName() + TEXT("]\n");
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Edit)) continue;
			FString Value;
			It->ExportText_InContainer(0, Value, Object, Object, Object, PPF_None);
			Result += It->GetName() + TEXT("=") + Value + TEXT("\n");
		}
	}
	return Result;
}

bool UVBEnvironmentCameraAuthoring::WireEnvironmentCamera(UCameraAsset* Camera, UBlueprint* Director,
	UBlueprint* Character, UVBEnvironmentCameraConfig* Config)
{
	if (!Camera || !Director || !Character || !Config || !Character->GeneratedClass ||
		!Camera->GetPathName().StartsWith(TEXT("/Game/Vowbound/Blueprints/Cameras/")) ||
		!Director->GetPathName().StartsWith(TEXT("/Game/Vowbound/Blueprints/Cameras/"))) return false;
	auto* CameraDirector = Cast<UBlueprintCameraDirector>(Camera->GetCameraDirector());
	if (!CameraDirector) return false;
	UK2Node_CallFunction* Old = nullptr;
	UK2Node_Event* Activate = nullptr;
	UK2Node_CallFunction* Existing = nullptr;
	for (UEdGraph* Graph : Director->UbergraphPages)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Event = Cast<UK2Node_Event>(Node); Event && Event->EventReference.GetMemberName() == TEXT("ActivateCameraDirector")) Activate = Event;
			if (auto* Call = Cast<UK2Node_CallFunction>(Node))
			{
				if (Call->FunctionReference.GetMemberName() == TEXT("ActivateEnvironmentCollision")) Existing = Call;
				if (Call->FunctionReference.GetMemberName() == TEXT("ActivatePersistentGlobalCameraRig"))
				{
					auto* Pin = Call->FindPin(TEXT("CameraRigPrefab"));
					if (Pin && Pin->DefaultObject && Pin->DefaultObject->GetName() == TEXT("CameraRig_CollisionOffset")) Old = Call;
				}
			}
		}
	}
	if (!Existing)
	{
		if (!Old || !Activate || Old->GetGraph() != Activate->GetGraph() ||
			!Activate->FindPin(TEXT("EvaluationContextOwner")) || !Old->GetExecPin() || !Old->GetThenPin()) return false;
		UEdGraph* Graph = Old->GetGraph();
		Graph->Modify();
		Director->Modify();
		const TArray<UEdGraphPin*> Inputs = Old->GetExecPin()->LinkedTo;
		const TArray<UEdGraphPin*> Outputs = Old->GetThenPin()->LinkedTo;
		UObject* Fallback = Old->FindPinChecked(TEXT("CameraRigPrefab"))->DefaultObject;
		UFunction* Function = UVBEnvironmentCameraComponent::StaticClass()->FindFunctionByName(TEXT("ActivateEnvironmentCollision"));
		if (!Function) return false;
		auto* Call = NewObject<UK2Node_CallFunction>(Graph);
		Call->CreateNewGuid();
		Call->SetFromFunction(Function);
		Graph->AddNode(Call, false, false);
		Call->AllocateDefaultPins();
		Call->NodePosX = Old->NodePosX;
		Call->NodePosY = Old->NodePosY;
		auto* Self = NewObject<UK2Node_Self>(Graph);
		Self->CreateNewGuid(); Graph->AddNode(Self, false, false); Self->AllocateDefaultPins();
		Self->NodePosX = Call->NodePosX - 180; Self->NodePosY = Call->NodePosY + 120;
		const auto* Schema = GetDefault<UEdGraphSchema_K2>();
		bool bConnected = Schema->TryCreateConnection(Activate->FindPinChecked(TEXT("EvaluationContextOwner")), Call->FindPinChecked(TEXT("EvaluationContextOwner")));
		bConnected &= Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self), Call->FindPinChecked(TEXT("Director")));
		Schema->TrySetDefaultObject(*Call->FindPinChecked(TEXT("FallbackRig")), Fallback);
		if (!bConnected) { FBlueprintEditorUtils::RemoveNode(Director, Call, true); FBlueprintEditorUtils::RemoveNode(Director, Self, true); return false; }
		// Direct links preserve the one existing execution path; no original GASP asset is modified.
		Old->BreakAllNodeLinks();
		for (UEdGraphPin* Pin : Inputs) Pin->MakeLinkTo(Call->GetExecPin());
		for (UEdGraphPin* Pin : Outputs) Call->GetThenPin()->MakeLinkTo(Pin);
		FBlueprintEditorUtils::RemoveNode(Director, Old, true);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Director);
	}
	FKismetEditorUtilities::CompileBlueprint(Director);
	if (Director->Status == BS_Error) return false;
	Camera->Modify(); CameraDirector->Modify();
	CameraDirector->CameraDirectorEvaluatorClass = Director->GeneratedClass;
	Camera->BuildCamera();

	FKismetEditorUtilities::CompileBlueprint(Character);
	if (Character->Status == BS_Error) return false;
	auto* CDO = Cast<AActor>(Character->GeneratedClass->GetDefaultObject());
	auto* GameplayCamera = CDO ? CDO->FindComponentByClass<UGameplayCameraComponent>() : nullptr;
	auto* EnvironmentCamera = CDO ? CDO->FindComponentByClass<UVBEnvironmentCameraComponent>() : nullptr;
	if (!GameplayCamera || !EnvironmentCamera) return false;
	Character->Modify(); CDO->Modify(); GameplayCamera->Modify(); EnvironmentCamera->Modify();
	GameplayCamera->CameraReference.SetCameraAsset(Camera);
	EnvironmentCamera->Config = Config;
	Character->MarkPackageDirty(); Camera->MarkPackageDirty(); Director->MarkPackageDirty();
	return true;
}
