// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VBWeaponStateTypes.generated.h"

// 로코모션 상태 ENUM — 의미 정본 (DEC-006 리모델 2026-06-07, user 확정 다이어그램)
// 2×2(전투×무기) 중 "비전투+무기On" 칸은 존재하지 않는다 — 무기를 꺼냈다 = 전투 자세다.
// 식별자 rename 은 CoreRedirects 파급(BP/ABP 직렬화 전반) 대비 이득이 없어 보류 — 이 주석이 의미 정본.
UENUM(BlueprintType)
enum class EVBLocomotionState : uint8
{
	Unarmed UMETA(DisplayName="Unarmed"),		// 평상(비전투·무기 없음) — MM 로코모션
	ArmedExploration UMETA(DisplayName="Armed Exploration"), // 전투·무기Off — 무기별 Locomotion BS (이름과 달리 '무기 꺼냄' 상태가 아님)
	ArmedCombat UMETA(DisplayName="Armed Combat"), // 전투·무기On — 무기별 Strafe BS
};

// 무기 타입 열거형
UENUM(BlueprintType)
enum class EVBWeaponType : uint8
{
	None UMETA(DisplayName="None"),
	Katana UMETA(DisplayName="Katana"),			// Scalpel 오라
	BigSword UMETA(DisplayName="Big Sword"),	// 대형 Scalpel
	Fighter UMETA(DisplayName="Fighter"),		// Melee 오라 (맨손 격투)
	Magic UMETA(DisplayName="Magic"),			// 마법 오라 (원거리 투사체, 견제형 — 2026-06-01 추가, append-only)
};

class UNiagaraSystem;

// 무기 소환 이벤트 구조체
USTRUCT(BlueprintType)
struct FVBWeaponSummonData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon")
	EVBWeaponType WeaponType = EVBWeaponType::None;

	// 이 무기를 들었을 때 부여할 소속 태그(State.Armed.<무기>). State.Armed 는 코드가 함께 붙인다.
	// 왜 데이터인가: 종전에는 UVBWeaponStateComponent::ApplyWeaponTags 의 C++ switch 가 짝을 정했다.
	//   무기를 하나 추가하면 데이터 한 행이 아니라 C++ 을 또 고쳐야 했고, 빠뜨리면 default 로 빠져
	//   State.Armed 조차 안 붙었다 - 무기를 들었는데 전투로 인식되지 않는 무음 실패다(FIND-087).
	//   무기 추가 시 이 맵의 행은 어차피 만들어야 하므로, 짝을 그 행에 두면 빠뜨릴 자리가 사라진다.
	// 문자열로 조립하지 않는 이유: 태그는 VBGameplayTags 네이티브 선언이 정본이고(카탈로그 규칙),
	//   조립하면 심볼 grep 으로 사용처를 찾을 수 없게 된다. 에디터 피커가 선언된 것만 고르게 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon", meta=(Categories="State.Armed"))
	FGameplayTag ArmedTag;

	// 발도(상태3)에서 쓸 로코모션 Motion Matching 풀. 값의 의미는 "그 풀을 소유한 무기"다 - 자기 자신을
	//   가리키는 것이 보통이고, 전용 풀이 없으면 비워 둔다(None = 무기 없는 기본 풀).
	// 왜 데이터인가: 종전에는 VBAnimInstance 가 무기 이름을 나열해 풀을 골랐다(Fighter/Katana/BigSword 면
	//   자기 풀, 그 외 기본). 무기를 추가하면 애님 코드를 또 고쳐야 했고 빠뜨리면 말없이 기본 풀로 떨어졌다.
	//   어떤 풀을 쓰느냐는 그 무기가 어떤 클립을 가졌느냐의 문제이므로 홈은 무기 데이터다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon")
	EVBWeaponType ArmedMMPool = EVBWeaponType::None;

	// 납도(상태2·경계)에서 쓸 로코모션 MM 풀. 현재는 전 무기가 비어 있다 = 납도하면 기본 풀로 돌아간다
	//   (user 확정 2026-08-14: "무기를 넣으면 기본 상태"는 무기마다 달라질 규칙이 아니다).
	//   종전의 "무기 무관 Fighter 맨손 경계 풀 재사용"은 정지 Idle 만 카타나 클립이라 이동과 어휘가
	//   어긋났다 - 파이터로 납도하면 정지는 칼 든 자세, 움직이면 맨손 격투가 나왔다.
	//   대검 전용 납도 풀(ver_A 20클립)은 자산이 살아 있고 여기 BigSword 를 넣으면 즉시 되살아난다 -
	//   그 판단이 코드가 아니라 이 값 하나에 있다는 것이 이 필드의 존재 이유다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon")
	EVBWeaponType SheathedMMPool = EVBWeaponType::None;

	// 무기 소환 시 재생할 모션
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon")
	TObjectPtr<UAnimMontage> SummonMontage = nullptr;

	// 무기 수납 시 재생할 모션 (칼 넣기 등)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon")
	TObjectPtr<UAnimMontage> SheatheMontage = nullptr;

	// 무기 '전환' 몽타주 — From 무기별 페어 매핑 (탐험 스탠스, ver_A 계열).
	// 왜 페어 단위: 체인지 콘텐츠가 페어 전용(예: Fighter→Katana)이라 From이 다르면 연기가 어긋남.
	// 키 없는 From 페어는 몽타주 없이 SummonVFX만 발사 — 콘텐츠 생기면 데이터만 추가해 승격.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon")
	TMap<EVBWeaponType, TObjectPtr<UAnimMontage>> ChangeFromMontages;

	// 전투 스탠스(ArmedCombat)용 페어 변형 (ver_B 계열). 비어 있으면 ChangeFromMontages 폴백.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon")
	TMap<EVBWeaponType, TObjectPtr<UAnimMontage>> ChangeFromMontagesCombat;

	// 소환/전환/수납 공통 발사 나이아가라 (의료마법 소환 이펙트 — GDD 판타지)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon")
	TObjectPtr<UNiagaraSystem> SummonVFX = nullptr;

	// SummonVFX 를 붙일 소켓. 기본값은 이전 코드 리터럴과 같아 기존 자산은 저작 없이 현행 유지된다.
	// 무기마다 다를 수 있어 데이터로 뺐다 — 대검은 등, 마법은 지팡이 끝이 되는 순간 여기만 바꾼다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon")
	FName SummonVFXSocket = FName("hand_r");
};
