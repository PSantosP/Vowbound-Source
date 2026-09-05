// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Character/VBWeaponStateTypes.h"
#include "VBWeaponMovesetConfig.generated.h"

class UVBAttackConfig;

/**
 * FVBWeaponMoveset
 * 한 무기 타입의 공격 무브셋(UVBAttackConfig leaf 자산).
 * 강공격(Heavy)은 DEC-009 로 폐지됐다(RMB 를 가드/패링에 양도) — 무브셋은 이제 단일 Light 슬롯이다.
 *
 * 구 Heavy 필드에 CoreRedirects 를 넣지 않은 이유(2026-08-09 기록): 옮길 목적지가 없는 순수 삭제라
 *  리다이렉트가 성립하지 않는다. 리다이렉트는 '이름이 바뀐 값'을 잇는 것이고, 여기선 값 자체가 폐지됐다.
 *  기존 자산(DA_WeaponMovesets)은 재저장으로 이미 정리됐다(2026-07-26 리드백: 3무기 전부 Light leaf 만,
 *  Heavy 흔적 0). 즉 데이터는 정합하고, 남았던 것은 이 판단이 어디에도 안 적혀 있다는 것뿐이었다.
 */
USTRUCT(BlueprintType)
struct FVBWeaponMoveset
{
	GENERATED_BODY()

	// 공격 무브셋 (구 "약공격" — 강공격 폐지로 단일 슬롯이 됐으나, 데이터 재배선을 피하려 필드명은 유지)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBAttackConfig> Light = nullptr;
};

/**
 * UVBWeaponMovesetConfig
 *
 * 무기 타입(EVBWeaponType) → 약/강 무브셋 매핑. 단일 튜닝 자산.
 * 공격 GA(UVBGA_MeleeAttackBase)가 현재 무기 타입으로 leaf를 조회해 사용한다.
 *
 * 의도:
 *  - 무기별 GA 양산 없이, 단일 콤보 엔진이 데이터로 4-스타일(주먹/카타나/거대검/마법)을 분기
 *  - 미등록 타입 또는 미할당 leaf는 호출부에서 GA의 레거시 AttackConfig/AnimMontage로 폴백(회귀 안전)
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBWeaponMovesetConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 무기 타입별 약/강 무브셋
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TMap<EVBWeaponType, FVBWeaponMoveset> Movesets;

	// 무기 타입으로 leaf AttackConfig 반환. 미등록/미할당이면 nullptr (호출부가 레거시 폴백).
	UVBAttackConfig* GetAttackConfig(EVBWeaponType WeaponType) const;
};
