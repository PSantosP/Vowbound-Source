// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Data/VBDodgeConfig.h"

#include "Animation/AnimMontage.h"

namespace
{
	// 세트 안에서 방향 슬롯 하나를 꺼낸다. 미할당이면 nullptr → 상위 폴백으로 내려간다.
	UAnimMontage* PickDirection(const FVBDodgeMontageSet& Set, EVBDodgeDirection Direction)
	{
		switch (Direction)
		{
		case EVBDodgeDirection::Forward:  return Set.Forward;
		case EVBDodgeDirection::Backward: return Set.Backward;
		case EVBDodgeDirection::Left:     return Set.Left;
		case EVBDodgeDirection::Right:    return Set.Right;
		default:                          return nullptr;
		}
	}
}

UAnimMontage* UVBDodgeConfig::ResolveDodgeMontage(EVBWeaponType WeaponType, EVBDodgeDirection Direction) const
{
	// 폴백 3단 (설계 §2.2) — 무기 전용 → 통일 닷지 → nullptr(호출부가 LaunchCharacter 안전망).
	// 폴백은 슬롯 단위다: 무기 세트가 등록돼 있어도 그 '방향'만 비어 있으면 통일 세트로 내려간다.
	//   (예: 카타나 세트에 Left 만 미저작 → 통일 닷지의 Left 로 메꿔 방향 구멍이 안 생긴다.)
	if (const FVBDodgeMontageSet* WeaponSet = WeaponDodgeSets.Find(WeaponType))
	{
		if (UAnimMontage* M = PickDirection(*WeaponSet, Direction))
		{
			return M;
		}
	}

	// 통일 세트 — 비무장(None)·Fighter·Magic 등 미등록 전부가 여기로 온다.
	return PickDirection(DefaultDodgeSet, Direction);
}
