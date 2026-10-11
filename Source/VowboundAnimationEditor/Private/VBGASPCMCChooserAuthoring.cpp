#include "VBGASPLocomotionAuthoring.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/VBGASPActiveDatabaseColumn.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "BoolColumn.h"
#include "Chooser.h"
#include "EnumColumn.h"
#include "FloatRangeColumn.h"
#include "ObjectChooser_Asset.h"
#include "OutputBoolColumn.h"
#include "OutputStructColumn.h"
#include "PoseSearch/Chooser/PoseSearchChooserColumn.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/AnimSequence.h"
#include "Misc/AutomationTest.h"
#include "PoseSearch/PoseSearchSchema.h"
#endif

namespace VBGASPCMCChooserAuthoring
{
struct FSearchEntry
{
	UPoseSearchDatabase* Database = nullptr;
	FPoseSearchDatabaseAnimationAsset Data;
};

TArray<FPoseSearchColumnRow>* PoseRows(FPoseSearchColumn& Column)
{
	// UE5.8 exposes the property but does not export GetRowValue/EditRowValue.
	const FArrayProperty* Property = FindFProperty<FArrayProperty>(FPoseSearchColumn::StaticStruct(), TEXT("RowValues"));
	const FStructProperty* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
	return Inner && Inner->Struct == FPoseSearchColumnRow::StaticStruct()
		? Property->ContainerPtrToValuePtr<TArray<FPoseSearchColumnRow>>(&Column) : nullptr;
}

const FArrayProperty* LocalDatabaseAssetsProperty()
{
	const FArrayProperty* Property = FindFProperty<FArrayProperty>(UPoseSearchDatabase::StaticClass(), TEXT("DatabaseAnimationAssets"));
	const FStructProperty* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
	return Inner && Inner->Struct == FPoseSearchDatabaseAnimationAsset::StaticStruct() ? Property : nullptr;
}

UChooserTable* Child(const FInstancedStruct& Result)
{
	if (const auto* Nested = Result.GetPtr<FNestedChooser>()) return Nested->Chooser;
	if (const auto* Evaluate = Result.GetPtr<FEvaluateChooser>()) return Evaluate->Chooser;
	return nullptr;
}

UObject* Asset(const FInstancedStruct& Result)
{
	if (const auto* Hard = Result.GetPtr<FAssetChooser>()) return Hard->Asset;
	if (const auto* Soft = Result.GetPtr<FSoftAssetChooser>()) return Soft->Asset.LoadSynchronous();
	return nullptr;
}

FInstancedStruct AssetResult(UObject* Object)
{
	FInstancedStruct Result = FInstancedStruct::Make<FAssetChooser>();
	Result.GetMutable<FAssetChooser>().Asset = Object;
	return Result;
}

FInstancedStruct NestedResult(UChooserTable* Table)
{
	FInstancedStruct Result = FInstancedStruct::Make<FNestedChooser>();
	Result.GetMutable<FNestedChooser>().Chooser = Table;
	return Result;
}

void SetSignature(UChooserTable* Root, UClass* ContextClass)
{
	Root->RootChooser = nullptr;
	Root->Version = UChooserTable::CurrentVersion;
	Root->OutputObjectType = UAnimationAsset::StaticClass();
	Root->ContextData.Reset();
	Root->ContextData.Add(FInstancedStruct::Make<FContextObjectTypeClass>());
	Root->ContextData[0].GetMutable<FContextObjectTypeClass>().Class = ContextClass;
	Root->ContextData.Add(FInstancedStruct::Make<FContextObjectTypeStruct>());
	auto& Output = Root->ContextData[1].GetMutable<FContextObjectTypeStruct>();
	Output.Struct = FVBGASPCMCChooserOutput::StaticStruct();
	Output.Direction = EContextObjectDirection::ReadWrite;
	Root->ContextData.Add(FInstancedStruct::Make<FContextObjectTypeStruct>());
	Root->ContextData[2].GetMutable<FContextObjectTypeStruct>().Struct = FPoseHistoryReference::StaticStruct();
}

template<typename T> FInstancedStruct BoundParameter(FName Field)
{
	FInstancedStruct Value = FInstancedStruct::Make<T>();
	auto& Binding = Value.GetMutable<T>().Binding;
	Binding.ContextIndex = 1;
	Binding.PropertyBindingChain = { Field };
	return Value;
}

FProperty* FindField(const FInstancedStruct& Value, const TCHAR* Prefix)
{
	if (!Value.IsValid()) return nullptr;
	for (TFieldIterator<FProperty> It(Value.GetScriptStruct()); It; ++It)
		if (It->GetName() == Prefix || It->GetName().StartsWith(FString(Prefix) + TEXT("_"))) return *It;
	return nullptr;
}

double Number(const FInstancedStruct& Value, const TCHAR* Name, double Default)
{
	const auto* Property = CastField<FNumericProperty>(FindField(Value, Name));
	if (!Property) return Default;
	const void* Address = Property->ContainerPtrToValuePtr<void>(Value.GetMemory());
	return Property->IsFloatingPoint() ? Property->GetFloatingPointPropertyValue(Address) : Property->GetSignedIntPropertyValue(Address);
}

void ReadMetadata(const FInstancedStruct& Value, FVBGASPCMCChooserOutput& Out, bool& UseMM)
{
	Out.StartTime = Number(Value, TEXT("StartTime"), Out.StartTime);
	Out.BlendTime = Number(Value, TEXT("BlendTime"), Out.BlendTime);
	Out.MaxAcceptedCost = Number(Value, TEXT("MMCostLimit"), Out.MaxAcceptedCost);
	if (const auto* Property = CastField<FBoolProperty>(FindField(Value, TEXT("UseMM"))))
		UseMM = Property->GetPropertyValue_InContainer(Value.GetMemory());
	if (const auto* Property = CastField<FNameProperty>(FindField(Value, TEXT("BlendProfile"))))
		Out.BlendProfileName = Property->GetPropertyValue_InContainer(Value.GetMemory());
	if (const auto* Property = CastField<FArrayProperty>(FindField(Value, TEXT("Tags"))))
	{
		if (CastField<FNameProperty>(Property->Inner))
			Out.Tags = *Property->ContainerPtrToValuePtr<TArray<FName>>(Value.GetMemory());
	}
}

struct FBuilder
{
	UChooserTable* Root;
	explicit FBuilder(UChooserTable* InRoot) : Root(InRoot) {}
	FString Error;
	TMap<UPoseSearchDatabase*, UPoseSearchDatabase*> Databases;
	TArray<UObject*> PendingPostLoad;
	TMap<UAnimationAsset*, TArray<FSearchEntry>> ReferenceEntries;
	TMap<UAnimationAsset*, TArray<FSearchEntry>> MigrationEntries;
	TSet<UChooserTable*> RecursionStack;
	UAnimationAsset* WalkSlope = nullptr;
	UAnimationAsset* RunSlope = nullptr;
	int32 SlopeRoutes = 0;

	UChooserTable* NewTable(FName Name)
	{
		auto* Table = NewObject<UChooserTable>(Root, MakeUniqueObjectName(Root, UChooserTable::StaticClass(), Name), RF_Transactional);
		Table->RootChooser = Root;
		Table->Version = UChooserTable::CurrentVersion;
		Table->OutputObjectType = UAnimationAsset::StaticClass();
		Root->AddNestedChooser(Table);
		return Table;
	}

	UPoseSearchDatabase* EmbeddedDatabase(UPoseSearchDatabase* Source)
	{
		if (!Source || !Source->Schema) { Error = TEXT("Pose Match source database has no schema"); return nullptr; }
		if (UPoseSearchDatabase** Existing = Databases.Find(Source)) return *Existing;
		const FArrayProperty* AssetsProperty = LocalDatabaseAssetsProperty();
		if (!AssetsProperty) { Error = TEXT("Pose Match database animation array contract changed"); return nullptr; }
		// Duplicate the database settings, including normalization, cost biases and sampling exclusions.
		// UE consults the Chooser mapping only when the local array is empty. Keeping a
		// copied array makes disabled/subset rows retain incompatible source indices.
		// Database::PostLoad requests async indexing. Defer it until every row and
		// mapping is final, so workers cannot index the half-authored duplicate.
		TMap<UObject*, UObject*> CreatedObjects;
		FObjectDuplicationParameters Parameters(Source, Root);
		Parameters.DestName = MakeUniqueObjectName(Root, UPoseSearchDatabase::StaticClass(), Source->GetFName());
		Parameters.bSkipPostLoad = true;
		Parameters.CreatedObjects = &CreatedObjects;
		auto* Copy = Cast<UPoseSearchDatabase>(StaticDuplicateObjectEx(Parameters));
		if (!Copy) { Error = TEXT("Failed to duplicate Pose Match database"); return nullptr; }
		for (const auto& Pair : CreatedObjects) PendingPostLoad.AddUnique(Pair.Value);
		FScriptArrayHelper(AssetsProperty, AssetsProperty->ContainerPtrToValuePtr<void>(Copy)).EmptyValues();
		Copy->ClearFlags(RF_Public | RF_Standalone);
		Root->AddNestedObject(Copy);
		Databases.Add(Source, Copy);
		return Copy;
	}

	void GatherReference(UChooserTable* Table, TSet<UChooserTable*>& Visited)
	{
		if (!Table || Visited.Contains(Table)) return;
		Visited.Add(Table);
		for (const FInstancedStruct& Column : Table->ColumnsStructs)
			if (const auto* Pose = Column.GetPtr<FPoseSearchColumn>())
				for (const FPoseSearchColumnRow& Entry : *PoseRows(const_cast<FPoseSearchColumn&>(*Pose)))
				{
					if (auto* Animation = Cast<UAnimationAsset>(Entry.Data.AnimAsset); Animation && Entry.Database)
						ReferenceEntries.FindOrAdd(Animation).Add({Entry.Database, Entry.Data});
				}
		for (const auto& Result : Table->ResultsStructs) GatherReference(Child(Result), Visited);
		GatherReference(Child(Table->FallbackResult), Visited);
	}

	TArray<FSearchEntry> FindEntries(UAnimationAsset* Animation)
	{
		TArray<FSearchEntry> Result;
		// CMC MotionMatch(animation assets) follows BranchIn databases. Preserve each authored range.
			if (const auto* Sequence = Cast<UAnimSequenceBase>(Animation))
				for (const FAnimNotifyEvent& Event : Sequence->Notifies)
					if (const auto* Branch = Cast<UAnimNotifyState_PoseSearchBranchIn>(Event.NotifyStateClass); Branch && Branch->Database)
						for (int32 Index = 0; Index < Branch->Database->GetNumAnimationAssets(); ++Index)
						{
							const auto* Data = Branch->Database->GetDatabaseAnimationAsset(Index);
							if (Data && Data->AnimAsset == Animation && Data->bEnabled &&
								!Result.ContainsByPredicate([&](const FSearchEntry& Other) { return Other.Database == Branch->Database && Other.Data == *Data; }))
								Result.Add({Branch->Database, *Data});
						}
		// Some migrated CMC traversal sequences lost their BranchIn notifies, while
		// the original transition DB still contains the exact authored range/mirror.
		// Only the explicitly supplied migration reference is allowed to repair this.
		if (Result.IsEmpty())
			if (const auto* Entries = MigrationEntries.Find(Animation)) Result = *Entries;
		// Slope BlendSpaces use the original PoseMatch table's explicit sampling contract.
		if (Result.IsEmpty())
			if (const auto* Entries = ReferenceEntries.Find(Animation))
				for (const auto& Entry : *Entries)
					if (!Result.ContainsByPredicate([&](const FSearchEntry& Other) { return Other.Database == Entry.Database && Other.Data == Entry.Data; })) Result.Add(Entry);
		if (Result.IsEmpty()) Error = FString::Printf(TEXT("No authored Pose Match sampling contract for %s"), *GetPathNameSafe(Animation));
		return Result;
	}

	void AddPoseColumn(UChooserTable* Table, const TArray<FSearchEntry>& Entries)
	{
		Table->ColumnsStructs.Add(FInstancedStruct::Make<FPoseSearchColumn>());
		auto& Pose = Table->ColumnsStructs.Last().GetMutable<FPoseSearchColumn>();
		FChooserColumnBase& Base = Pose; // Editor operations are public through the base interface.
		Base.Initialize(Table);
		Base.SetNumRows(Table->ResultsStructs.Num());
		Pose.PoseReselectHistory = 0.0f;
		Pose.MaxNumberOfResults = 1;
		Pose.InterruptMode = BoundParameter<FEnumContextProperty>(TEXT("InterruptMode"));
		Pose.InterruptMode.GetMutable<FEnumContextProperty>().Binding.Enum = StaticEnum<EPoseSearchInterruptMode>();
		Pose.InputValue = FInstancedStruct::Make<FPoseHistoryContextProperty>();
		Pose.InputValue.GetMutable<FPoseHistoryContextProperty>().Binding.ContextIndex = 2;
		Pose.InputValue.GetMutable<FPoseHistoryContextProperty>().Binding.IsBoundToRoot = true;
		Pose.OutputStartTime = BoundParameter<FFloatContextProperty>(TEXT("StartTime"));
		Pose.OutputCost = BoundParameter<FFloatContextProperty>(TEXT("SearchCost"));
		Pose.OutputMirror = BoundParameter<FBoolContextProperty>(TEXT("bMirror"));
		Pose.OutputForceBlendTo = BoundParameter<FBoolContextProperty>(TEXT("bForceBlend"));
		for (int32 Row = 0; Row < Entries.Num(); ++Row)
		{
			if (!Entries[Row].Database) continue;
			auto& Value = (*PoseRows(Pose))[Row];
			Value.Data = Entries[Row].Data;
			Value.Database = EmbeddedDatabase(Entries[Row].Database);
		}
	}

	UChooserTable* MakeSlopeRoute(UAnimationAsset* Flat, UAnimationAsset* Slope, FVBGASPCMCChooserOutput Output, bool UseMM, bool Startup)
	{
		UClass* ContextClass = Root->ContextData[0].Get<FContextObjectTypeClass>().Class;
		FProperty* RotationMode = ContextClass->FindPropertyByName(TEXT("RotationMode"));
		const FByteProperty* Byte = CastField<FByteProperty>(RotationMode);
		const FEnumProperty* EnumProperty = CastField<FEnumProperty>(RotationMode);
		UEnum* Enum = Byte ? Byte->Enum.Get() : EnumProperty ? EnumProperty->GetEnum() : nullptr;
		const int64 OrientToMovement = Enum ? Enum->GetValueByNameString(TEXT("NewEnumerator0")) : INDEX_NONE;
		if (OrientToMovement == INDEX_NONE) { Error = TEXT("CMC RotationMode OrientToMovement enum contract is missing"); return nullptr; }
		UChooserTable* Table = NewTable(FName(Flat->GetName() + TEXT("_SlopeRoute")));
		// Let the engine UScriptStruct construct this column: locally emitting its
		// vtable references UE5.8's unexported FChooserEnumRowData::Evaluate.
		FInstancedStruct GateValue;
		GateValue.InitializeAs(FEnumColumn::StaticStruct());
		FEnumColumn& Gate = GateValue.GetMutable<FEnumColumn>();
		Gate.InputValue = FInstancedStruct::Make<FEnumContextProperty>();
		auto& Binding = Gate.InputValue.GetMutable<FEnumContextProperty>().Binding;
		Binding.ContextIndex = 0;
		Binding.PropertyBindingChain = { TEXT("RotationMode") };
		Binding.Enum = Enum;
		FVBGASPCMCMetadataColumn Metadata;
		TArray<FSearchEntry> Entries;
		for (int32 Variant = 0; Variant < 2; ++Variant)
		{
			UAnimationAsset* Animation = Variant == 0 ? Slope : Flat;
			TArray<FSearchEntry> RowEntries;
			if (UseMM && !Startup) RowEntries = FindEntries(Animation);
			else RowEntries.AddDefaulted();
			for (const FSearchEntry& Entry : RowEntries)
			{
				Table->ResultsStructs.Add(AssetResult(Animation));
				FChooserEnumRowData Cell;
				Cell.Comparison = Variant == 0 ? EEnumColumnCellValueComparison::MatchEqual : EEnumColumnCellValueComparison::MatchNotEqual;
				Cell.Value = static_cast<uint8>(OrientToMovement);
				Cell.ValueName = TEXT("NewEnumerator0");
				Gate.RowValues.Add(Cell);
				FVBGASPCMCChooserOutput RowOutput = Output;
				if (Variant == 0) RowOutput.Tags.AddUnique(TEXT("BS_Slope"));
				Metadata.RowValues.Add(RowOutput);
				Entries.Add(Entry);
			}
		}
		Table->ColumnsStructs.Add(MoveTemp(GateValue));
		Table->ColumnsStructs.Add(FInstancedStruct::Make<FVBGASPCMCMetadataColumn>(Metadata));
		if (UseMM && !Startup && Error.IsEmpty()) AddPoseColumn(Table, Entries);
		++SlopeRoutes;
		return Table;
	}

	UChooserTable* CloneCMC(UChooserTable* Source, FVBGASPCMCChooserOutput Inherited, bool InheritedMM, bool Startup)
	{
		if (!Source || RecursionStack.Contains(Source)) { Error = TEXT("Null or cyclic CMC chooser reference"); return nullptr; }
		RecursionStack.Add(Source);
		UChooserTable* Table = NewTable(Source->GetFName());
		TArray<int32> SourceRows;
		TArray<FSearchEntry> Entries;
		FVBGASPCMCMetadataColumn Metadata;
		bool HasSearch = false;
		bool HasNonSearch = false;
		for (int32 Row = 0; Row < Source->ResultsStructs.Num() && Error.IsEmpty(); ++Row)
		{
			FVBGASPCMCChooserOutput Output = Inherited;
			bool UseMM = InheritedMM;
			for (const auto& Column : Source->ColumnsStructs)
				if (const auto* StructOutput = Column.GetPtr<FOutputStructColumn>(); StructOutput && !StructOutput->bDisabled && StructOutput->RowValues.IsValidIndex(Row))
					ReadMetadata(StructOutput->RowValues[Row], Output, UseMM);
			Output.bFallback = Startup;
			Output.bPoseMatched = UseMM && !Startup;
			const FInstancedStruct& SourceResult = Source->ResultsStructs[Row];
			FInstancedStruct Result = SourceResult;
			TArray<FSearchEntry> RowEntries;
			if (auto* Nested = Child(SourceResult))
			{
				Result = NestedResult(CloneCMC(Nested, Output, UseMM, Startup));
				if (!Source->IsRowDisabled(Row)) HasNonSearch = true;
			}
			else if (auto* Animation = Cast<UAnimationAsset>(Asset(SourceResult)))
			{
				UAnimationAsset* Replacement = nullptr;
				if (Animation->GetName() == TEXT("M_Neutral_Walk_Loop_F") || Animation->GetName() == TEXT("M_Relaxed_Walk_Loop_F")) Replacement = WalkSlope;
				if (Animation->GetName() == TEXT("M_Neutral_Run_Loop_F") || Animation->GetName() == TEXT("M_Relaxed_Run_Loop_F")) Replacement = RunSlope;
				if (Replacement)
				{
					Result = NestedResult(MakeSlopeRoute(Animation, Replacement, Output, UseMM, Startup));
					HasNonSearch = true;
				}
				else if (UseMM && !Startup && !Source->IsRowDisabled(Row))
				{
					RowEntries = FindEntries(Animation);
					HasSearch = true;
				}
				else if (!Source->IsRowDisabled(Row)) HasNonSearch = true;
			}
			else if (Asset(SourceResult)) Error = FString::Printf(TEXT("Unsupported CMC result %s"), *GetPathNameSafe(Asset(SourceResult)));
			if (RowEntries.IsEmpty()) RowEntries.AddDefaulted();
			for (const FSearchEntry& Entry : RowEntries)
			{
				SourceRows.Add(Row);
				Table->ResultsStructs.Add(Result);
				Table->DisabledRows.Add(Source->IsRowDisabled(Row));
				Metadata.RowValues.Add(Output);
				Entries.Add(Entry);
			}
		}
		for (const auto& Original : Source->ColumnsStructs)
		{
			if (Original.GetPtr<FOutputStructColumn>()) continue;
			if (Original.GetPtr<FPoseSearchColumn>()) { Error = TEXT("CMC source must be the original non-PoseMatch chooser"); break; }
			Table->ColumnsStructs.Add(Original);
			auto& Copy = Table->ColumnsStructs.Last().GetMutable<FChooserColumnBase>();
			Copy.SetNumRows(SourceRows.Num());
			// Read from the source, never from a resized copy when rows were expanded.
			for (int32 Row = 0; Row < SourceRows.Num(); ++Row)
				Copy.CopyRow(const_cast<FChooserColumnBase&>(Original.Get<FChooserColumnBase>()), SourceRows[Row], Row);
		}
		Metadata.FallbackValue = Inherited;
		Metadata.FallbackValue.bFallback = Startup;
		Metadata.FallbackValue.bPoseMatched = false;
		for (const auto& Column : Source->ColumnsStructs)
			if (const auto* StructOutput = Column.GetPtr<FOutputStructColumn>())
			{
				bool IgnoredMM = false;
				ReadMetadata(StructOutput->FallbackValue, Metadata.FallbackValue, IgnoredMM);
			}
		Table->ColumnsStructs.Add(FInstancedStruct::Make<FVBGASPCMCMetadataColumn>(Metadata));
		if (Source->FallbackResult.IsValid())
		{
			if (auto* Nested = Child(Source->FallbackResult)) Table->FallbackResult = NestedResult(CloneCMC(Nested, Metadata.FallbackValue, false, Startup));
			else Table->FallbackResult = Source->FallbackResult;
		}
		if (HasSearch && Error.IsEmpty())
		{
			if (HasNonSearch)
			{
				// Keep deterministic rows and their original priority. Each matching search row
				// enters the same search leaf, which re-applies every original filter to the
				// complete searchable subset before one Pose Match selection.
				UChooserTable* Search = NewTable(FName(Source->GetName() + TEXT("_PoseMatch")));
				Search->ResultsStructs = Table->ResultsStructs;
				Search->ColumnsStructs = Table->ColumnsStructs;
				Search->DisabledRows = Table->DisabledRows;
				for (int32 Row = 0; Row < Entries.Num(); ++Row)
				{
					if (Entries[Row].Database) Table->ResultsStructs[Row] = NestedResult(Search);
					else Search->DisabledRows[Row] = true;
				}
				AddPoseColumn(Search, Entries);
			}
			else AddPoseColumn(Table, Entries);
		}
		RecursionStack.Remove(Source);
		return Table;
	}

	bool CollectDatabases(UChooserTable* Table, TSet<UChooserTable*>& Visited, TArray<UPoseSearchDatabase*>& Out)
	{
		if (!Table || Visited.Contains(Table)) return true;
		Visited.Add(Table);
		TArray<FInstancedStruct> Results = Table->ResultsStructs;
		Results.Add(Table->FallbackResult);
		for (int32 Row = 0; Row < Results.Num(); ++Row)
		{
			if (Row < Table->ResultsStructs.Num() && Table->DisabledRows.IsValidIndex(Row) && Table->DisabledRows[Row]) continue;
			const FInstancedStruct& Result = Results[Row];
			if (!Result.IsValid()) continue;
			if (Result.GetPtr<FNestedChooser>() || Result.GetPtr<FEvaluateChooser>())
			{
				// Both engine result types evaluate a null Chooser as Failed/no result.
				if (!CollectDatabases(Child(Result), Visited, Out)) return false;
				continue;
			}
			UObject* Object = Asset(Result);
			if (auto* Database = Cast<UPoseSearchDatabase>(Object)) Out.AddUnique(Database);
			else if (Object)
			{
				Error = FString::Printf(TEXT("Weapon MasterChooser %s row %d (%s) contains non-database %s"),
					*Table->GetPathName(), Row, *Result.GetScriptStruct()->GetName(), *Object->GetPathName());
				return false;
			}
			else if (!Result.GetPtr<FAssetChooser>() && !Result.GetPtr<FSoftAssetChooser>())
			{
				Error = FString::Printf(TEXT("Weapon MasterChooser %s row %d contains unsupported dynamic result %s"),
					*Table->GetPathName(), Row, *Result.GetScriptStruct()->GetPathName());
				return false;
			}
		}
		return true;
	}

	void Finish()
	{
		if (!Error.IsEmpty()) return;
		// The exported engine hook rebuilds the mapping after the complete root is wired.
		for (UObject* Object : Root->NestedObjects)
			if (UChooserTable* Table = Cast<UChooserTable>(Object))
				for (auto& Column : Table->ColumnsStructs)
					if (auto* Pose = Column.GetMutablePtr<FPoseSearchColumn>()) Pose->PostLoad();
		for (UObject* Object : Root->NestedObjects)
			if (UChooserTable* Table = Cast<UChooserTable>(Object)) Table->Compile(true);
		Root->Compile(true);
		Error = UVBGASPLocomotionAuthoring::ValidateCMCAnimationChooser(Root);
		if (!Error.IsEmpty()) return;
		for (UObject* Object : PendingPostLoad)
			if (Object && !Object->IsTemplate()) Object->ConditionalPostLoad();
		for (const auto& Pair : Databases)
			UE::PoseSearch::FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Pair.Value, UE::PoseSearch::ERequestAsyncBuildFlag::ContinueRequest);
		Root->MarkPackageDirty();
	}
};

void Reset(UChooserTable* Destination, UClass* ContextClass)
{
	Destination->Modify();
	Destination->ResultsStructs.Reset();
	Destination->ColumnsStructs.Reset();
	Destination->DisabledRows.Reset();
	Destination->FallbackResult.Reset();
	Destination->NestedChoosers.Reset();
	Destination->NestedObjects.Reset();
	SetSignature(Destination, ContextClass);
}

void AddHistoryRoutes(UChooserTable* Root, UChooserTable* Search, UChooserTable* Startup)
{
	Root->ResultsStructs = { NestedResult(Search), NestedResult(Startup) };
	FBoolColumn Gate;
	Gate.InputValue = BoundParameter<FBoolContextProperty>(TEXT("bHistoryReady"));
	Gate.RowValuesWithAny = { EBoolColumnCellValue::MatchTrue, EBoolColumnCellValue::MatchFalse };
	Root->ColumnsStructs.Add(FInstancedStruct::Make<FBoolColumn>(Gate));
}

FString BindWeaponQueryColumn(FInstancedStruct& Column, bool bConvert)
{
	FChooserPropertyBinding* Binding = nullptr;
	const UEnum* Enum = nullptr;
	enum class EKind { Bool, Number, Enum } Kind = EKind::Bool;
	if (auto* Bool = Column.GetMutablePtr<FBoolColumn>())
	{
		if (auto* Input = Bool->InputValue.GetMutablePtr<FBoolContextProperty>()) Binding = &Input->Binding;
	}
	else if (auto* Number = Column.GetMutablePtr<FFloatRangeColumn>())
	{
		Kind = EKind::Number;
		if (auto* Input = Number->InputValue.GetMutablePtr<FFloatContextProperty>()) Binding = &Input->Binding;
	}
	else if (auto* EnumColumn = Column.GetMutablePtr<FEnumColumn>())
	{
		Kind = EKind::Enum;
		if (auto* Input = EnumColumn->InputValue.GetMutablePtr<FEnumContextProperty>())
		{
			Binding = &Input->Binding;
			Enum = Input->Binding.Enum;
		}
	}
	if (!Binding || Binding->ContextIndex != 0 || Binding->IsBoundToRoot)
		return FString::Printf(TEXT("Unsupported CMC candidate column/binding: %s"), *GetPathNameSafe(Column.GetScriptStruct()));
	TArray<FName> Chain = Binding->PropertyBindingChain;
	if (bConvert && Chain == TArray<FName>{TEXT("TrajectoryCollision"), TEXT("TimeToLand")}) Chain = {TEXT("TrajectoryTimeToLand")};
	const FProperty* Property = Chain.Num() == 1 ? FindFProperty<FProperty>(FVBGASPCMCWeaponQuery::StaticStruct(), Chain[0]) : nullptr;
	bool bValid = Kind == EKind::Bool ? CastField<FBoolProperty>(Property) != nullptr :
		Kind == EKind::Number ? CastField<FNumericProperty>(Property) != nullptr : false;
	if (Kind == EKind::Enum)
	{
		const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property);
		bValid = EnumProperty && EnumProperty->GetEnum() == Enum;
	}
	if (!bValid) return FString::Printf(TEXT("CMC candidate snapshot cannot represent binding %s"),
		Chain.IsEmpty() ? TEXT("<empty>") : *Chain.Last().ToString());
	if (bConvert) Binding->PropertyBindingChain = MoveTemp(Chain);
	return {};
}

FString ValidateWeaponCandidateChooser(UChooserTable* Candidate)
{
	if (!Candidate || Candidate->GetRootChooser() != Candidate || Candidate->OutputObjectType != UPoseSearchDatabase::StaticClass())
		return TEXT("CMC weapon candidate chooser is missing or has an invalid result type");
	const auto Signature = Candidate->GetContextData();
	const auto* Type = Signature.Num() == 1 ? Signature[0].GetPtr<FContextObjectTypeStruct>() : nullptr;
	if (!Type || Type->Struct != FVBGASPCMCWeaponQuery::StaticStruct() || Type->Direction != EContextObjectDirection::Read)
		return TEXT("CMC weapon candidate chooser requires one native read-only snapshot");
	TArray<UChooserTable*> Tables = {Candidate};
	for (UObject* Object : Candidate->NestedObjects)
		if (auto* Table = Cast<UChooserTable>(Object)) Tables.Add(Table);
	for (UChooserTable* Table : Tables)
	{
		if (Table->GetRootChooser() != Candidate) return TEXT("CMC candidate child has a foreign root");
		for (FInstancedStruct& Column : Table->ColumnsStructs)
		{
			const FString Error = BindWeaponQueryColumn(Column, false);
			if (!Error.IsEmpty()) return Error;
		}
	}
	return {};
}

// The database selector already authors these action roles through its filters.
// Preserve that semantic contract without guessing from database/sequence names.
struct FWeaponRowRole
{
	bool bIdle = false;
	bool bResidualSpeed = false;
	FName Action;
};

FString GatherWeaponRoles(UChooserTable* Table, FWeaponRowRole Inherited,
	TMap<UPoseSearchDatabase*, FName>& Roles, TSet<UChooserTable*>& Stack)
{
	if (!Table || Stack.Contains(Table)) return TEXT("Invalid/cyclic CMC role source");
	Stack.Add(Table);
	TArray<FInstancedStruct> Results = Table->ResultsStructs;
	Results.Add(Table->FallbackResult);
	for (int32 Row = 0; Row < Results.Num(); ++Row)
	{
		const bool bFallback = Row == Table->ResultsStructs.Num();
		if (!bFallback && Table->DisabledRows.IsValidIndex(Row) && Table->DisabledRows[Row]) continue;
		FWeaponRowRole Role = Inherited;
		for (const FInstancedStruct& Column : Table->ColumnsStructs)
		{
			// A fallback inherits parent gates, never this table's row filters.
			if (bFallback) break;
			if (Column.Get<FChooserColumnBase>().bDisabled) continue;
			if (const auto* Bool = Column.GetPtr<FBoolColumn>())
			{
				const auto* Input = Bool->InputValue.GetPtr<FBoolContextProperty>();
				if (!Input || Input->Binding.PropertyBindingChain.Num() != 1 || !Bool->RowValuesWithAny.IsValidIndex(Row) ||
					Bool->RowValuesWithAny[Row] != EBoolColumnCellValue::MatchTrue) continue;
				const FName Field = Input->Binding.PropertyBindingChain[0];
				FName Action;
				if (Field == TEXT("IsStarting")) Action = TEXT("Start");
				else if (Field == TEXT("IsPivoting")) Action = TEXT("Pivot");
				else if (Field == TEXT("ShouldTurnInPlace")) Action = TEXT("TurnInPlace");
				else if (Field == TEXT("ShouldSpinTransition")) Action = TEXT("SpinTransition");
				if (!Action.IsNone())
				{
					if (!Role.Action.IsNone() && Role.Action != Action) return TEXT("Conflicting CMC role filters on a source row");
					Role.Action = Action;
				}
			}
			else if (const auto* EnumColumn = Column.GetPtr<FEnumColumn>())
			{
				const auto* Input = EnumColumn->InputValue.GetPtr<FEnumContextProperty>();
				if (Input && Input->Binding.PropertyBindingChain == TArray<FName>{TEXT("MMState")} &&
					EnumColumn->RowValues.IsValidIndex(Row) && EnumColumn->RowValues[Row].Comparison == EEnumColumnCellValueComparison::MatchEqual)
					Role.bIdle = EnumColumn->RowValues[Row].Value == static_cast<uint8>(EVBMMState::Idle);
			}
			else if (const auto* Number = Column.GetPtr<FFloatRangeColumn>())
			{
				const auto* Input = Number->InputValue.GetPtr<FFloatContextProperty>();
				if (Input && Input->Binding.PropertyBindingChain == TArray<FName>{TEXT("Speed2D")} && Number->RowValues.IsValidIndex(Row))
					Role.bResidualSpeed = !Number->RowValues[Row].bNoMin && Number->RowValues[Row].Min > 0.f;
			}
		}
		const FInstancedStruct& Result = Results[Row];
		if (UChooserTable* Nested = Child(Result))
		{
			const FString Error = GatherWeaponRoles(Nested, Role, Roles, Stack);
			if (!Error.IsEmpty()) return Error;
		}
		else if (auto* Database = Cast<UPoseSearchDatabase>(Asset(Result)))
		{
			if (Role.Action.IsNone() && Role.bIdle && Role.bResidualSpeed) Role.Action = TEXT("Stop");
			if (const FName* Existing = Roles.Find(Database); Existing && *Existing != Role.Action)
				return FString::Printf(TEXT("CMC source database has conflicting authored roles: %s"), *Database->GetPathName());
			Roles.Add(Database, Role.Action);
		}
	}
	Stack.Remove(Table);
	return {};
}

FString BuildWeaponCandidateChooser(UChooserTable* Source, UChooserTable* Owner, UClass* ContextClass,
	UChooserTable*& Candidate, TMap<UPoseSearchDatabase*, FName>& Roles)
{
	TSet<UChooserTable*> RoleStack;
	FString Error = GatherWeaponRoles(Source, {}, Roles, RoleStack);
	if (!Error.IsEmpty()) return Error;
	Candidate = NewObject<UChooserTable>(Owner, MakeUniqueObjectName(Owner, UChooserTable::StaticClass(), TEXT("CMCWeaponCandidates")), RF_Transactional);
	Owner->AddNestedObject(Candidate);
	Error = UVBGASPLocomotionAuthoring::BuildCMCCompatibilityDatabaseChooser(Source, Candidate, ContextClass);
	if (!Error.IsEmpty()) return Error;
	Candidate->ContextData = {FInstancedStruct::Make<FContextObjectTypeStruct>()};
	auto& Context = Candidate->ContextData[0].GetMutable<FContextObjectTypeStruct>();
	Context.Struct = FVBGASPCMCWeaponQuery::StaticStruct();
	Context.Direction = EContextObjectDirection::Read;
	TArray<UChooserTable*> Tables = {Candidate};
	for (UObject* Object : Candidate->NestedObjects)
		if (auto* Table = Cast<UChooserTable>(Object)) Tables.Add(Table);
	for (UChooserTable* Table : Tables)
	{
		for (FInstancedStruct& Column : Table->ColumnsStructs)
		{
			Error = BindWeaponQueryColumn(Column, true);
			if (!Error.IsEmpty()) return Error;
		}
		Table->Compile(true);
	}
	return ValidateWeaponCandidateChooser(Candidate);
}
}

FString UVBGASPLocomotionAuthoring::BuildCMCAnimationChooser(UChooserTable* Source, UChooserTable* Destination,
	UClass* ContextClass, UChooserTable* PoseMatchReference, UAnimationAsset* WalkSlope, UAnimationAsset* RunSlope, UPoseSearchDatabase* TransitionReference)
{
	using namespace VBGASPCMCChooserAuthoring;
	if (!IsInGameThread() || !Source || !Destination || Source == Destination || !ContextClass || !PoseMatchReference || !WalkSlope || !RunSlope || !TransitionReference || !TransitionReference->Schema)
		return TEXT("Missing CMC authoring input or source/destination alias");
	Reset(Destination, ContextClass);
	FBuilder Builder{Destination};
	Builder.WalkSlope = WalkSlope;
	Builder.RunSlope = RunSlope;
	TSet<UChooserTable*> Visited;
	Builder.GatherReference(PoseMatchReference, Visited);
	for (int32 Index = 0; Index < TransitionReference->GetNumAnimationAssets(); ++Index)
	{
		const auto* Data = TransitionReference->GetDatabaseAnimationAsset(Index);
		if (Data && Data->bEnabled)
			if (auto* Animation = Cast<UAnimationAsset>(Data->AnimAsset))
				Builder.MigrationEntries.FindOrAdd(Animation).Add({TransitionReference, *Data});
	}
	UChooserTable* Search = Builder.CloneCMC(Source, {}, false, false);
	if (!Builder.Error.IsEmpty()) return Builder.Error;
	UChooserTable* Startup = Builder.CloneCMC(Source, {}, false, true);
	if (!Builder.Error.IsEmpty()) return Builder.Error;
	if (Builder.SlopeRoutes < 4) return TEXT("Expected forward walk/run slope routes in both search and startup branches");
	AddHistoryRoutes(Destination, Search, Startup);
	Builder.Finish();
	return Builder.Error;
}

FString UVBGASPLocomotionAuthoring::ValidateCMCAnimationChooser(UChooserTable* Root, bool bValidateSearchIndex)
{
	using namespace VBGASPCMCChooserAuthoring;
	if (!IsInGameThread() || !Root || Root->GetRootChooser() != Root || Root->IsCookedData())
		return TEXT("CMC validation requires an uncooked root Chooser on the game thread");
	const FArrayProperty* LocalAssets = LocalDatabaseAssetsProperty();
	const FStructProperty* Fallback = FindFProperty<FStructProperty>(FPoseSearchColumn::StaticStruct(), TEXT("FallbackValue"));
	if (!LocalAssets || !Fallback || Fallback->Struct != FPoseSearchColumnRow::StaticStruct())
		return TEXT("CMC database or Pose Match reflection contract changed");

	TMap<UPoseSearchDatabase*, TArray<FPoseSearchDatabaseAnimationAsset>> Expected;
	TSet<UChooserTable*> Visited;
	FString Error;
	TFunction<void(UChooserTable*)> Visit;
	Visit = [&](UChooserTable* Table)
	{
		if (!Error.IsEmpty() || Visited.Contains(Table)) return;
		if (!Table || Table->GetRootChooser() != Root || Table->IsCookedData())
		{ Error = TEXT("CMC nested table has an invalid root or cooked data"); return; }
		Visited.Add(Table);
		TArray<FPoseSearchColumn*> Columns;
		for (const FInstancedStruct& Column : Table->ColumnsStructs)
			if (const auto* Active = Column.GetPtr<FVBGASPActiveDatabaseColumn>())
			{
				Error = ValidateWeaponCandidateChooser(Active->CandidateChooser);
				if (!Error.IsEmpty()) return;
				TMap<UPoseSearchDatabase*, FName> Roles;
				TSet<UChooserTable*> RoleStack;
				Error = GatherWeaponRoles(Active->CandidateChooser, {}, Roles, RoleStack);
				if (!Error.IsEmpty()) return;
				const FVBGASPCMCMetadataColumn* Metadata = nullptr;
				for (const FInstancedStruct& Other : Table->ColumnsStructs)
					if (const auto* Value = Other.GetPtr<FVBGASPCMCMetadataColumn>()) Metadata = Value;
				if (Active->SourceDatabases.Num() != Table->ResultsStructs.Num() ||
					Active->LoopingRows.Num() != Table->ResultsStructs.Num() || !Metadata || Metadata->RowValues.Num() != Table->ResultsStructs.Num())
				{ Error = FString::Printf(TEXT("CMC weapon phase row count mismatch: %s"), *Table->GetPathName()); return; }
				for (int32 Row = 0; Row < Table->ResultsStructs.Num(); ++Row)
				{
					bool bLoop = false;
					UAnimationAsset* Animation = Cast<UAnimationAsset>(Asset(Table->ResultsStructs[Row]));
					UPoseSearchLibrary::IsAnimationAssetLooping(Animation, bLoop);
					if (!Animation || !Active->SourceDatabases[Row] || Active->LoopingRows[Row] != bLoop)
					{ Error = FString::Printf(TEXT("CMC weapon phase data mismatch: %s row %d"), *Table->GetPathName(), Row); return; }
					TArray<FName> Tags = Active->SourceDatabases[Row]->Tags;
					const FName* Role = Roles.Find(Active->SourceDatabases[Row]);
					if (!Role) { Error = TEXT("CMC source database is missing its authored candidate role"); return; }
					if (!Role->IsNone() && !bLoop) Tags.AddUnique(*Role);
					if (Tags != Metadata->RowValues[Row].Tags)
					{ Error = FString::Printf(TEXT("CMC role metadata mismatch: %s row %d"), *Table->GetPathName(), Row); return; }
				}
			}
		for (FInstancedStruct& Column : Table->ColumnsStructs)
			if (auto* Pose = Column.GetMutablePtr<FPoseSearchColumn>())
			{
				const auto* Rows = PoseRows(*Pose);
				if (!Rows || Rows->Num() != Table->ResultsStructs.Num() || Pose->GetChooser() != Table)
				{ Error = FString::Printf(TEXT("CMC Pose Match row/owner mismatch: %s"), *Table->GetPathName()); return; }
				Columns.Add(Pose);
			}
		// Match UE5.8 FMapping::Init traversal: depth-first nested results, enabled
		// rows in order, columns in order, then fallback. Do not deduplicate assets:
		// repeated sequences can have distinct sampling/mirror contracts.
		auto Process = [&](const FInstancedStruct& Result, int32 Row)
		{
			if (!Result.IsValid() || !Error.IsEmpty()) return;
			if (const auto* Nested = Result.GetPtr<FNestedChooser>()) { Visit(Nested->Chooser); return; }
			if (Result.GetPtr<FEvaluateChooser>())
			{ Error = TEXT("CMC Pose Match mapping requires nested Choosers, not EvaluateChooser results"); return; }
			for (FPoseSearchColumn* Pose : Columns)
			{
				const FPoseSearchColumnRow& Value = Row == ChooserColumn_SpecialIndex_Fallback
					? *Fallback->ContainerPtrToValuePtr<FPoseSearchColumnRow>(Pose) : (*PoseRows(*Pose))[Row];
				if (!Value.Database) continue;
				if (Value.Database->GetOuter() != Root || !Value.Database->Schema
					|| !Cast<UAnimationAsset>(Value.Data.AnimAsset) || Asset(Result) != Value.Data.AnimAsset)
				{ Error = FString::Printf(TEXT("CMC database/schema/result mismatch: %s row %d"), *Table->GetPathName(), Row); return; }
				Expected.FindOrAdd(Value.Database).Add(Value.Data);
			}
		};
		for (int32 Row = 0; Row < Table->ResultsStructs.Num(); ++Row)
			if (!Table->IsRowDisabled(Row)) Process(Table->ResultsStructs[Row], Row);
		Process(Table->FallbackResult, ChooserColumn_SpecialIndex_Fallback);
	};
	Visit(Root);
	if (!Error.IsEmpty()) return Error;
	if (Expected.IsEmpty()) return TEXT("CMC Chooser has no mapped Pose Match database");
	for (UObject* Object : Root->NestedObjects)
		if (UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(Object))
			if (FScriptArrayHelper(LocalAssets, LocalAssets->ContainerPtrToValuePtr<void>(Database)).Num() != 0)
				return FString::Printf(TEXT("CMC embedded database retains a local animation array: %s"), *Database->GetPathName());
	for (const auto& Pair : Expected)
	{
		UPoseSearchDatabase* Database = Pair.Key;
		if (FScriptArrayHelper(LocalAssets, LocalAssets->ContainerPtrToValuePtr<void>(Database)).Num() != 0)
			return FString::Printf(TEXT("CMC mapped database retains a local animation array: %s"), *Database->GetPathName());
		if (Database->GetNumAnimationAssets() != Pair.Value.Num())
			return FString::Printf(TEXT("CMC database mapping count mismatch: %s"), *Database->GetPathName());
		for (int32 Index = 0; Index < Pair.Value.Num(); ++Index)
		{
			const auto* Actual = Database->GetDatabaseAnimationAsset(Index);
			if (!Actual || !(*Actual == Pair.Value[Index]))
				return FString::Printf(TEXT("CMC database mapping entry mismatch: %s index %d"), *Database->GetPathName(), Index);
		}
		if (bValidateSearchIndex)
		{
			using namespace UE::PoseSearch;
			const EAsyncBuildIndexResult Status = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest);
			if (Status != EAsyncBuildIndexResult::Success)
				return FString::Printf(TEXT("CMC index %s: %s"), Status == EAsyncBuildIndexResult::InProgress ? TEXT("pending") : TEXT("failed"), *Database->GetPathName());
			for (const FSearchIndexAsset& IndexedAsset : Database->GetSearchIndex().Assets)
				if (!Pair.Value.IsValidIndex(IndexedAsset.GetSourceAssetIdx()) || !Pair.Value[IndexedAsset.GetSourceAssetIdx()].bEnabled)
					return FString::Printf(TEXT("CMC search index contains an unmapped/disabled source: %s index %d"), *Database->GetPathName(), IndexedAsset.GetSourceAssetIdx());
		}
	}
	return {};
}

FString UVBGASPLocomotionAuthoring::BuildWeaponAnimationChooser(UChooserTable* MasterChooser, UChooserTable* Destination, UClass* ContextClass)
{
	using namespace VBGASPCMCChooserAuthoring;
	if (!IsInGameThread() || !MasterChooser || !Destination || MasterChooser == Destination || !ContextClass)
		return TEXT("Missing weapon authoring input or source/destination alias");
	FBuilder Builder{Destination};
	TSet<UChooserTable*> Visited;
	TArray<UPoseSearchDatabase*> Sources;
	if (!Builder.CollectDatabases(MasterChooser, Visited, Sources)) return Builder.Error;
	if (Sources.IsEmpty()) return TEXT("Weapon MasterChooser has no reachable PoseSearch databases");
	Reset(Destination, ContextClass);
	UChooserTable* Search = Builder.NewTable(TEXT("ActiveWeaponPoseMatch"));
	UChooserTable* Startup = Builder.NewTable(TEXT("ActiveWeaponStartup"));
	FVBGASPActiveDatabaseColumn Active;
	FVBGASPCMCMetadataColumn Metadata;
	TMap<UPoseSearchDatabase*, FName> Roles;
	UChooserTable* Candidate = nullptr;
	const FString CandidateError = BuildWeaponCandidateChooser(MasterChooser, Destination, ContextClass, Candidate, Roles);
	if (!CandidateError.IsEmpty()) return CandidateError;
	Active.CandidateChooser = Candidate;
	TArray<FSearchEntry> Entries;
	for (UPoseSearchDatabase* Source : Sources)
	{
		for (int32 Index = 0; Index < Source->GetNumAnimationAssets(); ++Index)
		{
			const auto* Data = Source->GetDatabaseAnimationAsset(Index);
			if (!Data || !Data->bEnabled) continue;
			if (!Cast<UAnimationAsset>(Data->AnimAsset)) return FString::Printf(TEXT("Unsupported weapon database entry in %s"), *Source->GetPathName());
			Search->ResultsStructs.Add(AssetResult(Data->AnimAsset));
			Active.SourceDatabases.Add(Source);
			bool bLoop = false;
			UPoseSearchLibrary::IsAnimationAssetLooping(Cast<UAnimationAsset>(Data->AnimAsset), bLoop);
			Active.LoopingRows.Add(bLoop);
			Entries.Add({Source, *Data});
			FVBGASPCMCChooserOutput Output;
			Output.bPoseMatched = true;
			Output.Tags = Source->Tags;
			if (const FName* Role = Roles.Find(Source); Role && !Role->IsNone() && !bLoop) Output.Tags.AddUnique(*Role);
			Metadata.RowValues.Add(Output);
		}
	}
	if (Entries.IsEmpty()) return TEXT("Weapon databases have no enabled animation entries");
	Search->ColumnsStructs.Add(FInstancedStruct::Make<FVBGASPCMCMetadataColumn>(Metadata));
	Search->ColumnsStructs.Add(FInstancedStruct::Make<FVBGASPActiveDatabaseColumn>(Active));
	Startup->ResultsStructs = Search->ResultsStructs;
	FOutputBoolColumn StartupMirror;
	StartupMirror.InputValue = BoundParameter<FBoolContextProperty>(TEXT("bMirror"));
	for (int32 Row = 0; Row < Metadata.RowValues.Num(); ++Row)
	{
		Metadata.RowValues[Row].bPoseMatched = false;
		Metadata.RowValues[Row].bFallback = true;
		Metadata.RowValues[Row].StartTime = Entries[Row].Data.SamplingRange.Min;
		StartupMirror.RowValues.Add(Entries[Row].Data.MirrorOption == EPoseSearchMirrorOption::MirroredOnly);
	}
	Startup->ColumnsStructs.Add(FInstancedStruct::Make<FVBGASPCMCMetadataColumn>(Metadata));
	Startup->ColumnsStructs.Add(FInstancedStruct::Make<FVBGASPActiveDatabaseColumn>(Active));
	Startup->ColumnsStructs.Add(FInstancedStruct::Make<FOutputBoolColumn>(StartupMirror));
	AddHistoryRoutes(Destination, Search, Startup);
	Builder.AddPoseColumn(Search, Entries);
	if (!Builder.Error.IsEmpty()) return Builder.Error;
	Builder.Finish();
	return Builder.Error;
}

FString UVBGASPLocomotionAuthoring::BuildCMCCompatibilityDatabaseChooser(UChooserTable* Source, UChooserTable* Destination, UClass* ContextClass)
{
	using namespace VBGASPCMCChooserAuthoring;
	if (!IsInGameThread() || !Source || !Destination || Source == Destination || !ContextClass)
		return TEXT("Missing compatibility chooser input or source/destination alias");
	const auto SourceContext = Source->GetContextData();
	if (SourceContext.Num() != 1 || !SourceContext[0].GetPtr<FContextObjectTypeClass>() ||
		Source->ResultType != EObjectChooserResultType::ObjectResult || !Source->OutputObjectType ||
		!Source->OutputObjectType->IsChildOf(UPoseSearchDatabase::StaticClass()))
		return TEXT("Compatibility source must be a single-object-context database chooser");
	Reset(Destination, ContextClass);
	Destination->ContextData = { SourceContext[0] };
	Destination->ContextData[0].GetMutable<FContextObjectTypeClass>().Class = ContextClass;
	Destination->OutputObjectType = Source->OutputObjectType;
	Destination->ResultType = Source->ResultType;
	TMap<UChooserTable*, UChooserTable*> Copies;
	TSet<UChooserTable*> Stack;
	FString Error;
	TFunction<UChooserTable*(UChooserTable*, UChooserTable*)> Clone;
	Clone = [&](UChooserTable* Original, UChooserTable* Existing) -> UChooserTable*
	{
		if (!Original) return nullptr;
		if (Stack.Contains(Original)) { Error = FString::Printf(TEXT("Cyclic compatibility chooser %s"), *Original->GetPathName()); return nullptr; }
		if (UChooserTable** Copy = Copies.Find(Original)) return *Copy;
		const auto Context = Original->GetContextData();
		if (Context.Num() != 1 || !Context[0].GetPtr<FContextObjectTypeClass>())
		{
			Error = FString::Printf(TEXT("Unsupported compatibility context on %s"), *Original->GetPathName());
			return nullptr;
		}
		UChooserTable* Table = Existing ? Existing : NewObject<UChooserTable>(Destination,
			MakeUniqueObjectName(Destination, UChooserTable::StaticClass(), Original->GetFName()), RF_Transactional);
		if (!Existing)
		{
			Table->RootChooser = Destination;
			Destination->AddNestedChooser(Table);
		}
		Table->OutputObjectType = Original->OutputObjectType;
		Table->ResultType = Original->ResultType;
		Table->Version = UChooserTable::CurrentVersion;
		Table->ColumnsStructs = Original->ColumnsStructs;
		Table->DisabledRows = Original->DisabledRows;
		Copies.Add(Original, Table);
		Stack.Add(Original);
		auto Convert = [&](const FInstancedStruct& Result) -> FInstancedStruct
		{
			if (Result.GetPtr<FNestedChooser>() || Result.GetPtr<FEvaluateChooser>())
				return NestedResult(Clone(Child(Result), nullptr));
			if (!Result.IsValid() || Result.GetPtr<FAssetChooser>() || Result.GetPtr<FSoftAssetChooser>())
			{
				if (UObject* Object = Asset(Result); Object && !Object->IsA<UPoseSearchDatabase>())
					Error = FString::Printf(TEXT("Non-database compatibility result %s in %s"), *Object->GetPathName(), *Original->GetPathName());
				return Result;
			}
			Error = FString::Printf(TEXT("Unsupported compatibility result %s in %s"), *GetPathNameSafe(Result.GetScriptStruct()), *Original->GetPathName());
			return {};
		};
		for (const FInstancedStruct& Result : Original->ResultsStructs)
		{
			Table->ResultsStructs.Add(Convert(Result));
			if (!Error.IsEmpty()) break;
		}
		if (Error.IsEmpty()) Table->FallbackResult = Convert(Original->FallbackResult);
		Stack.Remove(Original);
		return Table;
	};
	Clone(Source, Destination);
	if (!Error.IsEmpty()) return Error;
	for (UObject* Object : Destination->NestedObjects)
		if (UChooserTable* Table = Cast<UChooserTable>(Object)) Table->Compile(true);
	Destination->Compile(true);
	Destination->MarkPackageDirty();
	return {};
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPCMCEmbeddedDatabaseMappingTest,
	"Vowbound.Animation.CMC.EmbeddedDatabaseMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPCMCEmbeddedDatabaseMappingTest::RunTest(const FString& Parameters)
{
	using namespace VBGASPCMCChooserAuthoring;
	UPoseSearchDatabase* Source = NewObject<UPoseSearchDatabase>();
	Source->Schema = NewObject<UPoseSearchSchema>();
	UAnimSequence* Disabled = NewObject<UAnimSequence>();
	UAnimSequence* Repeated = NewObject<UAnimSequence>();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FPoseSearchDatabaseAnimationAsset Entry;
		Entry.bEnabled = (Index % 2) != 0;
		Entry.AnimAsset = Entry.bEnabled ? Repeated : Disabled;
		Entry.SamplingRange = FFloatInterval(Index * 0.1f, Index * 0.1f + 0.05f);
		Source->AddAnimationAsset(Entry);
	}

	// Exercise the production duplication/row/mapping path without building an
	// index for synthetic sequences or changing any project assets.
	auto BuildMapping = [&](UPoseSearchDatabase* Input, UChooserTable*& OutRoot) -> UPoseSearchDatabase*
	{
		OutRoot = NewObject<UChooserTable>();
		FBuilder Builder{OutRoot};
		UChooserTable* Search = Builder.NewTable(TEXT("MappingRegression"));
		OutRoot->ResultsStructs.Add(NestedResult(Search));
		TArray<FSearchEntry> Entries;
		for (int32 Index = 0; Index < Input->GetNumAnimationAssets(); ++Index)
		{
			const auto* Entry = Input->GetDatabaseAnimationAsset(Index);
			if (Entry && Entry->bEnabled)
			{
				Entries.Add({Input, *Entry});
				Search->ResultsStructs.Add(AssetResult(Entry->AnimAsset));
			}
		}
		Builder.AddPoseColumn(Search, Entries);
		if (!TestTrue(TEXT("Authoring succeeds"), Builder.Error.IsEmpty())) return nullptr;
		Search->ColumnsStructs.Last().GetMutable<FPoseSearchColumn>().PostLoad();
		TestTrue(TEXT("Full mapping contract passes"), UVBGASPLocomotionAuthoring::ValidateCMCAnimationChooser(OutRoot).IsEmpty());
		return Builder.Databases.FindRef(Input);
	};
	UChooserTable* Root = nullptr;
	UPoseSearchDatabase* Embedded = BuildMapping(Source, Root);
	if (!TestNotNull(TEXT("Embedded database was created"), Embedded)) return false;
	TestEqual(TEXT("Original disabled-first/middle entries remain intact"), Source->GetNumAnimationAssets(), 4);
	TestEqual(TEXT("Only enabled entries occupy mapped indices"), Embedded->GetNumAnimationAssets(), 2);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const auto* Actual = Embedded->GetDatabaseAnimationAsset(Index);
		TestTrue(TEXT("Repeated asset retains its distinct sampling contract"),
			Actual && *Actual == *Source->GetDatabaseAnimationAsset(Index * 2 + 1));
	}
	UChooserTable* SecondRoot = nullptr;
	UPoseSearchDatabase* Reembedded = BuildMapping(Embedded, SecondRoot);
	if (!TestNotNull(TEXT("Chooser-backed source can be embedded again"), Reembedded)) return false;
	TestEqual(TEXT("Chooser-backed source retains both sampling entries"), Reembedded->GetNumAnimationAssets(), 2);

	// The exported validator must reject the exact pre-fix representation.
	const FArrayProperty* LocalAssets = LocalDatabaseAssetsProperty();
	FScriptArrayHelper Corrupted(LocalAssets, LocalAssets->ContainerPtrToValuePtr<void>(Embedded));
	Corrupted.AddValue();
	TestFalse(TEXT("Retained local array is rejected before save"),
		UVBGASPLocomotionAuthoring::ValidateCMCAnimationChooser(Root).IsEmpty());
	Corrupted.EmptyValues();
	return true;
}
#endif
