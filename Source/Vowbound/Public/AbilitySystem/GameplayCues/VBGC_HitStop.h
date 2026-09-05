// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "VBGC_HitStop.generated.h"

/**
 * 히트스톱 GameplayCue
 * 타격 시 월드 시간을 순간 감속시켜 타격감을 연출한다.
 * ExecuteGameplayCue로 호출되며, 각 클라이언트에서 로컬로 실행된다.
 */
UCLASS()
class VOWBOUND_API UVBGC_HitStop : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	UVBGC_HitStop();
	
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
	
};
