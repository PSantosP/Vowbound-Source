// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBMovementConfig.h"

UVBMovementConfig::UVBMovementConfig()
{
	// FVBMovementProfile 인라인 디폴트 = Armed 값이므로 ArmedDefaultProfile 은 무설정(그대로).
	// Unarmed / BigSword 만 여기서 명시 초기화 — 값 = 리팩터 전 현행 리터럴 그대로(동작 보존).

	// Unarmed: run 500(직진)/350(strafe)/300(후진), sprint 균일 650(2단 램프 없음).
	UnarmedProfile.WalkSpeeds        = FVector(200.f, 175.f, 150.f);
	UnarmedProfile.RunSpeeds         = FVector(500.f, 350.f, 300.f);
	UnarmedProfile.SprintRunSpeeds   = FVector(650.f, 650.f, 650.f);
	UnarmedProfile.SprintSpeeds      = FVector(650.f, 650.f, 650.f); // 미사용(ramp off)이나 명료성 위해 명시
	UnarmedProfile.CrouchSpeed       = 150.f;
	UnarmedProfile.bSprintRampEnabled = false;

	// BigSword: 순항 270(jog254 8-way 통일), sprint 홀드해도 585 캡(2단 1100 미진입 — 콘텐츠 부재).
	// Walk/Crouch(165) 는 Armed 디폴트와 동일 → 완전 프로파일로 명시 재기술.
	FVBMovementProfile Big; // Armed 디폴트(165/430/585/1100/165/ramp on)에서 시작
	Big.RunSpeeds          = FVector(270.f, 270.f, 270.f);
	Big.SprintSpeeds       = FVector(585.f, 585.f, 585.f); // 미사용(ramp off)이나 명료성 위해 명시
	Big.bSprintRampEnabled = false;                        // sprint 램프 미진입(SprintRunSpeeds 585 캡)
	WeaponProfileOverrides.Add(EVBWeaponType::BigSword, Big);
}

const FVBMovementProfile& UVBMovementConfig::ResolveProfile(EVBWeaponType WeaponType, bool bArmed) const
{
	if (!bArmed) { return UnarmedProfile; }
	if (const FVBMovementProfile* Ov = WeaponProfileOverrides.Find(WeaponType)) { return *Ov; }
	return ArmedDefaultProfile;
}
