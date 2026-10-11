// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBMovementConfig.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UVBMovementConfig::UVBMovementConfig()
{
	// Fighter의 jog 8-way(전방 약238/대각선 약225), Run549, Sprint636과
	// Katana의 jog(254/240), Run550, Fast679를 기준으로 캡슐 속도를 맞춘다.

	// Unarmed: run 500(직진)/350(strafe)/300(후진), sprint 균일 650(2단 램프 없음).
	UnarmedProfile.WalkSpeeds        = FVector(200.f, 175.f, 150.f);
	UnarmedProfile.RunSpeeds         = FVector(500.f, 350.f, 300.f);
	UnarmedProfile.RunDiagonalSpeed = 0.f; // Armed jog 보정만 소비한다.
	UnarmedProfile.SprintRunSpeeds   = FVector(650.f, 650.f, 650.f);
	UnarmedProfile.SprintSpeeds      = FVector(650.f, 650.f, 650.f); // 미사용(ramp off)이나 명료성 위해 명시
	UnarmedProfile.CrouchSpeed       = 150.f;
	UnarmedProfile.bSprintRampEnabled = false;

	FVBMovementProfile Katana = ArmedDefaultProfile;
	Katana.RunDiagonalSpeed = 240.f;
	Katana.SprintSpeeds = FVector(680.f, 680.f, 680.f);
	WeaponProfileOverrides.Add(EVBWeaponType::Katana, Katana);

	// BigSword: jog254/대각선240, Run320, Fast487. 고속 Run/Fast는 별도 Sprint PSD에서 선택.
	// Walk/Crouch(165) 는 Armed 디폴트와 동일 → 완전 프로파일로 명시 재기술.
	FVBMovementProfile Big = ArmedDefaultProfile;
	Big.RunSpeeds          = FVector(255.f, 240.f, 225.f);
	Big.RunDiagonalSpeed   = 240.f;
	Big.SprintRunSpeeds    = FVector(485.f, 485.f, 485.f);
	Big.SprintSpeeds       = Big.SprintRunSpeeds;
	Big.SprintPoolJoinSpeed = 400.f;
	Big.bSprintRampEnabled = false;
	WeaponProfileOverrides.Add(EVBWeaponType::BigSword, Big);
}

const FVBMovementProfile& UVBMovementConfig::ResolveProfile(EVBWeaponType WeaponType, bool bArmed) const
{
	if (!bArmed) { return UnarmedProfile; }
	if (const FVBMovementProfile* Ov = WeaponProfileOverrides.Find(WeaponType)) { return *Ov; }
	return ArmedDefaultProfile;
}

#if WITH_EDITOR
EDataValidationResult UVBMovementConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	// 슬라이드는 «시작 속도 초과로 시작, 끝 속도 이하로 끝» 이다. 끝 >= 시작이면 시작 프레임에 바로 끝나 슬라이드가 보이지도 않는다.
	//   런타임은 이 경우 슬라이드를 아예 시작하지 않는다(UVBSlideMovementComponent::CanRequestSlide) - 여기서는 저작 때 막는다.
	if (SlideEndSpeed >= SlideStartSpeed)
	{
		Context.AddError(FText::Format(NSLOCTEXT("VBMovementConfig", "SlideSpeedOrder",
			"SlideEndSpeed({0}) 는 SlideStartSpeed({1}) 보다 작아야 한다 - 같거나 크면 슬라이드가 시작 프레임에 끝난다."),
			FText::AsNumber(SlideEndSpeed), FText::AsNumber(SlideStartSpeed)));
		Result = EDataValidationResult::Invalid;
	}
	// 검사를 했으니 «안 함»(부모 기본값 NotValidated)으로 돌려주지 않는다 - 오류가 없으면 유효다.
	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif
