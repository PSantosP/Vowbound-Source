// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Progression/VBGrowthConfig.h"

#include "GameplayEffect.h"
#include "Progression/VBSkillTreeDefinition.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBGrowthConfig"

#if WITH_EDITOR
EDataValidationResult UVBGrowthConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto Fail = [&](const FText& Text)
	{
		Context.AddError(Text);
		Result = EDataValidationResult::Invalid;
	};
	if (XPToNextLevel.IsEmpty())
	{
		Fail(LOCTEXT("NoTable", "XPToNextLevel 이 비었다 - 레벨이 1 에서 오르지 않는다."));
	}
	for (int32 Index = 0; Index < XPToNextLevel.Num(); ++Index)
	{
		if (XPToNextLevel[Index] < 1)
		{
			Fail(FText::Format(LOCTEXT("BadStep", "XPToNextLevel[{0}] 가 1 미만이다 - 경험치 없이 레벨이 오른다."), Index));
		}
	}
	if (PointsPerLevel < 1)
	{
		Fail(LOCTEXT("Points", "PointsPerLevel 은 1 이상이어야 한다."));
	}
	if (!SkillTree)
	{
		Fail(LOCTEXT("NoTree", "SkillTree 가 비었다 - 포인트를 쓸 곳이 없다."));
	}
	if (!StatBonusEffect)
	{
		Fail(LOCTEXT("NoEffect", "StatBonusEffect 가 비었다 - 능력치 칸을 열어도 아무것도 안 바뀐다."));
	}
	else if (StatBonusEffect.GetDefaultObject()->DurationPolicy != EGameplayEffectDurationType::Infinite)
	{
		Fail(LOCTEXT("NotInfinite", "StatBonusEffect 는 무한(Infinite)이어야 한다 - 순간 효과는 활성 핸들이 없어 떼지 못한다."));
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
