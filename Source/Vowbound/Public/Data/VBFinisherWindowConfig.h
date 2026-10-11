// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBFinisherWindowConfig.generated.h"

class UAnimMontage;

/**
 * 처형 창의 튜닝값. 창을 여는 쪽(피해자)이 드는 자산이다.
 *
 * 무엇: 언제 열리나(체력 문턱) · 얼마나 열려 있나 · 그동안 무엇을 보여주나.
 * 왜 피해자 쪽인가: 창을 여는 판단을 피해자의 어트리뷰트 셋이 하기 때문이다. 값이 공격자 설정에 있으면
 *   같은 숫자를 양쪽에서 읽게 되고, 한쪽만 고친 날 조용히 어긋난다(규칙 26 「주인 둘 이상」).
 *   종전에는 이 문턱이 공격자의 UVBMedicalConfig 에 있었고 그래서 어트리뷰트 셋이 그것을 못 봤다.
 * 전제: UVBGA_FinisherWindow 가 이 자산을 든다. 공격자는 이 자산을 읽지 않는다 - 태그만 본다.
 * 부작용: 없음(순수 데이터).
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBFinisherWindowConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// 이 비율 이하로 떨어진 «그 타격» 에서 창이 열린다. 0.30 = 30%.
	// 종전 UVBMedicalConfig::LowHealthThreshold 의 자산 실측값이 0.30 이었고 그 값을 옮겨 왔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher",
	          meta=(ClampMin="0.0", ClampMax="1.0"))
	float HealthThreshold = 0.30f;

	// 창이 열려 있는 시간(초). 지나면 적이 풀려나 다시 싸운다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher", meta=(ClampMin="0.1"))
	float WindowDuration = 4.0f;

	// 창을 열기 전에 반응(피격·넘어짐·기상·슈퍼아머 공격)이 끝나기를 기다리는 최대 시간(초). 넘으면 창을 포기하고 경고한다.
	// 왜 상한이 있나: 기다리는 동안 «창 켜짐» 태그가 서서 기절 게이지 소진을 거절한다. 반응 태그가 새면(예: 공격 GA 가 슈퍼아머를
	//   못 떼고 끝남) 창도 안 열리고 기절도 영영 안 나온다(2026-09-30 /sw:verify). 병사 넘어짐 한 바퀴(넘어짐 1초 + 대기 0.8초 + 기상)가
	//   5초 안쪽이라 그보다 넉넉히 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher", meta=(ClampMin="0.5"))
	float MaxReactionWaitSeconds = 10.0f;

	// 창 동안 재생할 대기 자세.
	// 미할당을 정상으로 둔다 - 전용 그로기 자산이 아직 없다. 없으면 태그와 차단만 서고 자세는 안 바뀐다.
	//   선례: UVBGA_EnemyMeleeAttack 은 예고 몽타주 미할당을 같은 방식으로 다룬다.
	// Poise 경직 몽타주(AM_Enemy_Stagger)를 여기 꽂지 마라. 두 상태가 같은 그림이면 플레이어가
	//   «지금 처형할 수 있다» 와 «그냥 비틀거린다» 를 구분할 수 없다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher")
	TObjectPtr<UAnimMontage> WaitMontage = nullptr;

	// 창이 열릴 때 속도와 가속을 지울 것인가.
	// 이것은 «지금 이 프레임 멈춤» 이지 «못 움직임» 이 아니다. 적 BT 가 다음 주기에 다시 이동을 지시하면
	//   또 걷는다. 그것을 실제로 막는 것은 적 BT 의 UVBBTDecorator_HasGameplayTag 게이트(자산)다.
	// 왜 MOVE_None 으로 잠그지 않는가: 처형 장면이 피해자를 워프로 끌어다 놓는데, 이동을 잠그면
	//   그 워프가 죽는다. 사망 경로가 쓰는 DisableMovement 는 되돌릴 일이 없는 자리의 도구다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher")
	bool bStopMovementOnOpen = true;
};
