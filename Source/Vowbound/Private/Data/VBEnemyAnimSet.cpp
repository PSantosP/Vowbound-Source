// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBEnemyAnimSet.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "VBEnemyAnimSet"

EDataValidationResult UVBEnemyAnimSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 둘 중 하나라도 비면 그 적은 참조 포즈(T 포즈)로 서 있다. 눈에는 보이지만 로그에는 초기화 때 한 줄뿐이라
	// 저작 시점에 막는다.
	if (!Idle)
	{
		Context.AddError(LOCTEXT("VBEnemyAnimSet_NoIdle", "Idle 클립이 비어 있습니다. 이 묶음을 쓰는 적은 서 있을 때 T 포즈가 됩니다."));
		Result = EDataValidationResult::Invalid;
	}
	if (!Locomotion)
	{
		Context.AddError(LOCTEXT("VBEnemyAnimSet_NoLocomotion", "Locomotion 블렌드스페이스가 비어 있습니다. 이 묶음을 쓰는 적은 이동할 때 T 포즈가 됩니다."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
