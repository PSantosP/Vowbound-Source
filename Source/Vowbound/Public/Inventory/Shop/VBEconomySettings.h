// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBEconomySettings.generated.h"

class UVBEconomyConfig;

/**
 * 경제 설정 자산을 가리키는 프로젝트 전역 설정. Project Settings > Game > Vowbound Economy, 저장은 Config/DefaultGame.ini.
 * 「무엇이 돈인가」의 답은 프로젝트에 하나뿐이라 DeveloperSettings 다(reuse-catalog §6) - 상점마다 가리키게 하면 하나만 다른 돈을 가리키는 사고가 가능하다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="Vowbound Economy"))
class VOWBOUND_API UVBEconomySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 비면 상점이 열리지 않는다 - 돈을 모르면 거래를 판정할 수 없다(조용히 공짜로 떨어지지 않게).
	UPROPERTY(config, EditAnywhere, Category="Vowbound|Economy")
	TSoftObjectPtr<UVBEconomyConfig> Economy;

	// 경제 자산을 읽는 유일한 길. 비었거나 못 읽으면 nullptr(거래 쪽은 경고, 자산 검증은 조용히 - 검증은 설정이 없는 자리에서도 돈다).
	// 처음 쓸 때 동기 로드한다(상점을 열 때 - 작은 자산이다).
	static const UVBEconomyConfig* LoadEconomy(bool bWarnIfMissing = true);
};
