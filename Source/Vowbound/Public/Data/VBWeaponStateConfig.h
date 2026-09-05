// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBWeaponStateConfig.generated.h"

/**
 * 무기 상태(전투 진입/이탈) 타이밍 튜닝 DataAsset.
 * 무엇: VBWeaponStateComponent 의 전투 해제/적 감지 시간을 외부화.
 * 왜: 전투 "느낌"을 좌우하는 타이밍값 — 디자이너가 컴포넌트 디폴트를 뒤지지 않고 한곳에서 튜닝.
 * 가정: VBWeaponStateComponent 가 TObjectPtr<UVBWeaponStateConfig> 로 참조. 미할당 시 CDO 디폴트 fallback.
 * 부작용: 없음(순수 데이터). 무기별 메시/타입 매핑(WeaponDataMap)은 캐릭터별 데이터라 컴포넌트에 잔류.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBWeaponStateConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 전투 행위(공격/피격/락온) 후 이 시간 경과 시 전투 이탈 시도(초)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	float CombatExitDelay = 4.0f;

	// 이 반경 내에 적이 있으면 전투 유지(자동 이탈 안 함). 1500 = 15m
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	float EnemyProximityRadius = 1500.0f;

	// (SheatheDelay 는 DEC-006 ④에서 제거 — 전투 이탈이 납도를 동반해 Unarmed 직행하므로 별도 수납 지연 없음.
	//  기존 DA 에셋에 직렬화된 잔존값은 로드 시 무시된다.)

	// 무기 전환 몽타주를 재생할 최대 평면 속도(cm/s). 초과 시 몽타주 스킵, 소환 VFX만 발사.
	// 왜: 체인지 애님은 idle 스탠스 기준이라 이동 중 재생하면 발이 미끄러짐 — GDD 템포(Spider-Man speedy)상
	//     달리면서 바꾸면 즉시 전환이 맞고, 멈춰 있을 때만 연기를 보여준다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	float ChangeActingMaxSpeed = 150.0f;

	// 이 평면 속도(cm/s) 초과면 '이동 중=활동 중'으로 보고 전투 이탈을 보류(타이머 재연장).
	// 정지(idle) 판정 임계 — user 2026-06-09 "움직이는 건 아무것도 안 함이 아니다". 현행 10.0f 보존.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	float CombatExitIdleSpeedThreshold = 10.0f;
};
