// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBEnemyAnimSet.generated.h"

class UAnimSequenceBase;
class UBlendSpace;
class FDataValidationContext; // IsDataValid 인자 - 헤더에서 Misc/DataValidation.h 를 끌지 않는다

/**
 * 적 아키타입 하나의 로코모션 클립 묶음 (design .sw/enemy-anim/design.md §6).
 *
 * 무엇: ABP_Enemy_Base 하나가 SK_Mannequin 위의 모든 적을 돌리고, «어느 클립인가» 는 이 자산이 정한다.
 *   골격이 다른 몬스터는 예외다 - AnimBlueprint 는 TargetSkeleton 에 묶이므로 그때는 ABP 를 복제한다
 *   (2026-09-14 보스 트롤이 첫 사례, ABP_Boss_Troll). 그 경우 그래프를 고치면 양쪽을 함께 고쳐야 한다.
 *   UVBEnemyAnimInstance 가 초기화 때 소유 적의 UVBEnemyConfig::AnimSet 에서 읽어 그래프의 자산 핀에 바인딩한다.
 * 왜 그래프가 아니라 자산인가: 「몬스터 ABP 각각」의 뜻은 «몬스터마다 다른 동작» 이지 «그래프 복사» 가 아니다.
 *   그래프를 복사하면 고칠 때마다 N 번 고친다 - 위 골격 예외가 치르는 대가가 정확히 그것이다.
 *   클립은 아키타입의 성질이니 아키타입 자산 아래가 홈이다.
 * 가정: Locomotion 은 2D 블렌드스페이스 - X 방향(-180..180, 대상을 보며 옆걸음), Y 속도(cm/s).
 *   적은 이동 중 대상을 보므로(CMC bOrientRotationToMovement=false + SetFocus) 8방향 세트가 필요하다.
 * 부작용: 없음(순수 데이터).
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBEnemyAnimSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 서 있을 때. 골반 높이가 Locomotion 과 맞는 클립을 고른다 - 병사 팩의 낮은 전투 대기(골반 71)는
	// 이동(90~94)과 20cm 튀어 뺐고 방패 든 대기(90)를 쓴다(2026-09-06 실측).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Animation")
	TObjectPtr<UAnimSequenceBase> Idle;

	// 이동. 2D 블렌드스페이스(X 방향 / Y 속도). 샘플 클립은 EnableRootMotion=true(제자리 잠금)여야 한다 -
	// 팩 클립은 root 트랙에 변위가 있으면서 플래그가 꺼져 있어, 그대로 넣으면 메시가 캡슐에서 미끄러진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Animation")
	TObjectPtr<UBlendSpace> Locomotion;

	// 이 속도(cm/s)를 넘으면 «이동 중» - Idle 과 Locomotion 을 가르는 문턱. 0 근처의 잔떨림이 이동으로 읽히지 않게.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Animation", meta=(ClampMin="0.0"))
	float MovingSpeedThreshold = 10.0f;

	// Idle <-> Locomotion 블렌드 시간은 여기 없다. ABP_Enemy_Base 의 BlendListByBool 핀 리터럴(0.2)이 홈이다 -
	//   2026-09-06 핀 함수 바인딩이 노드에 값을 넘기지 못한 것이 실측돼(계측 로그 값은 정상, 화면은 T 포즈) 바인딩을 걷어냈다.
	//   아키타입별 블렌드 시간이 필요해지면 그때 노드 함수로 넘긴다.

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
