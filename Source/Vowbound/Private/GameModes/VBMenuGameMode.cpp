// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "GameModes/VBMenuGameMode.h"

#include "Player/VBMenuPlayerController.h"

AVBMenuGameMode::AVBMenuGameMode()
{
	// 메뉴 전용 PC 를 기본값으로. BP 에서 override 가능.
	PlayerControllerClass = AVBMenuPlayerController::StaticClass();

	// 메뉴엔 조작할 폰/HUD 가 없다.
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
}
