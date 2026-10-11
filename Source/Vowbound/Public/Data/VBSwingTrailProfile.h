// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBSwingTrailProfile.generated.h"

class UMaterialInterface;
class USoundBase;

/**
 * 휘두름 궤적의 모양새. 설계 .sw/weapon-trace 18절.
 *
 * 날의 위치·길이는 여기 없다 - 판정 모양(UVBMeleeTraceShapeSet)을 그대로 따른다. 보이는 궤적과 맞는 자리가 같은 데이터에서 나온다.
 * 모양마다 기본 프로필(FVBMeleeTraceShapeDef::TrailProfile)이 있고, 궤적 노티(UVBANS_WeaponTrail)가 스윙별로 덮어쓸 수 있다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBSwingTrailProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 궤적 띠 머티리얼 - 무기별 머티리얼 인스턴스(MI_VB_SwingTrail_*)를 꽂는다. 색·밝기는 그 인스턴스의 파라미터가 홈이다
	//   (성능 기준: 베이스 M_ 하나 + MI_ 파생, 설계 19절 1). 정점 규약: 알파 = 그 구역 수명 안의 남은 비율(새 조각 1 -> 사라질 조각 0),
	//   UV.X = 같은 나이 비율(0 새 -> 1 늙음), UV.Y = 0 날 시작 / 1 날 끝. 띠 구역은 SegmentLifetime, 왜곡 구역은 DistortionLifetime 으로 잰다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UMaterialInterface> TrailMaterial;

	// 공기 왜곡(굴절) 머티리얼 인스턴스(MI_VB_SwingTrailDistortion_*). 같은 날 자리를 두 번째 구역으로 한 번 더 그린다. 비우면 안 그린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UMaterialInterface> DistortionMaterial;

	// 띠 조각 하나가 사라지기까지(초, 공격자 시간 - 히트스톱에 궤적도 멈춘다). 짧을수록 날에 바짝 붙어 날카롭다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0.02"))
	float SegmentLifetime = 0.12f;

	// 공기 왜곡이 그 자리에 남아 일렁이는 시간(초, 같은 공격자 시간). 띠와 따로 둔다 - 같은 수명이면 밝은 띠가 그 위를 덮은 채
	//   함께 사라져 왜곡이 보이지 않았다(설계 20절). 조각은 둘 중 긴 쪽까지 산다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0.02"))
	float DistortionLifetime = 0.35f;

	// 조각이 살아 있어야 하는 시간 - 그려지는 구역 중 가장 긴 수명.
	float GetMaxSegmentLifetime() const { return DistortionMaterial ? FMath::Max(SegmentLifetime, DistortionLifetime) : SegmentLifetime; }

	// 프레임 사이를 날 끝이 이만큼(cm) 움직일 때마다 한 조각씩 채운다. 작을수록 호가 매끈하다. 판정 컴포넌트와 같은 변환 보간이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="1"))
	float SubstepSpacing = 8.0f;

	// 궤적이 열릴 때 한 번 낸다. 변주(무작위 선택·음높이 흔들기)는 사운드 큐(SC_Swing_*)가 맡는다 - 병사 휘두름 소리와 같은 방식이다.
	//   비우면 소리 없음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<USoundBase> WhooshSound;
};
