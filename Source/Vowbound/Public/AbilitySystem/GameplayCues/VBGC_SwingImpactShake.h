// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "VBGC_SwingImpactShake.generated.h"

/**
 * 방향성 임팩트 쉐이크 GameplayCue
 * 휘두른 방향으로 카메라를 한 번 밀었다 되돌린다. ExecuteGameplayCue로 호출되며 각 머신에서 로컬로 실행된다.
 *
 * 왜 BP 가 아니라 C++ 인가:
 *  - BP 의 Play World Camera Shake 노드는 PlaySpace 를 노출하지 않아 방향을 지정할 방법이 아예 없다.
 *    또한 그 노드는 모든 PlayerController 를 순회하므로 남의 스윙에 내 화면이 흔들린다.
 *  - 즉 승격은 취향이 아니라 기능의 전제다.
 *
 * 상태를 갖지 않는다(GameplayCueNotify_Static 은 CDO 로 실행된다).
 *  방향은 Parameters.Normal, 쉐이크 자산과 세기는 Parameters.SourceObject(UVBSwingImpactProfile)로 들어온다.
 *  이 클래스가 값을 하나도 소유하지 않는 것이 설계다 — 튜너블은 전부 프로필 자산과 쉐이크 자산에 있다.
 */
UCLASS()
class VOWBOUND_API UVBGC_SwingImpactShake : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	UVBGC_SwingImpactShake();

	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

};
