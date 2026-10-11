// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

/**
 * PlayerState 에 붙은 플레이어 컴포넌트(인벤토리·장비·성장 ...)를 폰·컨트롤러·PlayerState 어느 쪽에서든 찾는다.
 * 같은 FindFor 가 인벤토리·장비에 한 벌씩 있었고 성장이 셋째가 되어 뽑았다(2026-10-06, 셋째 반복에서 뽑기 규칙). 인벤토리·장비·성장이 이 함수를 쓴다.
 */
namespace VBPlayerStateLookup
{
	inline const APlayerState* ResolvePlayerState(const AActor* Actor)
	{
		if (const APlayerState* PlayerState = Cast<APlayerState>(Actor))
		{
			return PlayerState;
		}
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			return Pawn->GetPlayerState();
		}
		if (const AController* Controller = Cast<AController>(Actor))
		{
			return Controller->PlayerState;
		}
		return nullptr;
	}

	// 없으면 nullptr.
	template <typename TComponent>
	TComponent* FindComponent(const AActor* Actor)
	{
		const APlayerState* PlayerState = ResolvePlayerState(Actor);
		return PlayerState ? PlayerState->FindComponentByClass<TComponent>() : nullptr;
	}
}
