// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBSwingImpactProfile.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"   // FDataValidationContext::AddError
#define LOCTEXT_NAMESPACE "VBSwingImpactProfile"
#endif

FVector UVBSwingImpactProfile::GetNormalizedAxis() const
{
	// GetSafeNormal 은 길이가 임계 이하면 영벡터를 돌려준다. 그 영벡터가 곧 "축 미저작" 신호이고
	//  호출부(GA)가 그것을 보고 큐를 쏘지 않는다.
	// 여기서 임의 방향(예: 정면)으로 대체하지 않는 이유: 그 순간 '방향'이라는 값이 데이터가 아니라
	//  코드에 생긴다. 방향의 데이터 홈은 LocalSwingAxis 하나뿐이어야 한다.
	return LocalSwingAxis.GetSafeNormal();
}

#if WITH_EDITOR
EDataValidationResult UVBSwingImpactProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 영벡터 축은 "정규화 결과가 영벡터 -> 큐 미발동"으로 이어져 쉐이크가 통째로 사라진다.
	//  런타임엔 무연출로만 보이므로 저작 시점에 이름을 붙여 준다.
	if (LocalSwingAxis.IsNearlyZero())
	{
		Context.AddError(LOCTEXT("VBSwingImpact_ZeroAxis",
			"LocalSwingAxis 가 영벡터입니다. 정규화 결과가 영벡터가 되어 임팩트 쉐이크가 발동하지 않습니다. "
			"밀고 싶은 방향을 아바타 로컬 기준으로 넣으세요(정면=(1,0,0), 오른쪽=(0,1,0), 아래=(0,0,-1))."));

		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
