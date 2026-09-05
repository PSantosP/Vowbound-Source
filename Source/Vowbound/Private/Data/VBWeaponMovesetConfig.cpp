// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBWeaponMovesetConfig.h"

#include "Data/VBAttackConfig.h"

UVBAttackConfig* UVBWeaponMovesetConfig::GetAttackConfig(EVBWeaponType WeaponType) const
{
	// 등록된 무기 타입의 leaf 반환. 미등록이면 nullptr → 호출부가 레거시 경로로 폴백.
	// (강공격 분기는 DEC-009 폐지로 사라짐 — 무기당 무브셋 1개.)
	if (const FVBWeaponMoveset* Moveset = Movesets.Find(WeaponType))
	{
		return Moveset->Light;
	}
	return nullptr;
}
