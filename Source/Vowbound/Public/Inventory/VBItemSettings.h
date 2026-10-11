// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBItemSettings.generated.h"

class AVBWorldItem;

/**
 * 아이템이 세계에 놓일 때의 전역 설정. Project Settings > Game > Vowbound Items, 저장은 Config/DefaultGame.ini.
 * 지금 쓰는 곳 = 보상표 주기의 넘친 몫 떨구기(.sw/narrative 14.5 덧붙임 - 적 드롭이 가득 찬 인벤토리에 넘치면 그 자리에 놓는다).
 * 적 설정·보상표마다 적지 않는 이유: 같은 값이 그 수만큼 반복되고, 하나를 빠뜨리면 그 적의 넘친 몫만 조용히 버려진다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="Vowbound Items"))
class VOWBOUND_API UVBItemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 떨굴 때 만드는 줍기 액터(BP_WorldItem - 줍기 상호작용 설정이 BP 에 꽂혀 있다, AVBWorldItem 주석). 비면 넘친 몫을 버리고 경고한다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|Item")
	TSoftClassPtr<AVBWorldItem> WorldItemClass;

	// 한 자리에 여럿을 떨굴 때 서로 겹치지 않게 벌리는 반지름(cm). 첫 줄은 가운데, 다음 줄부터 이 반지름의 원 위에 돌아가며 놓는다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|Item", meta=(ClampMin="0"))
	float DropSpreadRadius = 40.0f;

	// 바닥을 찾는 아래 방향 추적 길이(cm, 떨군 액터의 위치 = 캡슐 가운데에서 시작). 바닥을 못 찾으면 그 위치 그대로 놓고 경고한다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|Item", meta=(ClampMin="0"))
	float DropGroundTraceDistance = 500.0f;
};
