// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Loot/VBLootTable.h"

#include "Inventory/VBItemDefinition.h"
#include "Narrative/Story/VBRuleTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBLootTable"

#if WITH_EDITOR
EDataValidationResult UVBLootTable::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FVBLootEntry& Entry = Entries[Index];
		if (!Entry.Item)
		{
			Context.AddError(FText::Format(LOCTEXT("NoItem", "{0} 번 줄: 아이템이 비었다."), Index));
			Result = EDataValidationResult::Invalid;
		}
		else if (!Entry.Item->ItemId.IsValid())
		{
			// 인벤토리 저장은 ItemId 로 아이템을 되찾는다 - 없으면 받은 것이 이어하기 뒤에 사라진다.
			Context.AddError(FText::Format(LOCTEXT("NoItemId", "{0} 번 줄: {1} 에 ItemId 가 없다 - 받아도 저장되지 않는다."),
			                               Index, FText::FromString(Entry.Item->GetName())));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.MinCount < 1 || Entry.MaxCount < Entry.MinCount)
		{
			Context.AddError(FText::Format(LOCTEXT("BadCount", "{0} 번 줄: 개수 범위 {1}~{2} - 최소는 1 이상, 최대는 최소 이상이어야 한다."),
			                               Index, Entry.MinCount, Entry.MaxCount));
			Result = EDataValidationResult::Invalid;
		}
		// 0 은 «절대 안 나오는 줄» 이다. 끄려고 둔 것이라도 표를 읽는 사람에게 거짓말을 하므로 지우게 한다.
		if (!(Entry.Chance > 0.0f && Entry.Chance <= 1.0f))
		{
			Context.AddError(FText::Format(LOCTEXT("BadChance", "{0} 번 줄: 확률 {1} - 0 초과 1 이하여야 한다."), Index, Entry.Chance));
			Result = EDataValidationResult::Invalid;
		}
		const FText ConditionsWhere = FText::Format(LOCTEXT("ConditionsWhere", "{0} 번 줄의 조건"), Index);
		if (!VBRules::ValidateStructArray(Entry.Conditions, FVBCondition::StaticStruct(), ConditionsWhere, Context))
		{
			Result = EDataValidationResult::Invalid;
		}
	}
	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif

#undef LOCTEXT_NAMESPACE
