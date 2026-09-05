// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBHitReactConfig.h"

#include "Animation/AnimMontage.h"

UAnimMontage* UVBHitReactConfig::ResolveMontage(EVBWeaponType Stance) const
{
	// 등록된 스탠스의 몽타주 우선. 스탠스가 등록됐어도 몽타주가 비어 있으면 DefaultSet 으로 폴백한다
	// (부분 할당된 엔트리가 무동작을 유발하지 않도록 — UVBWeaponMovesetConfig::GetAttackConfig 폴백 관례 확장).
	if (const FVBHitReactSet* Set = StanceOverrides.Find(Stance))
	{
		if (Set->Montage)
		{
			return Set->Montage;
		}
	}
	return DefaultSet.Montage;
}

UAnimMontage* UVBHitReactConfig::ResolveStaggerMontage() const
{
	// StaggerSet 은 단일 세트다 - 스탠스로 갈리지 않으므로 인자를 받지 않는다(헤더 주석 참조).
	// 히트리액트로 폴백하지 않는다 - 폴백하면 미저작이 "가벼운 플린치"로 보여 눈에 안 띈다.
	return StaggerSet.Montage;
}
