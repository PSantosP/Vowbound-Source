// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Engine/EngineBaseTypes.h"   // EMouseCaptureMode
#include "VBScreenWidget.generated.h"

class UButton;

/**
 * 이 화면이 맨 위에 있을 때 입력이 어디로 가나. Lyra ELyraWidgetInputMode 와 같은 값 넷이다.
 * Default = 입력 설정을 바꾸지 않는다(아래 화면의 설정이 그대로 산다).
 */
UENUM(BlueprintType)
enum class EVBScreenInputMode : uint8
{
	Default,
	Game,
	GameAndMenu,
	Menu,
};

/**
 * 모든 화면(HUD·일시정지·대화창·상점...)의 부모. 설계 .sw/ui-foundation/design.md 2.2~2.3.
 *
 * 화면은 입력과 정지를 «원하는 것» 만 데이터로 말한다. 실제로 바꾸는 주인은 따로 있다:
 *   입력 모드 = CommonUI 액션 라우터(맨 위 활성 화면의 GetDesiredInputConfig 가 이긴다),
 *   게임 정지 = UVBUIManagerSubsystem 의 정지 목록.
 * 그래서 화면 코드에서 SetInputMode·SetShowMouseCursor·SetGamePaused 를 부르지 않는다 -
 * 부르면 입력 모드의 주인이 둘이 되고 CommonUI 가 다음 화면 전환 때 조용히 덮어쓴다.
 *
 * 화면 위젯은 컨테이너가 재사용한다(엔진 CommonActivatableWidgetContainer 의 GeneratedWidgetsPool).
 * 그래서 보여줄 값은 NativeConstruct 가 아니라 NativeOnActivated 에서 매번 처음부터 채운다.
 */
UCLASS(Abstract, Blueprintable)
class VOWBOUND_API UVBScreenWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	bool PausesGame() const { return bPausesGame; }

protected:
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI|Screen")
	EVBScreenInputMode InputMode = EVBScreenInputMode::Default;

	// Game·GameAndMenu 일 때 마우스를 어떻게 잡나. Menu 는 이 값과 무관하게 잡지 않는다 -
	//   뷰포트가 마우스를 잡고 있으면 엔진이 Menu 모드에서도 게임 입력을 통과시킨다(CanProcessNormalGameInput).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI|Screen", meta=(EditCondition="InputMode == EVBScreenInputMode::Game || InputMode == EVBScreenInputMode::GameAndMenu"))
	EMouseCaptureMode GameMouseCaptureMode = EMouseCaptureMode::CapturePermanently;

	// 이 화면이 활성인 동안 게임을 멈추기를 원하나. 멈추고 푸는 것은 UI 관리자가 한다(여럿이 원해도 한 번만).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI|Screen")
	bool bPausesGame = false;

	// 화면이 켜질 때 게임패드 초점을 받을 위젯의 이름(WBP 위젯 트리 안). 비어 있으면 BP 의 Get Desired Focus Target 에 맡긴다.
	//   데이터로 둔 이유: 초점 대상은 화면마다 다르지만 «이름으로 찾는다» 는 방법은 같아서, 화면마다 함수를 짜면 같은 코드가 는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI|Screen")
	FName DefaultFocusWidgetName;

	// 화면 위 닫기 버튼(WBP 위젯 트리에서 이 이름). 누르면 NativeOnCloseClicked - 기본은 뒤로 가기와 같은 길이라 화면마다
	//   닫기를 다시 짜지 않는다(상점처럼 뒤로 가기를 재정의한 화면은 그 재정의가 닫는다).
	//   왜 필요한가: 폰(터치)에는 뒤로 가기 키가 없고, PIE 에서는 Esc 가 PIE 종료라 이 버튼이 화면을 벗어나는 유일한 길이다.
	//   상점·키 설정이 저마다 들던 BtnClose 를 2026-10-07 여기로 올렸다(가방·스킬 트리·일지가 셋째로 같은 것을 원해서).
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnClose;

	// 닫기 버튼을 눌렀을 때. 뒤로 가기와 닫기의 뜻이 다른 화면만 재정의한다(키 설정: 뒤로 가기 = 입력 대기 취소, 닫기 = 화면 닫기).
	virtual void NativeOnCloseClicked() { HandleBackAction(); }

	// 닫기 버튼이 반드시 있어야 하는 화면인가. 참이면 BtnClose 가 없거나 뒤로 가기를 안 받는 화면일 때 WBP 컴파일이 실패한다
	//   (ValidateCompiledWidgetTree). 선택 묶음만으로는 빠져도 조용히 넘어가 «닫을 길 없는 화면» 이 나간다.
	//   상점·키 설정은 생성자에서, 가방·스킬 트리·일지는 WBP 클래스 기본값에서 켠다(그 셋은 bIsBackHandler 도 WBP 데이터다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI|Screen")
	bool bRequireCloseButton = false;

	virtual void NativeOnInitialized() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	// 초점 대상을 네이티브로 정하는가. 아래 컴파일 검사가 읽는다. 엔진에는 «네이티브 재정의 여부» 를 묻는 길이 없어서
	//   이름을 데이터로 줬거나, C++ 파생이 NativeGetDesiredFocusTarget 을 따로 구현했으면 참을 돌려준다.
	virtual bool ProvidesNativeFocusTarget() const { return !DefaultFocusWidgetName.IsNone(); }

#if WITH_EDITOR
	// 메뉴 입력을 받는 화면이 초점 대상을 안 정하면 게임패드로 아무것도 못 고른다.
	//   공식 문서가 「strongly recommend that you always implement」 라고 하는 자리라 컴파일 때 경고한다(Lyra 와 같은 검사).
	virtual void ValidateCompiledWidgetTree(const UWidgetTree& BlueprintWidgetTree, class IWidgetCompilerLog& CompileLog) const override;
#endif

private:
	UFUNCTION()
	void HandleCloseClicked();
};
