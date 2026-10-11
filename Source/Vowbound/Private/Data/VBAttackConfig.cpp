// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Data/VBAttackConfig.h"

#include "AbilitySystem/Abilities/VBWeaponStyleBehavior.h"
#include "AbilitySystem/VBGameplayTags.h"

UVBAttackConfig::UVBAttackConfig()
{
	// 답의 기본은 «막을 수 있다». 네이티브 태그는 헤더 멤버 초기화로 못 넣어 생성자에서 준다(GA 의 InputTag 와 같은 관례).
	// 비워 두면 예고 큐가 답 행을 못 찾아 경고를 찍는다 - 기본값이 그 경고를 없는 자산에서까지 막는다.
	AttackAnswer = VBGameplayTags::Attack_Answer_Parryable;
}

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

float UVBAttackConfig::GetVariantApproachOffset(int32 VariantIndex, float BodyScale) const
{
	// 0번(AttackMontage)은 CollectVariants 가 기본 구조체로 만들므로 override 가 -1 이라 자산 값으로 떨어진다.
	const FVBAttackMontageVariant Variant = ResolveMontageVariant(VariantIndex);
	const float Base = Variant.WarpApproachOffsetOverride >= 0.0f ? Variant.WarpApproachOffsetOverride : WarpApproachOffset;
	return FMath::Clamp(Base * BodyScale, 0.0f, FMath::Max(0.0f, GetScaledAttackRange(BodyScale)));
}
