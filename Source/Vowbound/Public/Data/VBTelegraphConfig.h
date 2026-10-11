// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBTelegraphConfig.generated.h"

class UMaterialInterface;
class USoundAttenuation;
class USoundBase;

/**
 * FVBTelegraphAnswerCue
 * 답(Attack.Answer.*) 하나의 연출 묶음 - 색, 몸 발광 세기와 감쇠, 답 소리.
 *
 * 왜 struct 인가: 답이 늘면(가드만 되는 노랑) 이 struct 가 맵의 값으로 한 행 더 늘어난다.
 *   값 타입을 나중에 스칼라에서 struct 로 바꾸면 이미 직렬화된 행이 전부 사라진다(FVBHitFlashProfile 과 같은 관례).
 */
USTRUCT(BlueprintType)
struct FVBTelegraphAnswerCue
{
	GENERATED_BODY()

	// 이 답의 색. 몸 발광과 HUD 가장자리 화살표가 같은 값을 쓴다 - 색이 곧 답이라 홈이 하나여야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Telegraph")
	FLinearColor Color = FLinearColor::White;

	// 몸 발광 세기(오버레이 머티리얼 FlashIntensity). 피격 플래시(2.0)와 구분되게 조금 낮거나(흰색) 높게(빨강) 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Telegraph", meta=(ClampMin="0.0"))
	float GlowIntensity = 1.5f;

	// 발광 감쇠 지수. 1 = 선형, 1 미만 = 예고 내내 밝다가 끝에 꺼지는 «유지형». 피격 플래시의 임팩트형(>1)과 반대다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Telegraph", meta=(ClampMin="0.01"))
	float GlowFalloffExponent = 0.35f;

	// 답 소리(3D, 적에 부착). 공통 예고 소리(기합·휘두름) 위에 겹친다. 흰색은 짧은 종, 빨강은 낮은 위협음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Telegraph")
	TObjectPtr<USoundBase> AnswerSound = nullptr;
};

/**
 * UVBTelegraphConfig
 * 적 공격 예고 연출의 데이터 홈(design .sw/combat-feel/design.md §11).
 *
 * 무엇: 답 태그 -> {색·발광·답 소리} 맵 + 답과 무관한 공통 연출(발광 머티리얼, 기합, 휘두름, 3D 감쇠, 화면 밖 경고음).
 * 왜 DataAsset 인가: 소비자 UVBGC_Telegraph 는 GameplayCueNotify_Static 이라 CDO 로 실행돼 인스턴스 상태를 못 가진다 -
 *   데이터는 바깥 자산이어야 한다(UVBHitFlashConfig 와 같은 이유). 저작 지점 = GCN_Telegraph 클래스 기본값 하나.
 * 왜 공격 자산(UVBAttackConfig)에 두지 않는가: 답은 공격의 성질이지만 «빨강이 어떤 빨강인가» 는 게임 전체의 약속이다.
 *   공격마다 색을 두면 같은 답이 적마다 다른 색으로 나와 플레이어가 배운 약속이 깨진다.
 * 부작용: 없음(순수 데이터).
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBTelegraphConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 답 태그별 연출. 미등록 답은 공통 연출만 나가고 큐가 Warning 을 찍는다 - 조용히 흰색으로 떨어뜨리면
	//   «답이 안 붙은 공격» 을 아무도 못 본다(규칙 27).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Telegraph", meta=(Categories="Attack.Answer"))
	TMap<FGameplayTag, FVBTelegraphAnswerCue> AnswerCues;

	// 몸 발광 오버레이 머티리얼. 피격 플래시의 MI(MI_VB_HitFlash_White)를 그대로 꽂는다 -
	//   UVBHitFlashComponent 는 머티리얼당 MID 하나를 들고 머티리얼이 바뀔 때만 오버레이를 다시 거는데(렌더 상태 재생성),
	//   플래시와 다른 머티리얼을 주면 예고마다 그 비용을 낸다. 색·세기·감쇠는 파라미터라 같은 MI 로 충분하다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Telegraph")
	TObjectPtr<UMaterialInterface> GlowOverlayMaterial = nullptr;

	// 예고 공통 소리 둘(3D, 적에 부착). 기합 = «누가», 휘두름 = «언제». 둘 다 선택이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Telegraph")
	TObjectPtr<USoundBase> TelegraphVoice = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Telegraph")
	TObjectPtr<USoundBase> TelegraphWhoosh = nullptr;

	// 위 3D 소리들의 감쇠. 미할당이면 엔진 기본(감쇠 없음 = 어디서나 같은 크기)이라 방향을 못 읽는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Telegraph")
	TObjectPtr<USoundAttenuation> Attenuation = nullptr;

	// 예고에 들어간 적이 로컬 화면 밖일 때 한 번 재생하는 2D 경고음. 예고 소리와 음색을 갈라야 «방향 경고» 로 읽힌다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Telegraph")
	TObjectPtr<USoundBase> OffScreenWarningSound = nullptr;

	// 답 태그로 연출 행 조회. 없으면 nullptr - 호출부가 경고를 남긴다.
	const FVBTelegraphAnswerCue* FindAnswerCue(const FGameplayTag& Answer) const;
};
