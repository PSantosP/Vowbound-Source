// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBGC_HitFlash.h"

#include "Character/VBHitFlashComponent.h"
#include "GameFramework/Actor.h"
#include "Vowbound/Vowbound.h"

bool UVBGC_HitFlash::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	if (!MyTarget)
	{
		return false;
	}

	// 상태 소유자에게 위임한다. 큐는 CDO 라 MID 도 타이머도 들 수 없다.
	if (UVBHitFlashComponent* FlashComponent = MyTarget->FindComponentByClass<UVBHitFlashComponent>())
	{
		FlashComponent->TriggerFlash(Parameters);
		return true;
	}

	// Warning 이 아니라 Verbose 인 이유: 플래시 컴포넌트가 없는 액터가 맞는 것은 정상 상황이고,
	// 매 타격마다 경고를 찍으면 전투 중 로그가 이것 하나로 가득 찬다.
	VB_LOG(Verbose, "VBGC_HitFlash: %s 에 UVBHitFlashComponent 없음 — 플래시 스킵", *MyTarget->GetName());
	return false;
}
