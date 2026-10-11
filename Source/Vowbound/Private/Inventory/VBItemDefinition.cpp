// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBItemDefinition.h"

#include "Engine/AssetManager.h"
#include "Narrative/Story/VBRuleTypes.h"

#if WITH_EDITOR
#include "Animation/AnimMontage.h"
#include "Animation/VBAN_ItemUseMarker.h"
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBItemDefinition"

FPrimaryAssetType UVBItemDefinition::GetItemAssetType()
{
	// 함수 안 정적 변수 - FName 은 모듈 정적 초기화 때 아직 쓸 수 없을 수 있다.
	static const FPrimaryAssetType Type(TEXT("VBItem"));
	return Type;
}

FPrimaryAssetId UVBItemDefinition::MakeItemAssetId(FGameplayTag InItemId)
{
	return FPrimaryAssetId(GetItemAssetType(), InItemId.GetTagName());
}

const UVBItemDefinition* UVBItemDefinition::FindById(const FGameplayTag& InItemId)
{
	const FSoftObjectPath Path = UAssetManager::Get().GetPrimaryAssetPath(MakeItemAssetId(InItemId));
	return Cast<UVBItemDefinition>(Path.TryLoad());
}

int32 UVBItemDefinition::ClampToMax(int32 Current, int32 Requested) const
{
	if (Requested <= 0)
	{
		return 0;
	}
	return MaxCount <= 0 ? Requested : FMath::Clamp(MaxCount - Current, 0, Requested);
}

FPrimaryAssetId UVBItemDefinition::GetPrimaryAssetId() const
{
	// ItemId 가 비었으면 자산 이름으로 - 에셋 매니저 목록에는 들어가되 저장에서 되찾을 수는 없다(IsDataValid 가 잡는다).
	return FPrimaryAssetId(GetItemAssetType(), ItemId.IsValid() ? ItemId.GetTagName() : GetFName());
}

#if WITH_EDITOR
EDataValidationResult UVBItemDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!ItemId.IsValid())
	{
		Context.AddError(LOCTEXT("NoItemId", "ItemId 가 비었다 - 저장·조건·퀘스트가 이 아이템을 부를 수 없다."));
		Result = EDataValidationResult::Invalid;
	}
	if (!VBRules::ValidateStructArray(Fragments, FVBItemFragment::StaticStruct(), LOCTEXT("FragmentsWhere", "조각"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	// 파이썬·복사로 만든 자산은 ClampMin 을 거치지 않는다. 0 원짜리는 상점에서 공짜가 된다.
	if (const FVBItemFragment_Price* Price = FindFragment<FVBItemFragment_Price>(); Price && Price->BaseValue < 1)
	{
		Context.AddError(LOCTEXT("BadPrice", "가격 조각의 BaseValue 가 1 보다 작다."));
		Result = EDataValidationResult::Invalid;
	}
	// 장비 조각에 칸 종류가 없으면 어느 칸에도 못 들어간다 - 입을 수 없는 장비가 조용히 생긴다.
	if (const FVBItemFragment_Equippable* Equippable = FindFragment<FVBItemFragment_Equippable>(); Equippable && !Equippable->SlotType.IsValid())
	{
		Context.AddError(LOCTEXT("NoSlotType", "장비 조각의 SlotType 이 비었다 - 어느 칸에도 못 입는다."));
		Result = EDataValidationResult::Invalid;
	}
	// 사용 몽타주가 사용 GA 의 약속을 지키나(.sw/quickslot-input 6.1절) - 표식 하나·몽타주 위·블렌드아웃 앞, 끝나는 몽타주.
	//   어기면 쓰는 시늉만 하고 확정이 영영 안 오거나 GA 가 안 끝난다. 막을 수 있으니 막는다.
	if (const FVBItemFragment_Usable* Usable = FindFragment<FVBItemFragment_Usable>(); Usable && Usable->UseMontage)
	{
		for (const FText& Problem : UVBAN_ItemUseMarker::FindUseMontageProblems(*Usable->UseMontage))
		{
			Context.AddError(FText::Format(LOCTEXT("UseMontageProblem", "사용 몽타주 {0}: {1}"), FText::FromString(GetNameSafe(Usable->UseMontage)), Problem));
			Result = EDataValidationResult::Invalid;
		}
	}
	// 부모(UObject)는 NotValidated 를 돌려준다. 여기까지 오류가 없으면 «봤고 멀쩡하다» 로 올린다.
	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif

#undef LOCTEXT_NAMESPACE
