// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "VBMenuGameMode.generated.h"

/**
 * 메인 메뉴 레벨 전용 GameMode.
 * 이유: 메뉴 화면엔 플레이어 폰/HUD/GAS 가 필요 없다. 메뉴 전용 PC 만 스폰하고
 *       기본 폰 스폰을 끈다(DefaultPawnClass=nullptr) — 불필요한 액터/입력 충돌 방지.
 */
UCLASS()
class VOWBOUND_API AVBMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AVBMenuGameMode();
};
