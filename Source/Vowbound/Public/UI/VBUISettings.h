// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "VBUISettings.generated.h"

class UVBPrimaryLayout;
class UVBScreenWidget;

/** 화면 태그 하나가 어느 층에 어떤 위젯으로 뜨는가. */
USTRUCT(BlueprintType)
struct FVBScreenRoute
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Vowbound|UI", meta=(Categories="UI.Layer"))
	FGameplayTag Layer;

	UPROPERTY(EditAnywhere, Category="Vowbound|UI")
	TSoftClassPtr<UVBScreenWidget> ScreenClass;
};

/**
 * UI 층의 프로젝트 전역 설정. Project Settings > Game > Vowbound UI, Config/DefaultGame.ini.
 * 설계 .sw/ui-foundation/design.md 2.1.
 *
 * 화면을 여는 쪽은 위젯 클래스를 모르고 화면 태그(UI.Screen.*)만 안다. 태그 -> 층·클래스 표가 여기 한 곳이다.
 *   이유: 게임플레이 코드가 위젯 클래스를 직접 쥐면 UI 를 바꿀 때 게임플레이를 고쳐야 한다
 *   (Lyra UI 구조에 대한 가장 흔한 비판 - 설계 8절 조사 기록).
 * DeveloperSettings 인 이유는 UVBGameFlowSettings 와 같다 - 프로젝트에 하나뿐인 사실이라 가리킬 포인터가 없어야 한다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="Vowbound UI"))
class VOWBOUND_API UVBUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 로컬 플레이어마다 뷰포트에 하나 올라가는 뿌리 위젯(WBP_PrimaryLayout). 층 넷을 담는다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|UI")
	TSoftClassPtr<UVBPrimaryLayout> PrimaryLayoutClass;

	// 화면 태그 -> 층·위젯. UVBUIManagerSubsystem::PushScreen 이 읽는 유일한 표다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|UI", meta=(Categories="UI.Screen", ForceInlineRow))
	TMap<FGameplayTag, FVBScreenRoute> Screens;

	// UI 조작(확인·뒤로) 키를 담은 IMC. CommonUI 는 UI 액션의 키를 활성 IMC 에서 찾으므로 늘 붙어 있어야 한다.
	//   붙이는 주인은 UI 관리자다 - 게임플레이 IMC 를 갈아 끼우는 UVBInputDeviceManager 는 캐릭터에 붙어 있어
	//   캐릭터가 없는 메인 메뉴에서는 UI 조작이 죽는다. 장치별로 나누지 않고 키보드·패드 키를 한 IMC 에 담는다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|UI|Input")
	TSoftObjectPtr<class UInputMappingContext> UIInputMapping;

	// 게임플레이 IMC(0)보다 높게 둔다 - 같은 키가 겹쳐도 UI 액션의 키 조회가 이 IMC 를 먼저 본다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|UI|Input")
	int32 UIInputMappingPriority = 10;

	// 알림 줄을 그리는 위젯(WBP_NoticeFeed). 뿌리 위젯이 층 넷 위에 겹으로 붙인다 - 메뉴 위에도 보이고 입력을 안 받는다.
	//   비면 알림이 안 보인다(경고). 설계 .sw/remaining-features/a2-notices.md.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|UI|Notice")
	TSoftClassPtr<class UVBNoticeFeedWidget> NoticeFeedClass;

	// 알림 한 줄이 떠 있는 시간(실시간 초). 같은 줄에 합쳐지면 다시 잰다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|UI|Notice", meta=(ClampMin="0.5", Units="s"))
	float NoticeSeconds = 3.0f;

	// 한꺼번에 떠 있는 줄의 최대 수. 넘으면 가장 오래된 줄이 빠진다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|UI|Notice", meta=(ClampMin="1"))
	int32 MaxNotices = 4;
};
