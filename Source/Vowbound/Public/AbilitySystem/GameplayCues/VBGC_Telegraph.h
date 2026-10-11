// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "VBGC_Telegraph.generated.h"

class UVBTelegraphConfig;

/**
 * 적 공격 예고 GameplayCue (design .sw/combat-feel/design.md §11)
 * Add/Remove 큐. 예고 시작(OnActive)에 몸 발광 + 소리 + 위협 등록 + 화면 밖 경고음, 스윙 시작·취소(OnRemove)에 발광 정지 + 등록 해제.
 *
 * 상태를 갖지 않는다(GameplayCueNotify_Static 은 CDO 로 실행된다) - 발광은 적의 UVBHitFlashComponent 가, 목록은
 *   UVBThreatTrackerSubsystem 이 든다. 소리는 부착 오디오 컴포넌트가 스스로 산다(적이 죽으면 함께 멈춘다). VBGC_HitFlash 와 같은 구조.
 *
 * 파라미터 계약(보내는 쪽 = UVBGA_EnemyMeleeAttack):
 *   RawMagnitude         = 예고 길이(초). 클라는 UVBEnemyConfig 를 모르므로 서버가 실어 보낸다.
 *   AggregatedSourceTags = 답 태그(Attack.Answer.*) 하나. 색·소리 행의 키다.
 *   AggregatedSourceTags 는 FGameplayCueParameters::NetSerialize 에 포함된다 - 원격 클라에서도 온다.
 *
 * 생성자에서 GameplayCueTag 를 설정하지 않는다(VBGC_HitFlash 와 같은 이유 - 네이티브 부모가 태그를 들면
 *   DeriveGameplayCueTagFromClass 가 레지스트리 키를 None 으로 만든다). 태그는 BP 자식(GCN_Telegraph)의 클래스 기본값에서 준다.
 */
UCLASS()
class VOWBOUND_API UVBGC_Telegraph : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
	virtual bool OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

protected:
	// 연출 데이터. 저작 지점 = GCN_Telegraph 클래스 기본값. 미할당이면 CDO 폴백 = 빈 자산 = 무연출 + 경고.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Telegraph")
	TObjectPtr<UVBTelegraphConfig> TelegraphConfig;

private:
	const UVBTelegraphConfig* GetConfig() const;

	// 파라미터에서 답 태그를 꺼낸다. Attack.Answer 아래 첫 태그. 없으면 빈 태그.
	static FGameplayTag ResolveAnswerTag(const FGameplayCueParameters& Parameters);
};
