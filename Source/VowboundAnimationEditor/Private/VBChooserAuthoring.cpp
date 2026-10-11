#include "VBChooserAuthoring.h"

#include "Chooser.h"
#include "FloatRangeColumn.h"

bool UVBChooserAuthoring::SetFloatRangeRow(UChooserTable* Table, FName BindingName, int32 RowIndex,
	float Min, float Max, bool bNoMin, bool bNoMax)
{
	if (!Table || BindingName.IsNone() || RowIndex < 0 || RowIndex >= Table->ResultsStructs.Num() ||
		(!bNoMin && !bNoMax && Min > Max))
	{
		return false;
	}

	for (FInstancedStruct& Column : Table->ColumnsStructs)
	{
		FFloatRangeColumn* FloatColumn = Column.GetMutablePtr<FFloatRangeColumn>();
		if (!FloatColumn || !FloatColumn->InputValue.IsValid() ||
			FloatColumn->InputValue.GetScriptStruct() != FFloatContextProperty::StaticStruct())
		{
			continue;
		}

		const FFloatContextProperty& Input = FloatColumn->InputValue.Get<FFloatContextProperty>();
		if (Input.Binding.PropertyBindingChain.Num() != 1 ||
			Input.Binding.PropertyBindingChain[0] != BindingName ||
			!FloatColumn->RowValues.IsValidIndex(RowIndex))
		{
			continue;
		}

		Table->Modify();
		FChooserFloatRangeRowData& Row = FloatColumn->RowValues[RowIndex];
		Row.Min = Min;
		Row.Max = Max;
		Row.bNoMin = bNoMin;
		Row.bNoMax = bNoMax;
		Table->MarkPackageDirty();
		return true;
	}
	return false;
}
