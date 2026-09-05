// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Character/VBWeaponStateTypes.h" // EVBWeaponType (WeaponDodgeSets 키)
#include "VBDodgeConfig.generated.h"

class UAnimMontage;

/** 회피 4방향. 액터 로컬 기준(UE 규약 X=전방/Y=우측)으로 판정한다. */
UENUM(BlueprintType)
enum class EVBDodgeDirection : uint8
{
	Forward		UMETA(DisplayName="Forward"),
	Backward	UMETA(DisplayName="Backward"),
	Left		UMETA(DisplayName="Left"),
	Right		UMETA(DisplayName="Right")
};

/**
 * FVBDodgeMontageSet
 * 한 세트(무기 or 통일)의 4방향 회피 몽타주.
 * 방향은 액터 로컬 기준(UE 규약 X=전방/Y=우측)으로 판정해 고른다 — 클립은 이미 방향별 전용 저작이라
 * 축 변환이 필요 없다(클립 내부 root 축 규약과 혼동 금지).
 * 미할당(null) 슬롯은 상위 폴백으로 내려간다(무기세트 → 통일세트 → LaunchCharacter 안전망).
 */
USTRUCT(BlueprintType)
struct FVBDodgeMontageSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Dodge")
	TObjectPtr<UAnimMontage> Forward = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Dodge")
	TObjectPtr<UAnimMontage> Backward = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Dodge")
	TObjectPtr<UAnimMontage> Left = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Dodge")
	TObjectPtr<UAnimMontage> Right = nullptr;
};

/**
 * 회피(Dodge) 설정 DataAsset
 * 무엇: Dodge 어빌리티에서 사용하는 거리/시간/강도 수치를 외부화
 * 왜: Data-Driven 설계 — C++ 하드코딩 없이 에디터에서 밸런스 조정 가능
 * 가정: VBGA_Dodge에서 TObjectPtr<UVBDodgeConfig>로 참조
 *       BP 서브클래스(BP_DodgeConfig)에서 값 설정
 * 부작용: DodgeStrength 값이 LaunchCharacter에 직접 전달되므로
 *         과도한 값 설정 시 벽 관통 등 물리 문제 발생 가능
 */
UCLASS()
class VOWBOUND_API UVBDodgeConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vowbound|Combat")
	float DodgeDuration = 0.4f; // 무적시간
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vowbound|Combat")
	float DodgeStrength = 1200.0f; // LaunchCharacter 속도
	// 회피 시 상방 부스트 — LaunchCharacter 속도에 합성(전방*DodgeStrength + 이 값)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vowbound|Combat")
	FVector DodgeVerticalBoost = FVector(0.0f, 0.0f, 100.0f);

	// ── 닷지 애니메이션 (A안: 클립 루트모션이 이동 담당, 2026-07-26) ──
	// 통일 닷지 — 비무장(None)·Fighter·Magic 등 WeaponDodgeSets 에 등록되지 않은 전부가 쓰는 공용 세트.
	// 왜 TMap 의 None 키가 아니라 전용 필드인가: None 키만 두면 Magic 같은 미등록 무기가 폴백을 못 받는다.
	// 이 필드가 "등록 안 된 전부"의 단일 답 (콘텐츠 = Frank Evade 4방향, 0.75~0.80s/~288cm 로 방향 균일).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Dodge")
	FVBDodgeMontageSet DefaultDodgeSet;

	// 무기별 전용 세트(Katana/BigSword). 미등록 무기는 DefaultDodgeSet 으로 폴백.
	// 구조는 UVBWeaponMovesetConfig::Movesets 와 동형 — 무기 추가 = 데이터 1행, C++ 무변경.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Dodge")
	TMap<EVBWeaponType, FVBDodgeMontageSet> WeaponDodgeSets;

	// 닷지 몽타주 재생속도. 1.0 = 클립 원래 템포(user 확정: 원래 템포 먼저).
	// 루트모션은 PlayRate 에 비례해 속도만 빨라지고 이동거리는 보존된다. 느리면 이 값만 올리면 되고
	// 코드/에셋 변경이 없다. 클립 길이 0.67~1.20s 대비 기존 무적창 0.4s 라 압축 여지를 열어둔 노브.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Dodge", meta=(ClampMin="0.1"))
	float DodgeMontagePlayRate = 1.0f;

	// 무기 타입 + 방향으로 몽타주 해석. 폴백 = 무기세트 → 통일세트 → nullptr(호출부가 LaunchCharacter 안전망).
	// 방향 판정 자체는 GA 책임(입력 벡터 → 로컬 4분면), 여기선 해석만 한다.
	UAnimMontage* ResolveDodgeMontage(EVBWeaponType WeaponType, EVBDodgeDirection Direction) const;
};
