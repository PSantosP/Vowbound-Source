// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "VBGC_HitFlash.generated.h"

/**
 * 히트 플래시 GameplayCue
 * 피격자 메시를 순간 발광시킨다. ExecuteGameplayCue로 호출되며, 각 클라이언트에서 로컬로 실행된다.
 *
 * 상태를 갖지 않는다(GameplayCueNotify_Static 은 CDO 로 실행된다) — 피격자에 붙은
 * UVBHitFlashComponent 로 위임만 한다. MID/타이머 같은 인스턴스 상태는 전부 그쪽 소유다.
 *
 * 생성자에서 GameplayCueTag 를 설정하지 않는다(중요, 의도적):
 *   네이티브 부모가 태그를 들고 있으면 DeriveGameplayCueTagFromClass 가 "부모와 같은 태그"를 감지해
 *   자산명에서 태그를 유도하려 시도하고, 실패하면 태그만 되돌린 채 GameplayCueName 을 갱신하지 않고 반환한다.
 *   그 결과 레지스트리 키가 None 이 되어 큐 세트 등록에서 탈락한다.
 *   태그는 BP 자식(GCN_HitFlash)의 Class Defaults 에서 지정한다 — 실제로 동작 중인 GCN_DamagePhysical 과 같은 경로.
 */
UCLASS()
class VOWBOUND_API UVBGC_HitFlash : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

};
