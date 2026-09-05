// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h" // FTimerHandle (게임오버 지연 타이머)
#include "GameFramework/PlayerController.h"
#include "VBPlayerController.generated.h"


class UVBAbilitySystemComponent;
class UVBHUDWidget;
class UVBPauseMenuWidget;
class UVBGameOverWidget;
class UInputAction;
/**
 * 입력의 주인 - 캐릭터와 분리된 입력 처리
 * 캐릭터가 죽어도 PlayerController는 살아있음
 */
UCLASS()
class VOWBOUND_API AVBPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	AVBPlayerController();
	
	// PlayerState의 ASC에 접근하는 헬퍼
	UVBAbilitySystemComponent* GetVBAbilitySystemComponent() const;

	// 일시정지 메뉴에서 위임받는 종료 경로(위젯 → PC). 입력모드 복원을 PC 단일 소스로 소유.
	void ResumeGame();       // unpause + 입력 GameOnly 복원 + 위젯 제거
	void QuitToMainMenu();   // unpause + 입력 복원 + OpenLevel(MainMenuLevel)

	// ── 사망 연출/게임오버 (FIND-062) ──
	// 소유 폰 사망 통지(로컬 전용 프레젠테이션 진입점). AVBCharacter::HandleDeathCosmetic 이 로컬 조종일 때만 부른다.
	// 슬로모 push + HUD 숨김 + 게임오버 화면 예약. 복제 없음 — 전부 로컬 프레젠테이션이라 새 RPC 0개.
	// 비가역 주의: 여기서 건 HUD Collapsed 와 폰의 DisableInput 은 이 흐름에서 되돌리지 않는다.
	//   두 출구(계속하기/메뉴로 가기)가 모두 OpenLevel 이라 월드째 새로 만들어지기 때문.
	//   같은 레벨 부활(체크포인트 리스폰 등)을 나중에 추가하면 EnableInput(PC) + HUD SetVisibility(Visible) 가 필요하다.
	// DeathAnimSeconds: 재생 중인 사망 클립 길이(게임초). 게임오버 화면은 이 시간 + 여유 뒤에 뜬다 —
	//   '실시간 고정 지연'이 아니라 '애니가 끝난 뒤'가 계약이라 슬로모 배율을 바꿔도 연출이 안 잘린다.
	void OnPlayerDeath(float DeathAnimSeconds);

	// 게임오버 "계속하기". 가장 최근 세이브를 비동기 로드한 뒤 레벨 재진입.
	// 세이브가 하나도 없거나 로드 실패면 현재 레벨을 처음부터 재시작한다(조용한 no-op 금지 — user 결정 2).
	void ContinueFromLastSave();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override; // PauseAction 바인드(게임플레이 입력은 폰이 소유, pause 만 PC)

	// PauseAction(Started) 핸들러 — 게임 정지 + 오버레이 + 입력 UIOnly.
	void OpenPauseMenu();

#if !UE_BUILD_SHIPPING
	// 디버그 즉사(K). 사망 연출을 반복 확인하기 위한 개발용이며 Shipping 에서는 컴파일되지 않는다.
	// 새 InputAction 에셋이나 IMC 행을 만들지 않고 raw BindKey 로 붙인다 — 디버그 편의를 위해
	//   Content 를 오염시키지 않기 위함이다(에셋이 생기면 SVN 에 남고 실수로 배포된다).
	// 데미지 GE 가 아니라 Health 를 직접 0 으로 만든다. 회피 무적이나 가드에 막히지 않아야
	//   사망 자체를 반복 시험할 수 있기 때문이다.
	void DebugKillSelf();
#endif

	// 슬로모 종료 타이머 콜백 — dilation 해제 → 게임오버 위젯 → pause → 입력 UIOnly + 커서.
	void ShowGameOverMenu();

	// "계속하기"의 성공/실패 두 경로가 모두 여기로 수렴 — 메뉴 상태 해제 후 현재 레벨 재진입.
	void RestartCurrentLevel();

	// 메뉴 상태 해제의 단일 소스: 위젯 2종 제거 → unpause → 입력 GameOnly + 커서 off.
	// 소비처 3곳(ResumeGame / QuitToMainMenu / RestartCurrentLevel). 종료 경로가 늘 때마다 이 순서를
	// 손으로 복사하다 한 군데를 빠뜨린 것이 과거 입력모드 누수의 원인이라, 유일 구현으로 못박는다.
	void TeardownMenuState();

	// 세이브 로드 완료 콜백(1회용 구독). 성공/실패 무관하게 RestartCurrentLevel 로 수렴.
	UFUNCTION()
	void HandleContinueLoadCompleted(int32 SlotIndex, bool bSuccess);

	// HUD 위젯 클래스 (에디터에서 WBP_HUD 할당)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBHUDWidget> HUDWidgetClass;

	// 생성된 HUD 인스턴스
	UPROPERTY()
	TObjectPtr<UVBHUDWidget> HUDWidget;

	// 일시정지 입력. IMC_KeyboardMouse/Gamepad 에 Esc/게임패드로 매핑(에디터).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> PauseAction;

	// 일시정지 오버레이 위젯 클래스(에디터에서 WBP_PauseMenu 할당).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBPauseMenuWidget> PauseMenuWidgetClass;

	// Quit to Menu 가 열 메인 메뉴 레벨. 소프트 참조(레벨 리네임 시 갱신).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSoftObjectPtr<UWorld> MainMenuLevel;

	// 생성된 일시정지 위젯 인스턴스(GC-only 내부 ref — Category 금지).
	UPROPERTY()
	TObjectPtr<UVBPauseMenuWidget> PauseMenuWidget;

	// 게임오버 오버레이 위젯 클래스(에디터에서 WBP_GameOver 할당). HUDWidgetClass/PauseMenuWidgetClass 와 동형.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBGameOverWidget> GameOverWidgetClass;

	// 사망 슬로모 전역 배율(1.0=정상). 반드시 UVBTimeDilationSubsystem 경유 — SetGlobalTimeDilation 직접 write 금지.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Death")
	float DeathSlowMoScale = 0.3f;

	// 슬로모 지속(실시간 초). PushTimed 가 자동 만료시키므로 여기서 시간축 변환을 하지 않는다.
	// 짧게 잡는 이유: 사망 클립 전체에 슬로모를 걸면(1.833초 × 1/0.35) 죽는 데만 5초가 넘는다.
	// 타격 순간만 강조하고 시간을 되돌린 뒤, 나머지는 정상 속도로 쓰러지게 한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Death")
	float DeathSlowMoRealDuration = 0.9f;

	// 사망 애니가 끝난 뒤 게임오버 화면까지의 여유(게임 초). 시체가 완전히 정착한 걸 보고 화면이 뜨게 한다.
	// '실시간 고정 지연'으로 되돌리지 말 것. 그러면 슬로모 배율/클립 길이가 바뀔 때마다 연출이 잘린다
	//   (2026-07-28 실제로 그랬다: 실시간 2.0초 고정 + 슬로모 0.3배 → 1.833초 클립이 33% 지점에서 얼어붙음).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Death")
	float DeathScreenExtraDelay = 0.5f;

	// 사망 애니 길이의 하한(게임 초). 사망 몽타주가 미할당이면 길이가 0으로 들어와 게임오버 화면이
	// 즉시 떠 버리므로, 그 경우에도 최소한의 정적이 남게 한다. 이웃 3개가 전부 UPROPERTY 인데
	// 이 하한만 코드 리터럴이었다(2026-08-09 데이터화).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Death")
	float MinDeathScreenDelay = 1.0f;

	// 생성된 게임오버 위젯 인스턴스(GC-only 내부 ref — Category 금지, PauseMenuWidget 과 동형).
	UPROPERTY()
	TObjectPtr<UVBGameOverWidget> GameOverWidget;

private:
	// 게임오버 진행 중인가. Esc(PauseAction)로 게임오버 위에 일시정지가 겹치는 것을 막는다.
	// 폰의 DisableInput 은 폰 바인딩만 죽이고 PC 자신의 PauseAction 은 살려두므로 이 플래그가 별도로 필요하다.
	bool bGameOverActive = false;

	// "계속하기" 비동기 로드 진행 중인가. 중복 클릭으로 로드가 2회 겹치는 것을 막는다.
	bool bContinueInProgress = false;

	// 슬로모 종료 → ShowGameOverMenu 타이머. UObject 참조가 아니라 UPROPERTY 불요.
	FTimerHandle GameOverTimerHandle;
};
