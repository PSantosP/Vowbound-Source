#include "VBGASPSmartObjectAuthoring.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Interaction/VBGASPSmartObjectTasks.h"
#include "Interaction/VBGASPNPC.h"
#include "Animation/AnimBlueprint.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "Chooser.h"
#include "ProxyAsset.h"
#include "ProxyTable.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 에디터 모듈은 게임 모듈의 LogVB 를 못 쓴다(내보내지 않는다). 이름은 파일마다 달라야 unity 묶음에서 겹치지 않는다.
DEFINE_LOG_CATEGORY_STATIC(LogVBSmartObjectAuthoring, Log, All);

namespace
{
	FString ObjectPath(const FString& Package) { return Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package); }
	UStateTree* CopyStateTreeAuthoring(UStateTree* Source, UObject* Outer, FName Name, EObjectFlags Flags, FString& Error)
	{
		UStateTreeEditorData* SourceEditor = Source ? Cast<UStateTreeEditorData>(Source->EditorData) : nullptr;
		if (!SourceEditor || Source->GetClass() != UStateTree::StaticClass())
		{
			Error = TEXT("Expected StateTree with editor data");
			return nullptr;
		}
		UStateTree* Copy = NewObject<UStateTree>(Outer, Name, Flags);
		UStateTreeEditorData* Editor = NewObject<UStateTreeEditorData>(Copy, SourceEditor->GetClass(), SourceEditor->GetFName(), RF_Transactional | (Flags & RF_Transient));
		Copy->EditorData = Editor;
		// UE 5.8 EditorData::PostDuplicate independently randomizes derived event
		// IDs and reports its own repeated event visit as a duplicate. Copy into
		// a pre-created editor root instead; IDs are scoped to the owning tree.
		FObjectDuplicationParameters Parameters = InitStaticDuplicateObjectParams(SourceEditor, Copy, Editor->GetFName());
		Parameters.DuplicationSeed.Add(Source, Copy);
		Parameters.DuplicationSeed.Add(SourceEditor, Editor);
		Parameters.ApplyFlags = Flags & RF_Transient;
		Parameters.bSkipPostLoad = true;
		TMap<UObject*, UObject*> CreatedObjects;
		Parameters.CreatedObjects = &CreatedObjects;
		if (StaticDuplicateObjectEx(Parameters) != Editor)
		{
			Error = TEXT("StateTree editor-data copy failed");
			Copy->ClearFlags(RF_Public | RF_Standalone);
			Copy->MarkAsGarbage();
			return nullptr;
		}
		Editor->EditorBindings.SetBindingsOwner(Editor);
		{
			TGuardValue<bool> PostLoadGuard(FUObjectThreadContext::Get().IsRoutingPostLoad, true);
			Editor->ConditionalPostLoad();
			for (const auto& Pair : CreatedObjects) Pair.Value->ConditionalPostLoad();
		}
		Copy->MarkAsModified(true);
		return Copy;
	}
	UObject* CopyAsset(const FString& SourcePath, const FString& Root, const FString& Name)
	{
		if (UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath(Root + TEXT("/") + Name))) return Existing;
		UObject* Source = LoadObject<UObject>(nullptr, *SourcePath);
		if (!Source) return nullptr;
		if (UStateTree* SourceTree = Cast<UStateTree>(Source))
		{
			FString Error;
			UStateTree* Copy = CopyStateTreeAuthoring(SourceTree, CreatePackage(*(Root / Name)), FName(*Name), RF_Public | RF_Standalone | RF_Transactional, Error);
			if (!Copy) { UE_LOG(LogVBSmartObjectAuthoring, Error, TEXT("%s: %s"), *SourcePath, *Error); return nullptr; }
			FAssetRegistryModule::AssetCreated(Copy);
			Copy->MarkPackageDirty();
			return Copy;
		}
		return FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().DuplicateAsset(Name, Root, Source);
	}
	FName NativeUDSField(FName Name)
	{
		const FString Text = Name.ToString();
		// Only UDS-generated field names have this GUID suffix. An SCS component
		// such as SmartObject_GEN_VARIABLE must keep its name and inheritance key.
		FGuid FieldGuid;
		if (Text.Len() < 34 || Text[Text.Len() - 33] != TEXT('_')
			|| !FGuid::ParseExact(Text.Right(32), EGuidFormats::Digits, FieldGuid)) return Name;
		const TPair<const TCHAR*, const TCHAR*> Names[] = {
			{TEXT("TargetDistance_"), TEXT("TargetDistance")}, {TEXT("TargetAngle_"), TEXT("TargetAngle")},
			{TEXT("PoseHistoryNode_"), TEXT("PoseHistoryNode")}, {TEXT("Cost_"), TEXT("Cost")}, {TEXT("StartTime_"), TEXT("StartTime")},
			{TEXT("SlotToClaim_"), TEXT("Slot")}, {TEXT("SmartObject_"), TEXT("Actor")}
		};
		for (const auto& Pair : Names) if (Text.StartsWith(Pair.Key)) return FName(Pair.Value);
		return Name;
	}
	bool RepairDuplicatedEventBindings(UStateTree* Source, UStateTree* Copy, FString& Error)
	{
		auto* SourceEditor = Cast<UStateTreeEditorData>(Source->EditorData);
		auto* CopyEditor = Cast<UStateTreeEditorData>(Copy->EditorData);
		if (!SourceEditor || !CopyEditor) { Error = TEXT("Missing StateTree editor data"); return false; }
		TArray<FStateTreeBindableStructDesc> Originals, Duplicates;
		auto Collect = [](UStateTreeEditorData* Data, TArray<FStateTreeBindableStructDesc>& Out)
		{
			Data->VisitAllNodes([&Out](const UStateTreeState*, const FStateTreeBindableStructDesc& Desc, FStateTreeDataView)
			{ Out.Add(Desc); return EStateTreeVisitor::Continue; });
		};
		Collect(SourceEditor, Originals); Collect(CopyEditor, Duplicates);
		if (Originals.Num() != Duplicates.Num()) { Error = TEXT("Duplicated StateTree node count differs"); return false; }
		TMap<FGuid, FGuid> IDs, EventIDs;
		for (int32 Index = 0; Index < Originals.Num(); ++Index)
		{
			const auto& A = Originals[Index]; const auto& B = Duplicates[Index];
			if (A.DataSource != B.DataSource || A.Name != B.Name || A.StatePath != B.StatePath)
			{ Error = TEXT("Duplicated StateTree node hierarchy differs"); return false; }
			IDs.Add(A.ID, B.ID);
			if (A.DataSource == EStateTreeBindableStructSource::StateEvent || A.DataSource == EStateTreeBindableStructSource::TransitionEvent)
				EventIDs.Add(A.ID, B.ID);
		}
		const auto OriginalBindings = SourceEditor->EditorBindings.GetBindings();
		auto CopyBindings = CopyEditor->EditorBindings.GetMutableBindings();
		if (OriginalBindings.Num() != CopyBindings.Num()) { Error = TEXT("Duplicated StateTree binding count differs"); return false; }
		// DuplicateIDs randomizes binding event IDs, while GetEventID derives them
		// from the new state/transition ID. Validate every correspondence first.
		for (int32 Index = 0; Index < OriginalBindings.Num(); ++Index)
		{
			const auto& Original = OriginalBindings[Index]; const auto& Duplicate = CopyBindings[Index];
			if (!EventIDs.Contains(Original.GetSourcePath().GetStructID())) continue;
			const FGuid* TargetID = IDs.Find(Original.GetTargetPath().GetStructID());
			if (!TargetID || *TargetID != Duplicate.GetTargetPath().GetStructID()
				|| Original.GetSourcePath().GetSegments().Num() != Duplicate.GetSourcePath().GetSegments().Num()
				|| Original.GetTargetPath().GetSegments().Num() != Duplicate.GetTargetPath().GetSegments().Num())
			{ Error = TEXT("Duplicated StateTree event binding identity differs"); return false; }
		}
		for (int32 Index = 0; Index < OriginalBindings.Num(); ++Index)
			if (const FGuid* EventID = EventIDs.Find(OriginalBindings[Index].GetSourcePath().GetStructID()))
				CopyBindings[Index].GetMutableSourcePath().SetStructID(*EventID);
		return true;
	}
	// Walk reflected authoring data, including InstancedStruct columns. Never execute a BP graph.
	void RemapData(const UStruct* Type, void* Data, const TMap<UObject*, UObject*>& Objects)
	{
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			FProperty* Property = *It;
			void* Address = Property->ContainerPtrToValuePtr<void>(Data);
			TFunction<void(FProperty*, void*)> RemapValue;
			RemapValue = [&](FProperty* Field, void* Value)
			{
				if (FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Field))
				{
					if (UObject* const* Replacement = Objects.Find(Object->GetObjectPropertyValue(Value))) Object->SetObjectPropertyValue(Value, *Replacement);
				}
				else if (FStructProperty* Struct = CastField<FStructProperty>(Field))
				{
					if (Struct->Struct == FInstancedStruct::StaticStruct())
					{
						FInstancedStruct& Instance = *static_cast<FInstancedStruct*>(Value);
						if (Instance.IsValid()) RemapData(Instance.GetScriptStruct(), Instance.GetMutableMemory(), Objects);
					}
					else RemapData(Struct->Struct, Value, Objects);
				}
				else if (FArrayProperty* Array = CastField<FArrayProperty>(Field))
				{
					FScriptArrayHelper Values(Array, Value);
					for (int32 Index = 0; Index < Values.Num(); ++Index) RemapValue(Array->Inner, Values.GetRawPtr(Index));
				}
				else if (FNameProperty* Name = CastField<FNameProperty>(Field)) Name->SetPropertyValue(Value, NativeUDSField(Name->GetPropertyValue(Value)));
			};
			RemapValue(Property, Address);
		}
	}
	void RemapPackage(UObject* Asset, const TMap<UObject*, UObject*>& Objects)
	{
		TArray<UObject*> Inner;
		GetObjectsWithPackage(Asset->GetPackage(), Inner, EGetObjectsFlags::IncludeNestedObjects);
		for (UObject* Object : Inner)
		{
			// Generated UClass/UStruct metadata is maintained by the compiler. Writing
			// SuperStruct as an ordinary property corrupts UObject's class hash.
			if (Object->IsA<UField>()) continue;
			Object->Modify();
			RemapData(Object->GetClass(), Object, Objects);
			if (USCS_Node* Node = Cast<USCS_Node>(Object))
			{
				// Inherited attachments store the owner class as a name, not an object.
				for (const auto& Pair : Objects)
					if (const UClass* OldClass = Cast<UClass>(Pair.Key); OldClass && Node->ParentComponentOwnerClassName == OldClass->GetFName())
						if (const UClass* NewClass = Cast<UClass>(Pair.Value)) Node->ParentComponentOwnerClassName = NewClass->GetFName();
			}
		}
		Asset->MarkPackageDirty();
	}
	FName TaskField(FName OldName, EVBGASPSmartObjectTaskOperation Operation)
	{
		if (OldName == TEXT("Claimed Handle")) return TEXT("ClaimHandle");
		if (OldName == TEXT("SmartObject") && Operation == EVBGASPSmartObjectTaskOperation::ClaimSlot) return TEXT("SmartObjectActor");
		FString Name = OldName.ToString();
		Name.ReplaceInline(TEXT(" "), TEXT(""));
		return FName(Name);
	}
	void CopyFields(UObject* Old, UObject* Native, EVBGASPSmartObjectTaskOperation Operation, TMap<FName, FName>& Names)
	{
		for (TFieldIterator<FProperty> It(Old->GetClass()); It; ++It)
		{
			FProperty* From = *It;
			const FName Name = TaskField(From->GetFName(), Operation);
			FProperty* To = FindFProperty<FProperty>(Native->GetClass(), Name);
			if (!To) continue;
			Names.Add(From->GetFName(), Name);
			const void* Source = From->ContainerPtrToValuePtr<void>(Old);
			void* Target = To->ContainerPtrToValuePtr<void>(Native);
			if (From->SameType(To)) To->CopyCompleteValue(Target, Source);
			else if (FNumericProperty* NumberFrom = CastField<FNumericProperty>(From))
			{
				if (FNumericProperty* NumberTo = CastField<FNumericProperty>(To))
				{
					const double Value = NumberFrom->IsFloatingPoint() ? NumberFrom->GetFloatingPointPropertyValue(Source) : NumberFrom->GetSignedIntPropertyValue(Source);
					if (NumberTo->IsFloatingPoint()) NumberTo->SetFloatingPointPropertyValue(Target, Value);
					else NumberTo->SetIntPropertyValue(Target, static_cast<int64>(Value));
				}
			}
		}
	}
	bool ConvertStateTree(UStateTree* Tree, UClass* SmartObjectClass, FString& Error)
	{
		UStateTreeEditorData* Editor = Cast<UStateTreeEditorData>(Tree->EditorData);
		if (!Editor) { Error = TEXT("Missing StateTree editor data"); return false; }
		const TMap<FName, EVBGASPSmartObjectTaskOperation> Operations = {
			{TEXT("STT_FindSmartObject_C"), EVBGASPSmartObjectTaskOperation::FindSmartObject},
			{TEXT("STT_ClaimSlot_C"), EVBGASPSmartObjectTaskOperation::ClaimSlot},
			{TEXT("STT_ClearHandle_C"), EVBGASPSmartObjectTaskOperation::ClearHandle},
			{TEXT("STT_FindSlotTransforms_C"), EVBGASPSmartObjectTaskOperation::FindSlotTransforms},
			{TEXT("STT_PlayAnimMontage_C"), EVBGASPSmartObjectTaskOperation::PlayAnimMontage},
			{TEXT("STT_PlayAnimFromBestCost_C"), EVBGASPSmartObjectTaskOperation::PlayAnimFromBestCost},
			{TEXT("STT_FocusToTarget_C"), EVBGASPSmartObjectTaskOperation::FocusToTarget},
			{TEXT("STT_ClearFocus_C"), EVBGASPSmartObjectTaskOperation::ClearFocus},
			{TEXT("STT_CharacterIgnoreCollisionsWithOtherActor_C"), EVBGASPSmartObjectTaskOperation::IgnoreCollisions},
			{TEXT("STT_AddCooldown_C"), EVBGASPSmartObjectTaskOperation::AddCooldown},
			{TEXT("STT_UseSmartObject_C"), EVBGASPSmartObjectTaskOperation::UseSmartObject},
			{TEXT("STT_SetCharacterInputState_C"), EVBGASPSmartObjectTaskOperation::SetCharacterInputState},
			{TEXT("STT_FindRandomLocation_C"), EVBGASPSmartObjectTaskOperation::FindRandomLocation}
		};
		TMap<FGuid, TMap<FName, FName>> FieldMaps;
		auto ConvertNode = [&](FStateTreeEditorNode& Node)
		{
			UObject* Old = Node.InstanceObject;
			if (!Old || !Old->GetClass()->ClassGeneratedBy) return;
			UObject* Native = nullptr;
			EVBGASPSmartObjectTaskOperation Operation = EVBGASPSmartObjectTaskOperation::FindSmartObject;
			if (const auto* Found = Operations.Find(Old->GetClass()->GetFName()))
			{
				Operation = *Found;
				FStateTreeBlueprintTaskWrapper* Wrapper = Node.Node.GetMutablePtr<FStateTreeBlueprintTaskWrapper>();
				if (!Wrapper) { Error = TEXT("Unexpected task wrapper"); return; }
				Wrapper->TaskClass = UVBGASPSmartObjectTask::StaticClass();
				Native = NewObject<UVBGASPSmartObjectTask>(Old->GetOuter(), NAME_None, RF_Transactional);
			}
			else if (Old->GetClass()->GetFName() == TEXT("STE_GetAIData_C"))
			{
				Node.Node.GetMutable<FStateTreeBlueprintEvaluatorWrapper>().EvaluatorClass = UVBGASPGetAIDataEvaluator::StaticClass();
				Native = NewObject<UVBGASPGetAIDataEvaluator>(Old->GetOuter(), NAME_None, RF_Transactional);
			}
			else if (Old->GetClass()->GetFName() == TEXT("STC_CheckCooldown_C"))
			{
				Node.Node.GetMutable<FStateTreeBlueprintConditionWrapper>().ConditionClass = UVBGASPCheckCooldownCondition::StaticClass();
				Native = NewObject<UVBGASPCheckCooldownCondition>(Old->GetOuter(), NAME_None, RF_Transactional);
			}
			else { Error = TEXT("Unported StateTree Blueprint: ") + Old->GetClass()->GetPathName(); return; }
			CopyFields(Old, Native, Operation, FieldMaps.Add(Node.ID));
			if (UVBGASPSmartObjectTask* Task = Cast<UVBGASPSmartObjectTask>(Native))
			{
				Task->Operation = Operation;
				Task->ExcludedSmartObjectClass = SmartObjectClass;
			}
			Node.InstanceObject = Native;
		};
		for (auto& Node : Editor->Evaluators) ConvertNode(Node);
		for (auto& Node : Editor->GlobalTasks) ConvertNode(Node);
		Editor->VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
		{
			for (auto& Node : State.Tasks) ConvertNode(Node);
			for (auto& Node : State.EnterConditions) ConvertNode(Node);
			ConvertNode(State.SingleTask);
			for (auto& Transition : State.Transitions) for (auto& Node : Transition.Conditions) ConvertNode(Node);
			return EStateTreeVisitor::Continue;
		});
		if (!Error.IsEmpty()) return false;
		for (FStateTreePropertyPathBinding& Binding : Editor->EditorBindings.GetMutableBindings())
		{
			for (FPropertyBindingPath* Path : { &Binding.GetMutableSourcePath(), &Binding.GetMutableTargetPath() })
			{
				auto Segments = Path->GetMutableSegments();
				if (Segments.Num())
				{
					if (const auto* Fields = FieldMaps.Find(Path->GetStructID()))
					{
						const FName* NewName = Fields->Find(Segments[0].GetName());
						if (!NewName) { Error = TEXT("Unmapped StateTree binding: ") + Segments[0].GetName().ToString(); return false; }
						Segments[0].SetName(*NewName);
					}
				}
			}
		}
		Editor->UpdateBindings();
		return true;
	}
	UBlueprint* NativeBlueprint(UClass* Parent, const FString& Root, const FString& Name)
	{
		if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ObjectPath(Root + TEXT("/") + Name))) return BP->ParentClass == Parent ? BP : nullptr;
		UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
		Factory->ParentClass = Parent;
		return Cast<UBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(Name, Root, UBlueprint::StaticClass(), Factory));
	}
}

FString UVBGASPSmartObjectAuthoring::BuildSmartObjectAssets(UBlueprint* CharacterReference, const FString& DestinationPath)
{
	if (!CharacterReference || !CharacterReference->GeneratedClass || !DestinationPath.StartsWith(TEXT("/Game/Vowbound/"))) return TEXT("ERROR: invalid project destination or character");
	const TPair<const TCHAR*, const TCHAR*> Copies[] = {
		{TEXT("/Game/Blueprints/SmartObjects/PSS_SmartObject"), TEXT("PSS_VBSmartObject")},
		{TEXT("/Game/Blueprints/SmartObjects/CHPA_SmartObject"), TEXT("CHPA_VBSmartObject")},
		{TEXT("/Game/Blueprints/SmartObjects/Bench/CHT_SmartObject_BenchAnim"), TEXT("CHT_VBBenchAnim")},
		{TEXT("/Game/Blueprints/SmartObjects/Bench/CHPT_SmartObject_Bench"), TEXT("CHPT_VBBench")},
		{TEXT("/Game/Blueprints/AI/StateTree/ST_NPC_SandboxCharacter_Patrol_Subtree"), TEXT("ST_VBNPCPatrol")},
		{TEXT("/Game/Blueprints/SmartObjects/Bench/ST_SmartObject_Bench"), TEXT("ST_VBSmartBench")},
		{TEXT("/Game/Blueprints/AI/StateTree/ST_Player_SandboxCharacter_SmartObject"), TEXT("ST_VBPlayerSmartObject")},
		{TEXT("/Game/Blueprints/AI/StateTree/ST_NPC_SandboxCharacter_SmartObject"), TEXT("ST_VBNPCSmartObject")},
		{TEXT("/Game/Blueprints/SmartObjects/Bench/SO_BenchDefinition"), TEXT("SO_VBBench")},
		{TEXT("/Game/Blueprints/SmartObjects/Bench/BP_SmartBench"), TEXT("BP_VBSmartBench")},
		{TEXT("/Game/Blueprints/SmartObjects/BP_SmartObject_Base"), TEXT("BP_VBSmartObjectBase")}
	};
	TMap<UObject*, UObject*> Objects;
	TArray<UObject*> Assets;
	for (const auto& Pair : Copies)
	{
		UObject* Source = LoadObject<UObject>(nullptr, Pair.Key);
		UObject* Copy = CopyAsset(Pair.Key, DestinationPath, Pair.Value);
		if (!Source || !Copy) return FString(TEXT("ERROR: cannot copy ")) + Pair.Key;
		if (UStateTree* SourceTree = Cast<UStateTree>(Source))
		{
			FString Error;
			if (!RepairDuplicatedEventBindings(SourceTree, CastChecked<UStateTree>(Copy), Error)) return TEXT("ERROR: ") + Error;
		}
		Objects.Add(Source, Copy);
		if (const UBlueprint* SourceBP = Cast<UBlueprint>(Source))
			Objects.Add(SourceBP->GeneratedClass, CastChecked<UBlueprint>(Copy)->GeneratedClass);
		Assets.Add(Copy);
	}
	const TPair<const TCHAR*, UScriptStruct*> Types[] = {
		{TEXT("/Game/Blueprints/SmartObjects/SmartObjectSelectionInputs"), FVBGASPSmartObjectSelectionInputs::StaticStruct()},
		{TEXT("/Game/Blueprints/SmartObjects/SmartObjectSelectionOutputs"), FVBGASPSmartObjectSelectionOutputs::StaticStruct()},
		{TEXT("/Game/Blueprints/AI/StateTree/S_SmartObjectPayload"), FVBGASPSmartObjectPayload::StaticStruct()}
	};
	for (const auto& Pair : Types)
	{
		UObject* OldType = LoadObject<UObject>(nullptr, Pair.Key);
		if (!OldType) return FString(TEXT("ERROR: missing source struct ")) + Pair.Key;
		Objects.Add(OldType, Pair.Value);
	}
	UPoseSearchSchema* Schema = CastChecked<UPoseSearchSchema>(Assets[0]);
	int32 DistanceChannels = 0;
	FArrayProperty* ChannelsProperty = FindFProperty<FArrayProperty>(UPoseSearchSchema::StaticClass(), TEXT("Channels"));
	FObjectPropertyBase* ChannelProperty = ChannelsProperty ? CastField<FObjectPropertyBase>(ChannelsProperty->Inner) : nullptr;
	if (!ChannelProperty) return TEXT("ERROR: schema channel authoring property unavailable");
	FScriptArrayHelper Channels(ChannelsProperty, ChannelsProperty->ContainerPtrToValuePtr<void>(Schema));
	for (int32 Index = 0; Index < Channels.Num(); ++Index)
	{
		UPoseSearchFeatureChannel* Channel = Cast<UPoseSearchFeatureChannel>(ChannelProperty->GetObjectPropertyValue(Channels.GetRawPtr(Index)));
		if (!Channel) return TEXT("ERROR: null schema channel");
		if (Channel->GetClass()->GetFName() == TEXT("DistanceToSmartObject_C"))
		{
			UVBGASPSmartObjectDistanceChannel* Native = NewObject<UVBGASPSmartObjectDistanceChannel>(Schema, NAME_None, RF_Transactional);
			TMap<FName, FName> Unused;
			CopyFields(Channel, Native, EVBGASPSmartObjectTaskOperation::FindSmartObject, Unused);
			ChannelProperty->SetObjectPropertyValue(Channels.GetRawPtr(Index), Native);
			Channel = Native;
		}
		if (Cast<UVBGASPSmartObjectDistanceChannel>(Channel)) ++DistanceChannels;
	}
	if (DistanceChannels != 1) return TEXT("ERROR: expected exactly one native distance channel");
	for (UObject* Asset : Assets) RemapPackage(Asset, Objects);
	Schema->PostEditChange();
	UBlueprint* SmartBase = CastChecked<UBlueprint>(Assets[10]);
	FKismetEditorUtilities::CompileBlueprint(SmartBase);
	UBlueprint* BenchBP = CastChecked<UBlueprint>(Assets[9]);
	if (BenchBP->SimpleConstructionScript) BenchBP->SimpleConstructionScript->ValidateSceneRootNodes();
	FBlueprintEditorUtils::RefreshAllNodes(BenchBP);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BenchBP);
	FKismetEditorUtilities::CompileBlueprint(BenchBP, EBlueprintCompileOptions::SkipSave |
		EBlueprintCompileOptions::UseDeltaSerializationDuringReinstancing | EBlueprintCompileOptions::SkipNewVariableDefaultsDetection);
	if (SmartBase->Status == BS_Error || BenchBP->Status == BS_Error) return TEXT("ERROR: SmartObject actor Blueprint compile failed");
	for (UObject* Asset : Assets)
	{
		if (UChooserTable* Chooser = Cast<UChooserTable>(Asset)) Chooser->Compile(true);
		if (UProxyTable* Proxy = Cast<UProxyTable>(Asset)) Proxy->PostEditChange();
		if (UStateTree* Tree = Cast<UStateTree>(Asset))
		{
			FString Error;
			if (!ConvertStateTree(Tree, SmartBase->GeneratedClass, Error)) return TEXT("ERROR: ") + Tree->GetName() + TEXT(": ") + Error;
			FStateTreeCompilerLog Log;
			FStateTreeCompiler Compiler(Log);
			if (!Compiler.Compile(*Tree)) { Log.DumpToLog(LogVBSmartObjectAuthoring); return TEXT("ERROR: StateTree compile: ") + Tree->GetName(); }
		}
	}
	UBlueprint* ControllerBP = NativeBlueprint(AVBGASPNPCController::StaticClass(), DestinationPath, TEXT("BP_VBGASPNPCController"));
	UBlueprint* NPCBP = NativeBlueprint(AVBGASPNPC::StaticClass(), DestinationPath, TEXT("BP_VBGASPNPC"));
	if (!ControllerBP || !NPCBP) return TEXT("ERROR: NPC Blueprint creation failed");
	FKismetEditorUtilities::CompileBlueprint(ControllerBP);
	FKismetEditorUtilities::CompileBlueprint(NPCBP);
	AVBGASPNPCController* Controller = Cast<AVBGASPNPCController>(ControllerBP->GeneratedClass->GetDefaultObject());
	AVBCharacter* Source = Cast<AVBCharacter>(CharacterReference->GeneratedClass->GetDefaultObject());
	AVBGASPNPC* NPC = Cast<AVBGASPNPC>(NPCBP->GeneratedClass->GetDefaultObject());
	if (!Controller || !Source || !NPC) return TEXT("ERROR: NPC class defaults unavailable");
	Controller->BehaviorStateTree = Cast<UStateTree>(Assets[7]);
	NPC->AIControllerClass = ControllerBP->GeneratedClass;
	NPC->GetMesh()->SetSkeletalMeshAsset(Source->GetMesh()->GetSkeletalMeshAsset());
	NPC->GetMesh()->SetAnimInstanceClass(Source->GetMesh()->GetAnimClass());
	NPC->GetMesh()->SetRelativeTransform(Source->GetMesh()->GetRelativeTransform());
	NPC->GetCapsuleComponent()->SetCapsuleSize(Source->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), Source->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
	if (!ConnectSmartObjectCharacter(NPCBP, DestinationPath)) return TEXT("ERROR: NPC SmartObject component is missing");
	ControllerBP->MarkPackageDirty();
	NPCBP->MarkPackageDirty();
	for (UObject* Asset : Assets) Asset->MarkPackageDirty();
	return TEXT("OK: project SmartObject assets cloned; StateTrees native and compiled; NPC classes connected. Search index must pass before player wiring.");
}

bool UVBGASPSmartObjectAuthoring::ConnectSmartObjectCharacter(UBlueprint* Blueprint, const FString& DestinationPath)
{
	if (!Blueprint || !Blueprint->GeneratedClass) return false;
	AActor* Actor = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
	UVBGASPSmartObjectComponent* Component = Actor ? Actor->FindComponentByClass<UVBGASPSmartObjectComponent>() : nullptr;
	if (!Component) return false;
	Component->PlayerStateTree = LoadObject<UStateTree>(nullptr, *ObjectPath(DestinationPath + TEXT("/ST_VBPlayerSmartObject")));
	Component->SelectionProxy = LoadObject<UProxyAsset>(nullptr, *ObjectPath(DestinationPath + TEXT("/CHPA_VBSmartObject")));
	Blueprint->MarkPackageDirty();
	return Component->PlayerStateTree && Component->SelectionProxy;
}

FString UVBGASPSmartObjectAuthoring::CheckSmartObjectSearchIndex(const FString& DestinationPath, bool bStartBuild)
{
	UChooserTable* Chooser = LoadObject<UChooserTable>(nullptr, *ObjectPath(DestinationPath + TEXT("/CHT_VBBenchAnim")));
	if (!Chooser) return TEXT("ERROR: missing bench Chooser");
	TArray<UObject*> Objects;
	GetObjectsWithOuter(Chooser, Objects, EGetObjectsFlags::IncludeNestedObjects);
	int32 Count = 0;
	bool bPending = false;
	for (UObject* Object : Objects)
	{
		UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(Object);
		if (!Database) continue;
		++Count;
		using namespace UE::PoseSearch;
		const auto Result = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, bStartBuild ? ERequestAsyncBuildFlag::NewRequest : ERequestAsyncBuildFlag::ContinueRequest);
		if (Result == EAsyncBuildIndexResult::InProgress) bPending = true;
		else if (Result != EAsyncBuildIndexResult::Success || Database->GetSearchIndex().GetNumPoses() <= 0) return TEXT("ERROR: native bench search index build failed");
	}
	if (Count != 1) return TEXT("ERROR: expected one embedded PoseSearch database");
	return bPending ? TEXT("PENDING") : TEXT("OK");
}

FString UVBGASPSmartObjectAuthoring::InspectSmartObjectAssets(const FString& DestinationPath)
{
	const TCHAR* Trees[] = { TEXT("ST_VBNPCPatrol"), TEXT("ST_VBSmartBench"), TEXT("ST_VBPlayerSmartObject"), TEXT("ST_VBNPCSmartObject") };
	int32 NativeNodes = 0;
	for (const TCHAR* Name : Trees)
	{
		UStateTree* Tree = LoadObject<UStateTree>(nullptr, *ObjectPath(DestinationPath / Name));
		UStateTreeEditorData* Editor = Tree ? Cast<UStateTreeEditorData>(Tree->EditorData) : nullptr;
		if (!Editor || !Tree->IsReadyToRun()) return FString(TEXT("ERROR: StateTree not compiled: ")) + Name;
		bool bHasBlueprintNode = false;
		auto InspectNode = [&](const FStateTreeEditorNode& Node)
		{
			if (!Node.InstanceObject) return;
			bHasBlueprintNode |= Node.InstanceObject->GetClass()->ClassGeneratedBy != nullptr;
			NativeNodes += Node.InstanceObject->IsA<UVBGASPSmartObjectTask>()
				|| Node.InstanceObject->IsA<UVBGASPGetAIDataEvaluator>() || Node.InstanceObject->IsA<UVBGASPCheckCooldownCondition>();
		};
		for (const auto& Node : Editor->Evaluators) InspectNode(Node);
		for (const auto& Node : Editor->GlobalTasks) InspectNode(Node);
		Editor->VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
		{
			for (const auto& Node : State.Tasks) InspectNode(Node);
			for (const auto& Node : State.EnterConditions) InspectNode(Node);
			InspectNode(State.SingleTask);
			for (const auto& Transition : State.Transitions) for (const auto& Node : Transition.Conditions) InspectNode(Node);
			return EStateTreeVisitor::Continue;
		});
		if (bHasBlueprintNode) return FString(TEXT("ERROR: executable Blueprint task remains: ")) + Name;
	}
	USmartObjectDefinition* Definition = LoadObject<USmartObjectDefinition>(nullptr, *ObjectPath(DestinationPath / TEXT("SO_VBBench")));
	UStateTree* BenchTree = LoadObject<UStateTree>(nullptr, *ObjectPath(DestinationPath / TEXT("ST_VBSmartBench")));
	if (!Definition || Definition->K2_GetSlots().Num() != 2) return TEXT("ERROR: expected two bench slots");
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const auto* Behavior = Cast<UGameplayInteractionSmartObjectBehaviorDefinition>(Definition->GetBehaviorDefinition(Index, UGameplayInteractionSmartObjectBehaviorDefinition::StaticClass()));
		if (!Behavior || Behavior->GetStateTree() != BenchTree) return TEXT("ERROR: bench behavior still references source StateTree");
	}
	UBlueprint* BenchBP = LoadObject<UBlueprint>(nullptr, *ObjectPath(DestinationPath / TEXT("BP_VBSmartBench")));
	UBlueprint* BaseBP = LoadObject<UBlueprint>(nullptr, *ObjectPath(DestinationPath / TEXT("BP_VBSmartObjectBase")));
	if (!BenchBP || !BaseBP || BenchBP->ParentClass != BaseBP->GeneratedClass) return TEXT("ERROR: bench project parent mismatch");
	TArray<UObject*> Templates;
	GetObjectsWithPackage(BenchBP->GetPackage(), Templates, EGetObjectsFlags::IncludeNestedObjects);
	bool bDefinitionConnected = false;
	for (UObject* Template : Templates)
		if (const auto* Component = Cast<USmartObjectComponent>(Template)) bDefinitionConnected |= Component->GetDefinition() == Definition;
	if (!bDefinitionConnected) return TEXT("ERROR: bench component is not wired to project definition");
	UBlueprint* NPCBP = LoadObject<UBlueprint>(nullptr, *ObjectPath(DestinationPath / TEXT("BP_VBGASPNPC")));
	UBlueprint* ControllerBP = LoadObject<UBlueprint>(nullptr, *ObjectPath(DestinationPath / TEXT("BP_VBGASPNPCController")));
	const auto* NPC = NPCBP && NPCBP->GeneratedClass ? Cast<AVBGASPNPC>(NPCBP->GeneratedClass->GetDefaultObject()) : nullptr;
	const auto* Controller = ControllerBP && ControllerBP->GeneratedClass ? Cast<AVBGASPNPCController>(ControllerBP->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!NPC || !Controller || NPCBP->Status == BS_Error || ControllerBP->Status == BS_Error
		|| !NPC->GetMesh()->GetSkeletalMeshAsset() || !NPC->GetMesh()->GetAnimClass() || NPC->AIControllerClass != ControllerBP->GeneratedClass
		|| Controller->BehaviorStateTree != LoadObject<UStateTree>(nullptr, *ObjectPath(DestinationPath / TEXT("ST_VBNPCSmartObject"))))
		return TEXT("ERROR: native NPC controller/tree/mesh/AnimClass connection mismatch");
	return FString::Printf(TEXT("OK: 4 compiled StateTrees, %d native task/evaluator/condition nodes, 2 bench slots, project definition connected; NPC mesh=%s AnimClass=%s"),
		NativeNodes, *NPC->GetMesh()->GetSkeletalMeshAsset()->GetPathName(), *NPC->GetMesh()->GetAnimClass()->GetPathName());
}

FString UVBGASPSmartObjectAuthoring::ValidateSmartObjectStateTreeCopies()
{
	const TCHAR* Sources[] = {
		TEXT("/Game/Blueprints/AI/StateTree/ST_NPC_SandboxCharacter_Patrol_Subtree"),
		TEXT("/Game/Blueprints/SmartObjects/Bench/ST_SmartObject_Bench"),
		TEXT("/Game/Blueprints/AI/StateTree/ST_Player_SandboxCharacter_SmartObject"),
		TEXT("/Game/Blueprints/AI/StateTree/ST_NPC_SandboxCharacter_SmartObject")
	};
	TStrongObjectPtr<UPackage> Scratch(CreatePackage(*(TEXT("/Temp/VBGASPStateTreeCopyCheck_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	Scratch->SetFlags(RF_Transient);
	TArray<TStrongObjectPtr<UStateTree>> Copies;
	TMap<UObject*, UObject*> Objects;
	for (const TCHAR* Path : Sources)
	{
		UStateTree* Source = LoadObject<UStateTree>(nullptr, Path);
		if (!Source) return FString(TEXT("ERROR: missing StateTree: ")) + Path;
		FString Error;
		UStateTree* Copy = CopyStateTreeAuthoring(Source, Scratch.Get(), Source->GetFName(), RF_Transient | RF_Transactional, Error);
		if (!Copy) return TEXT("ERROR: ") + Error;
		Copies.Emplace(Copy);
		Objects.Add(Source, Copy);
		auto* SourceEditor = CastChecked<UStateTreeEditorData>(Source->EditorData);
		auto* CopyEditor = CastChecked<UStateTreeEditorData>(Copy->EditorData);
		TArray<FGuid> SourceIDs, CopyIDs;
		auto CollectIDs = [](UStateTreeEditorData* Editor, TArray<FGuid>& IDs)
		{
			Editor->VisitAllNodes([&IDs](const UStateTreeState*, const FStateTreeBindableStructDesc& Desc, FStateTreeDataView)
			{ IDs.Add(Desc.ID); return EStateTreeVisitor::Continue; });
		};
		CollectIDs(SourceEditor, SourceIDs);
		CollectIDs(CopyEditor, CopyIDs);
		if (SourceIDs != CopyIDs) return FString(TEXT("ERROR: copy changed StateTree descriptor IDs: ")) + Path;
		const auto OriginalBindings = SourceEditor->EditorBindings.GetBindings();
		const auto CopiedBindings = CopyEditor->EditorBindings.GetBindings();
		if (OriginalBindings.Num() != CopiedBindings.Num()) return FString(TEXT("ERROR: copy changed binding count: ")) + Path;
		for (int32 Index = 0; Index < OriginalBindings.Num(); ++Index)
			if (OriginalBindings[Index].GetSourcePath().GetStructID() != CopiedBindings[Index].GetSourcePath().GetStructID()
				|| OriginalBindings[Index].GetTargetPath().GetStructID() != CopiedBindings[Index].GetTargetPath().GetStructID())
				return FString(TEXT("ERROR: copy changed binding IDs: ")) + Path;
	}
	const TPair<const TCHAR*, UScriptStruct*> Types[] = {
		{TEXT("/Game/Blueprints/SmartObjects/SmartObjectSelectionInputs"), FVBGASPSmartObjectSelectionInputs::StaticStruct()},
		{TEXT("/Game/Blueprints/SmartObjects/SmartObjectSelectionOutputs"), FVBGASPSmartObjectSelectionOutputs::StaticStruct()},
		{TEXT("/Game/Blueprints/AI/StateTree/S_SmartObjectPayload"), FVBGASPSmartObjectPayload::StaticStruct()}
	};
	for (const auto& Pair : Types)
	{
		UObject* Source = LoadObject<UObject>(nullptr, Pair.Key);
		if (!Source) return FString(TEXT("ERROR: missing source struct: ")) + Pair.Key;
		Objects.Add(Source, Pair.Value);
	}
	UBlueprint* Base = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/SmartObjects/BP_SmartObject_Base"));
	if (!Base || !Base->GeneratedClass) return TEXT("ERROR: missing source SmartObject class");
	// The package belongs only to these scratch copies; never scan /Engine/Transient.
	RemapPackage(Copies[0].Get(), Objects);
	for (const auto& Copy : Copies)
	{
		FString Error;
		if (!ConvertStateTree(Copy.Get(), Base->GeneratedClass, Error)) return TEXT("ERROR: ") + Error;
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(*Copy.Get()))
		{
			Log.DumpToLog(LogVBSmartObjectAuthoring);
			return TEXT("ERROR: scratch StateTree compile: ") + Copy->GetName();
		}
	}
	return TEXT("OK: 4 transient source copies preserved descriptor/binding IDs and compiled with native nodes; no assets saved or gameplay run.");
}
