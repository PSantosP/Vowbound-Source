// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBSwingCombatProfile.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"   // FDataValidationContext::AddError
#define LOCTEXT_NAMESPACE "VBSwingCombatProfile"
#endif

bool FVBSwingCombatScales::HasAnyEffect() const
{
	// 정확 비교가 아니라 근사 비교를 쓰는 이유: 에디터에서 1.0 을 넣었다 지웠다 하면 1.0000001 같은 값이
	//  남을 수 있고, 그런 값은 저작 의도상 "안 건드림"이다. 실제로 곱해도 결과가 사실상 같다.
	return !FMath::IsNearlyEqual(DamageScale, 1.0f)
		|| !FMath::IsNearlyEqual(KnockbackStrengthScale, 1.0f)
		|| !FMath::IsNearlyEqual(KnockbackZScale, 1.0f)
		|| !FMath::IsNearlyEqual(HitStopDurationScale, 1.0f);
}

#if WITH_EDITOR
EDataValidationResult UVBSwingCombatProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 전 배율이 항등원이면 이 자산은 노티에 꽂혀도 아무 일도 하지 않는다.
	//  런타임 증상이 "배선 실패"와 똑같아서 추적이 오래 걸리는 부류다.
	if (!Scales.HasAnyEffect())
	{
		Context.AddError(LOCTEXT("VBSwingCombat_NoEffect",
			"모든 배율이 1.0 이라 이 프로필은 아무것도 바꾸지 않습니다. "
			"노티에 꽂아도 무보정이라 배선이 잘못된 것과 증상이 같습니다. "
			"바꾸려는 값의 배율을 넣거나, 쓰지 않을 자산이면 노티에서 빼세요."));

		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
