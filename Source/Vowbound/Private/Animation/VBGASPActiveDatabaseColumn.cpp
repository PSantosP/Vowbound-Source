#include "Animation/VBGASPActiveDatabaseColumn.h"

#include "Animation/VBGASPCMCSelectionTypes.h"
#include "ChooserIndexArray.h"
#include "Chooser.h"
#include "ChooserPropertyAccess.h"
#include "PoseSearch/PoseSearchDatabase.h"

namespace
{
	FVBGASPCMCChooserOutput* FindSelection(FChooserEvaluationContext& Context)
	{
		for (FStructView& Param : Context.Params)
			if (auto* Selection = Param.GetPtr<FVBGASPCMCChooserOutput>()) return Selection;
		return nullptr;
	}
}

void FVBGASPActiveDatabaseColumn::Filter(FChooserEvaluationContext& Context,
	const FChooserIndexArray& In, FChooserIndexArray& Out) const
{
	FVBGASPCMCChooserOutput* Selection = FindSelection(Context);
	// Fail closed: missing context must not admit another weapon's animation.
	if (!Selection) return;
	if (CandidateChooser)
	{
		FVBGASPCMCWeaponQuery* Query = nullptr;
		for (FStructView& Param : Context.Params)
			if (auto* Value = Param.GetPtr<FVBGASPCMCWeaponQuery>()) { Query = Value; break; }
		const auto Signature = CandidateChooser->GetContextData();
		const auto* Type = Signature.Num() == 1 ? Signature[0].GetPtr<FContextObjectTypeStruct>() : nullptr;
		if (!Query || !Type || Type->Struct != FVBGASPCMCWeaponQuery::StaticStruct())
		{
			Selection->bPhaseDataValid = false;
			return;
		}
		Selection->AllowedDatabases.Reset();
		FChooserEvaluationContext CandidateContext;
		CandidateContext.AddStructParam(*Query);
		UChooserTable::EvaluateChooser(CandidateContext, CandidateChooser,
			FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([Selection](UObject* Result)
			{
				if (UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(Result))
				{
					Selection->AllowedDatabases.AddUnique(Database);
					return FObjectChooserBase::EIteratorStatus::Continue;
				}
				Selection->bPhaseDataValid = false;
				return FObjectChooserBase::EIteratorStatus::Failed;
			}));
		Selection->bCandidateChooserEvaluated = true;
	}
	else if (Selection->bRequireCandidateChooser)
	{
		Selection->bPhaseDataValid = false;
		return;
	}
	if (SourceDatabases.Num() != LoopingRows.Num())
	{
		Selection->bPhaseDataValid = false;
		return;
	}
	auto IsAllowed = [&](int32 Row)
	{
		return SourceDatabases.IsValidIndex(Row) && SourceDatabases[Row]
			&& Selection->AllowedDatabases.Contains(SourceDatabases[Row]);
	};
	// Prefer starts/stops only inside the active weapon/gait pools and the incoming
	// filter result. A transition with no such clip can enter its repeating pose.
	bool bHasTransition = false;
	if (Selection->Phase == EVBGASPCMCPhase::Transition)
		for (const FChooserIndexArray::FIndexData& Row : In)
			bHasTransition |= IsAllowed(Row.Index) && !LoopingRows[Row.Index];
	for (const FChooserIndexArray::FIndexData& Row : In)
		if (IsAllowed(Row.Index) &&
			(Selection->Phase == EVBGASPCMCPhase::Any ||
			(Selection->Phase == EVBGASPCMCPhase::Loop ? LoopingRows[Row.Index] : LoopingRows[Row.Index] != bHasTransition)))
		{
			Selection->bHasPhaseCandidates = true;
			Out.Push(Row);
		}
}

void FVBGASPActiveDatabaseColumn::SetOutputs(FChooserEvaluationContext& Context, int RowIndex) const
{
	if (FVBGASPCMCChooserOutput* Selection = FindSelection(Context))
		Selection->SourceDatabase = SourceDatabases.IsValidIndex(RowIndex) ? SourceDatabases[RowIndex] : nullptr;
}

#if WITH_EDITOR
void FVBGASPActiveDatabaseColumn::InsertRows(int32 Index, int32 Count)
{
	if (Index < 0 || Index > SourceDatabases.Num() || Count <= 0) return;
	LoopingRows.SetNum(SourceDatabases.Num());
	for (int32 I = 0; I < Count; ++I)
	{
		SourceDatabases.Insert(nullptr, Index);
		LoopingRows.Insert(false, Index);
	}
}
void FVBGASPActiveDatabaseColumn::DeleteRows(TArrayView<int> RowIndices)
{
	TArray<int> Sorted;
	for (int Index : RowIndices) Sorted.AddUnique(Index);
	Sorted.Sort(TGreater<int>());
	LoopingRows.SetNum(SourceDatabases.Num());
	for (int Index : Sorted) if (SourceDatabases.IsValidIndex(Index))
	{
		SourceDatabases.RemoveAt(Index);
		LoopingRows.RemoveAt(Index);
	}
}
void FVBGASPActiveDatabaseColumn::MoveRow(int32 SourceIndex, int32 TargetIndex)
{
	if (!SourceDatabases.IsValidIndex(SourceIndex) || TargetIndex < 0 || TargetIndex > SourceDatabases.Num()) return;
	const TObjectPtr<UPoseSearchDatabase> Value = SourceDatabases[SourceIndex];
	LoopingRows.SetNum(SourceDatabases.Num());
	const bool bLoop = LoopingRows[SourceIndex];
	SourceDatabases.RemoveAt(SourceIndex);
	LoopingRows.RemoveAt(SourceIndex);
	SourceDatabases.Insert(Value, SourceIndex < TargetIndex ? TargetIndex - 1 : TargetIndex);
	LoopingRows.Insert(bLoop, SourceIndex < TargetIndex ? TargetIndex - 1 : TargetIndex);
}
void FVBGASPActiveDatabaseColumn::CopyRow(FChooserColumnBase& SourceColumn, int32 SourceIndex, int32 TargetIndex)
{
	const auto& Source = static_cast<const FVBGASPActiveDatabaseColumn&>(SourceColumn);
	if (Source.SourceDatabases.IsValidIndex(SourceIndex) && Source.LoopingRows.IsValidIndex(SourceIndex) && SourceDatabases.IsValidIndex(TargetIndex))
	{
		LoopingRows.SetNum(SourceDatabases.Num());
		SourceDatabases[TargetIndex] = Source.SourceDatabases[SourceIndex];
		LoopingRows[TargetIndex] = Source.LoopingRows[SourceIndex];
	}
}
void FVBGASPActiveDatabaseColumn::ReplaceReferences(UObject* ReferenceToReplace, UObject* ReplaceWith)
{
	if (CandidateChooser == ReferenceToReplace && (!ReplaceWith || ReplaceWith->IsA<UChooserTable>()))
		CandidateChooser = Cast<UChooserTable>(ReplaceWith);
	if (ReplaceWith && !ReplaceWith->IsA<UPoseSearchDatabase>()) return;
	for (TObjectPtr<UPoseSearchDatabase>& Database : SourceDatabases)
		if (Database == ReferenceToReplace) Database = Cast<UPoseSearchDatabase>(ReplaceWith);
}
#endif

void FVBGASPCMCMetadataColumn::SetOutputs(FChooserEvaluationContext& Context, int RowIndex) const
{
	FVBGASPCMCChooserOutput* Output = FindSelection(Context);
	if (!Output) return;
	const FVBGASPCMCChooserOutput& Data = RowValues.IsValidIndex(RowIndex) ? RowValues[RowIndex] : FallbackValue;
	Output->StartTime = Data.StartTime;
	Output->MaxAcceptedCost = Data.MaxAcceptedCost;
	Output->BlendTime = Data.BlendTime;
	Output->BlendProfileName = Data.BlendProfileName;
	Output->BlendCurve = Data.BlendCurve;
	Output->Tags = Data.Tags;
	Output->bPoseMatched = Data.bPoseMatched;
	Output->bFallback = Data.bFallback;
}

#if WITH_EDITOR
void FVBGASPCMCMetadataColumn::InsertRows(int32 Index, int32 Count)
{
	if (Index < 0 || Index > RowValues.Num() || Count <= 0) return;
	for (int32 I = 0; I < Count; ++I) RowValues.Insert(FVBGASPCMCChooserOutput(), Index);
}
void FVBGASPCMCMetadataColumn::DeleteRows(TArrayView<int> RowIndices)
{
	TArray<int> Sorted;
	for (int Index : RowIndices) Sorted.AddUnique(Index);
	Sorted.Sort(TGreater<int>());
	for (int Index : Sorted) if (RowValues.IsValidIndex(Index)) RowValues.RemoveAt(Index);
}
void FVBGASPCMCMetadataColumn::MoveRow(int32 SourceIndex, int32 TargetIndex)
{
	if (!RowValues.IsValidIndex(SourceIndex) || TargetIndex < 0 || TargetIndex > RowValues.Num()) return;
	const FVBGASPCMCChooserOutput Value = RowValues[SourceIndex];
	RowValues.RemoveAt(SourceIndex);
	RowValues.Insert(Value, SourceIndex < TargetIndex ? TargetIndex - 1 : TargetIndex);
}
void FVBGASPCMCMetadataColumn::CopyRow(FChooserColumnBase& SourceColumn, int32 SourceIndex, int32 TargetIndex)
{
	const auto& Source = static_cast<const FVBGASPCMCMetadataColumn&>(SourceColumn);
	if (Source.RowValues.IsValidIndex(SourceIndex) && RowValues.IsValidIndex(TargetIndex))
		RowValues[TargetIndex] = Source.RowValues[SourceIndex];
}
void FVBGASPCMCMetadataColumn::CopyFallback(FChooserColumnBase& SourceColumn)
{
	FallbackValue = static_cast<const FVBGASPCMCMetadataColumn&>(SourceColumn).FallbackValue;
}
#endif
