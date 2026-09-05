// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBParryConfig.h"

#include "Animation/AnimMontage.h" // UAnimMontage::GetSectionIndex

#if WITH_EDITOR
#include "Misc/DataValidation.h"   // FDataValidationContext::AddError
#define LOCTEXT_NAMESPACE "VBParryConfig"
#endif

const FVBGuardSet* UVBParryConfig::FindGuardSet(EVBWeaponType WeaponType) const
{
	// 폴백 없음이 설계다. 미등록은 nullptr 이고, 호출부(UVBGA_Guard)가 그것을 "가드 불가"로 읽는다.
	//  UVBDodgeConfig::ResolveDodgeMontage 처럼 통일 세트로 내리면 "BigSword 든 채 카타나 포즈"가 나온다
	//  (가드엔 무기 무관 범용 클립이 없다 — 근거는 헤더 클래스 주석).
	return WeaponGuardSets.Find(WeaponType);
}

#if WITH_EDITOR
EDataValidationResult UVBParryConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 등록된 모든 세트를 순회한다. 에셋 스코프 검사라 어느 무기를 먼저 플레이했든 전 무기가 검사된다
	//  (옛 런타임 래치의 커버리지 구멍을 구조적으로 닫는 지점).
	for (const TPair<EVBWeaponType, FVBGuardSet>& Pair : WeaponGuardSets)
	{
		// None 키 금지 (2026-07-26 2차 verify): "키 존재 = 가드 권한" 규약에서 None 행은 맨손 가드 허가가 된다.
		//  None 은 정식 enum 값이자 정식 TMap 키라 저작 실수로 들어올 수 있고, AVBCharacter::BeginGuard 는
		//  WeaponStateComponent 가 null 일 때 None 으로 조회하므로 그 경로에서도 세트가 잡힌다.
		//  헤더가 "타입 수준에서 불가능"이라 주장하는 강도를 실제로 맞추려면 여기서 닫아야 한다.
		if (Pair.Key == EVBWeaponType::None)
		{
			Context.AddError(LOCTEXT("VBParry_NoneKey",
				"WeaponGuardSets 에 EVBWeaponType::None 행이 있습니다. 키 존재는 '그 무기의 가드 권한'을 뜻하므로 "
				"None 행은 맨손 가드를 허가하는 것이 됩니다. 의도한 것이 아니라면 이 행을 삭제하세요."));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		const UAnimMontage* Montage = Pair.Value.GuardMontage;

		// 몽타주 미할당은 에러가 아니다. 키 존재만으로 '애니 없는 권위 가드'가 성립하는 게 설계다.
		if (!Montage)
		{
			continue;
		}

		const FName SectionsToCheck[] = { Pair.Value.GuardStartSection, Pair.Value.GuardLoopSection, Pair.Value.GuardEndSection };
		for (const FName& SectionName : SectionsToCheck)
		{
			// 왜 필요한가: Montage_SetNextSection 은 목표 섹션명이 없으면 INDEX_NONE 을 저장하고도 성공으로
			//  돌아온다 — 로그 한 줄 없다(소스 섹션명 오타만 엔진이 Warning).
			//  증상은 "가드 루프가 1회 재생 후 조용히 끝남"이라 애니 버그로 오진하기 쉽다.
			//  여기서 잡으면 플레이 이전, 에셋 저장 시점에 드러난다.
			if (Montage->GetSectionIndex(SectionName) == INDEX_NONE)
			{
				Context.AddError(FText::Format(
					LOCTEXT("VBParry_MissingGuardSection",
						"WeaponGuardSets[{0}]: 몽타주 '{1}' 에 섹션 '{2}' 가 없습니다. "
						"Montage_SetNextSection 은 무음 실패하므로 가드 루프/복귀가 조용히 깨집니다."),
					FText::FromString(UEnum::GetValueAsString(Pair.Key)),
					FText::FromString(GetNameSafe(Montage)),
					FText::FromName(SectionName)));

				Result = EDataValidationResult::Invalid;
			}
		}
	}

	return Result;
}
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
