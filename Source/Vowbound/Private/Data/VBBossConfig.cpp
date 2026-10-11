// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Data/VBBossConfig.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#define LOCTEXT_NAMESPACE "VBBossConfig"

EDataValidationResult UVBBossConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 첫 페이즈의 문턱은 1.0 이라 보고 시작한다. 그래서 [0] 은 1.0 미만이어야 하고, 뒤로 갈수록 작아져야 한다.
	float PreviousThreshold = 1.0f;
	for (int32 Index = 0; Index < AdditionalPhases.Num(); ++Index)
	{
		const FVBBossPhase& Phase = AdditionalPhases[Index];

		if (Phase.HealthRatioThreshold <= 0.0f || Phase.HealthRatioThreshold >= PreviousThreshold)
		{
			Context.AddError(FText::Format(LOCTEXT("VBBoss_ThresholdOrder",
				"AdditionalPhases[{0}].HealthRatioThreshold({1}) 는 0 보다 크고 앞 페이즈의 문턱({2}) 보다 작아야 합니다. "
				"아니면 그 페이즈는 절대 들어가지 않거나 앞 페이즈와 같은 순간에 겹쳐 들어갑니다."),
				Index, Phase.HealthRatioThreshold, PreviousThreshold));
			Result = EDataValidationResult::Invalid;
		}
		PreviousThreshold = Phase.HealthRatioThreshold;

		if (!Phase.AttackInputTag.IsValid())
		{
			Context.AddError(FText::Format(LOCTEXT("VBBoss_NoPhaseAttackTag",
				"AdditionalPhases[{0}].AttackInputTag 가 비어 있습니다. 그 페이즈에 들어가면 보스가 영영 공격하지 않습니다 "
				"(UVBEnemyCombatComponent::IsReadyToAttack 이 조용히 false 를 돌려줍니다)."), Index));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
