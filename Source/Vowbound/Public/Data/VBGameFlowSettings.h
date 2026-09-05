// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBGameFlowSettings.generated.h"

/**
 * 게임 흐름의 프로젝트 전역 설정.
 * Project Settings > Game > Vowbound Game Flow 에 나타나고 Config/DefaultGame.ini 에 저장된다
 * (config=Game 이면 엔진 UDeveloperSettings::GetContainerName/GetCategoryName 이 그 자리를 스스로 정하므로
 *  오버라이드가 필요 없다 - 5.8 소스 확인).
 *
 * 왜 같은 폴더의 UVB*Config 들처럼 DataAsset 이 아닌가:
 *  *Config 는 소비자마다 다른 값을 꽂는 튜닝 자산이라 "소비자가 자산을 가리키는" 구조가 옳다.
 *  여기 값은 프로젝트에 하나뿐인 사실이라 소비자별 지정이 곧 결함이다. 실제로 GameplayLevel 은
 *  UVBMainMenuWidget 과 UVBSaveSlotListWidget 에 각각 있었고, 한쪽만 바꾸면 새 게임과 불러오기가
 *  서로 다른 맵으로 가는 구조였다(FIND-087). DataAsset 으로 옮겨도 "그 자산을 가리키는 포인터"가
 *  위젯 둘에 남아 같은 분기가 재발한다. DeveloperSettings 는 GetDefault<T>() 로 배선 없이 닿으므로
 *  가리킬 포인터 자체가 존재하지 않는다 - 그것이 이 타입을 고른 이유다.
 *
 * 접미사가 Config 가 아니라 Settings 인 이유: 에디터에서 꽂을 수 있는 자산과 꽂을 수 없는 프로젝트 설정을
 *  이름으로 구분한다(엔진 관용 UGameMapsSettings/URendererSettings 와 같은 결).
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="Vowbound Game Flow"))
class VOWBOUND_API UVBGameFlowSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 새 게임 시작 또는 세이브 로드 후 이동할 게임플레이 레벨.
	// 소비자는 UVBSaveGameSubsystem::TravelToGameplayLevel() 하나다 - 여기를 직접 읽어
	// OpenLevel 을 부르지 말 것. 그렇게 하면 미지정 시 경고가 또 갈라진다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|Game Flow")
	TSoftObjectPtr<UWorld> GameplayLevel;
};
