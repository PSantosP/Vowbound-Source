// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBCameraConfig.generated.h"

/**
 * 탐험(기본) 카메라 리그 노브. AVBCharacter 의 SpringArm/Camera 컴포넌트에 적용된다.
 *
 * 범위 경계: 여기 있는 건 "평상시 카메라"뿐이다. 락온/전투 카메라 값은
 * `UVBTargetLockConfig`(CombatTargetArmLength/CombatSocketOffset/CombatCameraLagSpeed/CombatFieldOfView)가
 * 계속 소유한다 — 그건 "락온이 무엇을 바꾸는가"라는 락온 시스템의 관심사이기 때문.
 * VBTargetLockComponent 는 평상시 값을 런타임 컴포넌트에서 캡처해 그 사이를 Lerp 하므로,
 * 이 config 는 그 캡처보다 먼저 적용돼야 한다(AVBCharacter::PostInitializeComponents — BeginPlay 는 늦다).
 *
 * 구조 규약(config_data_structure_standard): 변형이 없는 전역 노브는 플랫 DataAsset 이다.
 * 무기/상태별 변형이 생기면 그때 프로파일 컴포지션으로 승격한다(FVBMovementProfile 선례) — 지금은 YAGNI.
 *
 * 인라인 디폴트 = 데이터화 이전 AVBCharacter in-class 값 그대로(동작 보존).
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBCameraConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// SpringArm 길이(cm) — 캐릭터에서 카메라까지의 기본 거리.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	float TargetArmLength = 400.0f;

	// SpringArm 소켓 오프셋 — Y=우측(어깨 너머), Z=상단. 3인칭 오버숄더 구도를 만든다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	FVector SocketOffset = FVector(0.0f, 50.0f, 80.0f);

	// 카메라 추적 지연 속도 — 낮을수록 부드럽고 늘어진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	float CameraLagSpeed = 10.0f;

	// lag 누적 거리 상한(cm). 0 이면 무제한 → 빠른 가속 시 카메라가 캡슐보다 과하게 뒤처진다.
	// GASP 정렬값 200.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	float CameraLagMaxDistance = 200.0f;

	// 시야각(도).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	float FieldOfView = 75.0f;
};
