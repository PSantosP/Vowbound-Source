// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/VBWeaponStateTypes.h" // EVBWeaponType (WeaponProfileOverrides 키)
#include "Engine/DataAsset.h"
#include "VBMovementConfig.generated.h"

/**
 * 한 (무기/상태) 조합의 속도 프로파일. X=Forward Y=Strafe Z=Backward.
 * UpdateMaxSpeed() 가 velocity-각도 zone(0/1/2) 으로 X/Y/Z 사이 lerp 하여 CMC->MaxWalkSpeed 갱신.
 * 특례 소거의 핵심: 무기별 속도를 필드가 아니라 프로파일(데이터)로 둔다 — BigSword override 추가/삭제에 C++ 무변경.
 * 인라인 디폴트 = Armed(Fighter/Katana/Magic) 값. Unarmed/BigSword 는 config 생성자에서 명시 초기화(VBMovementConfig.cpp).
 */
USTRUCT(BlueprintType)
struct FVBMovementProfile
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion", meta=(ToolTip="X=Forward Y=Strafe Z=Backward"))
	FVector WalkSpeeds = FVector(165.f, 165.f, 165.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion", meta=(ToolTip="기본 순항 jog(8-way strafe)"))
	FVector RunSpeeds = FVector(430.f, 430.f, 430.f);

	// Sprint 1단계(홀드 0~SprintRampDuration). bSprintRampEnabled=false 인 프로파일은 sprint 시 항상 이 값.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion")
	FVector SprintRunSpeeds = FVector(585.f, 585.f, 585.f);

	// Sprint 2단계(홀드 ≥ SprintRampDuration). bSprintRampEnabled=true 일 때만 도달.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion")
	FVector SprintSpeeds = FVector(1100.f, 1100.f, 1100.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion")
	float CrouchSpeed = 165.f;

	// 특례 소거용 플래그: true=2단 램프(1단→2단 승급), false=SprintRunSpeeds 에 캡(BigSword) 또는 단일 sprint(Unarmed).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion")
	bool bSprintRampEnabled = true;
};

/**
 * 이동/로코모션 튜닝 DataAsset (데이터화 P1.2, 2026-07-22).
 * 무엇: AVBCharacter in-class 로 박혀 있던 CMC 물리·회전율·무기별 속도셋을 외부화.
 * 왜: 이동/정지 FEEL 은 가장 자주 만지는 튜닝 표면 — 컴포넌트 디폴트에 있으면 매번 빌드/재시작(Live Coding 한계).
 *     DataAsset 로 빼면 에디터 즉시 튜닝. 변형(무기)은 코드가 아니라 프로파일 데이터로 둔다(Lyra 컴포지션 정합).
 * 가정: AVBCharacter 가 TObjectPtr<UVBMovementConfig> 로 참조. 미할당 시 CDO 디폴트 fallback (GetMovementConfig).
 * 부작용: 없음(순수 데이터). StrafeSpeedMapCurve(UCurveFloat) 는 BP 에셋 참조라 캐릭터에 잔류(CDO 가 못 담음).
 * 범위: MOVEMENT ONLY. 카메라 노브는 차기 VBCameraConfig.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBMovementConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVBMovementConfig();

	// ── CMC 물리(무기 무관, BeginPlay 1회 적용) ──
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	float BrakingDecelerationWalking = 750.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	float MaxAcceleration = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	float GroundFriction = 5.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	float BrakingFrictionFactor = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	float JumpZVelocity = 500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	float AirControl = 0.35f;

	// ── 회전율(매 tick 소비 — UpdateRotationRateForMovementMode) ──
	// Yaw=-1 = infinite(즉시 snap, GASP 관용). 불변에 가깝지만 BUG-010 라이브 튜닝을 위해 외부화했다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	FRotator DefaultRotationRate = FRotator(0.f, -1.f, 0.f);

	// 공중(Falling) 회전 — instant snap 이 공중에서 부자연스러워 분리. GASP Falling 분기 (0,200,0).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	FRotator AirborneRotationRate = FRotator(0.f, 200.f, 0.f);

	// 무장 지상 회전 — SM/Release 분기는 OffsetRootBone 흡수를 꺼 캡슐 회전이 메쉬 직결 → 유한 속도로 부드럽게.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	FRotator ArmedGroundedRotationRate = FRotator(0.f, 360.f, 0.f);

	// ── 기타 전역 ──
	// 비무장 crouch 보폭(SM/BS 경로). 무장 crouch 는 프로파일 CrouchSpeed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion")
	float NormalCrouchMaxWalkSpeed = 150.f;

	// sprint 1→2단 승급 홀드 시간(초). 프로파일 bSprintRampEnabled=true 일 때만 유효.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion")
	float SprintRampDuration = 0.6f;

	// ── 속도 프로파일(base + override) ──
	// 비무장 base. config 생성자에서 (200,175,150)/(500,350,300)/650/150/ramp off 로 초기화.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion")
	FVBMovementProfile UnarmedProfile;

	// 무장 base(Fighter/Katana/Magic). struct 인라인 디폴트(165/430/585/1100/165/ramp on) 그대로.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion")
	FVBMovementProfile ArmedDefaultProfile;

	// 무기별 오버라이드(미등록 = ArmedDefault base). 현재 BigSword(Run 270, ramp off) 1건.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Locomotion")
	TMap<EVBWeaponType, FVBMovementProfile> WeaponProfileOverrides;

	// 무기타입·무장여부로 프로파일 해석. !bArmed=Unarmed / bArmed & override 있음=override / 그 외=ArmedDefault.
	// 미등록 무기는 ArmedDefault 폴백(회귀 안전 — HitReact::ResolveMontage / Moveset::GetAttackConfig 동형).
	const FVBMovementProfile& ResolveProfile(EVBWeaponType WeaponType, bool bArmed) const;
};
