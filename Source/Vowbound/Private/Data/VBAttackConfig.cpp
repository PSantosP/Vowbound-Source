// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Data/VBAttackConfig.h"

#include "AbilitySystem/Abilities/VBWeaponStyleBehavior.h"

namespace
{
	// AttackMontage(0번) + null 아닌 변형들을 평탄한 목록으로 만든다.
	// null 원소를 빼는 이유: 에디터에서 배열 슬롯만 추가하고 비워 두면
	//   그 순간 공격이 무음으로 사라진다. 슬롯 실수를 동작 결함으로 만들지 않는다.
	void CollectVariants(const UVBAttackConfig& Config, TArray<FVBAttackMontageVariant>& Out)
	{
		if (Config.AttackMontage)
		{
			FVBAttackMontageVariant Base;
			Base.Montage = Config.AttackMontage;
			Out.Add(Base);
		}
		for (const FVBAttackMontageVariant& V : Config.MontageVariants)
		{
			if (V.Montage)
			{
				Out.Add(V);
			}
		}
	}
}

int32 UVBAttackConfig::GetMontageVariantCount() const
{
	TArray<FVBAttackMontageVariant> All;
	CollectVariants(*this, All);
	return All.Num();
}

FVBAttackMontageVariant UVBAttackConfig::ResolveMontageVariant(int32 VariantIndex) const
{
	TArray<FVBAttackMontageVariant> All;
	CollectVariants(*this, All);
	if (All.Num() == 0)
	{
		return FVBAttackMontageVariant();   // Montage=null -> 호출부가 즉시-Trace 폴백
	}

	// 음수 커서도 안전하게 감싼다. 호출부가 범위를 지킬 의무를 없앤다.
	const int32 Index = ((VariantIndex % All.Num()) + All.Num()) % All.Num();
	return All[Index];
}
