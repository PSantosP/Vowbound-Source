#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBChooserAuthoring.generated.h"

class UChooserTable;

/** Narrow editor operation for auditing and tuning an existing Chooser float-range row. */
UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBChooserAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns false without changing the asset when the column, binding, or row is missing. Save explicitly after review. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool SetFloatRangeRow(UChooserTable* Table, FName BindingName, int32 RowIndex,
		float Min, float Max, bool bNoMin, bool bNoMax);
};
