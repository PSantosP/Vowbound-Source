#pragma once

#include "CoreMinimal.h"
#include "IChooserColumn.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "VBGASPActiveDatabaseColumn.generated.h"

class UPoseSearchDatabase;
class UChooserTable;

/** Keep the existing weapon Chooser's complete database set before one Pose Match query. */
USTRUCT(DisplayName="Vowbound Active Pose Databases", meta=(Category="Filter"))
struct VOWBOUND_API FVBGASPActiveDatabaseColumn : public FChooserColumnBase
{

	GENERATED_BODY()
public:
	/** Independent PSD-result chooser whose only context is FVBGASPCMCWeaponQuery. */
	UPROPERTY(EditAnywhere, Category="Data")
	TObjectPtr<UChooserTable> CandidateChooser;
	UPROPERTY(EditAnywhere, Category="Data")
	TArray<TObjectPtr<UPoseSearchDatabase>> SourceDatabases;
	/** Authored from IsAnimationAssetLooping, one value per source/result row. */
	UPROPERTY(EditAnywhere, Category="Data")
	TArray<bool> LoopingRows;

	virtual void Filter(FChooserEvaluationContext& Context, const FChooserIndexArray& In,
		FChooserIndexArray& Out) const override;
	virtual bool HasOutputs() const override { return true; }
	virtual void SetOutputs(FChooserEvaluationContext& Context, int RowIndex) const override;

#if WITH_EDITOR
	virtual bool HasPrimaryInput() override { return false; }
	virtual FName RowValuesPropertyName() override { return GET_MEMBER_NAME_CHECKED(FVBGASPActiveDatabaseColumn, SourceDatabases); }
	virtual void SetNumRows(int32 NumRows) override { SourceDatabases.SetNum(NumRows); LoopingRows.SetNum(NumRows); }
	virtual void InsertRows(int32 Index, int32 Count) override;
	virtual void DeleteRows(TArrayView<int> RowIndices) override;
	virtual void MoveRow(int32 SourceIndex, int32 TargetIndex) override;
	virtual void CopyRow(FChooserColumnBase& SourceColumn, int32 SourceIndex, int32 TargetIndex) override;
	virtual void ReplaceReferences(UObject* ReferenceToReplace, UObject* ReplaceWith) override;
#endif
};

/** Playback metadata only; never overwrite the caller's candidate set or the pose-search result. */
USTRUCT(DisplayName="Vowbound CMC Playback", meta=(Category="Output"))
struct VOWBOUND_API FVBGASPCMCMetadataColumn : public FChooserColumnBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Data")
	TArray<FVBGASPCMCChooserOutput> RowValues;
	UPROPERTY(EditAnywhere, Category="Data")
	FVBGASPCMCChooserOutput FallbackValue;
	virtual bool HasFilters() const override { return false; }
	virtual bool HasOutputs() const override { return true; }
	virtual void SetOutputs(FChooserEvaluationContext& Context, int RowIndex) const override;
#if WITH_EDITOR
	virtual bool HasPrimaryInput() override { return false; }
	virtual FName RowValuesPropertyName() override { return GET_MEMBER_NAME_CHECKED(FVBGASPCMCMetadataColumn, RowValues); }
	virtual void SetNumRows(int32 Count) override { RowValues.SetNum(Count); }
	virtual void InsertRows(int32 Index, int32 Count) override;
	virtual void DeleteRows(TArrayView<int> RowIndices) override;
	virtual void MoveRow(int32 SourceIndex, int32 TargetIndex) override;
	virtual void CopyRow(FChooserColumnBase& SourceColumn, int32 SourceIndex, int32 TargetIndex) override;
	virtual void CopyFallback(FChooserColumnBase& SourceColumn) override;
#endif
};
