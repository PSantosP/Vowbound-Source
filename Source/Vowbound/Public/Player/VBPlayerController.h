// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h" // FTimerHandle (게임오버 지연 타이머)
#include "GameFramework/PlayerController.h"
#include "VBPlayerController.generated.h"


class UVBAbilitySystemComponent;
class UVBDialogueComponent;
class UVBShopComponent;
class UInputAction;
struct FGameplayTag;
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
	virtual void UpdateRotation(float DeltaTime) override;
	
	// PlayerState의 ASC에 접근하는 헬퍼
	UVBAbilitySystemComponent* GetVBAbilitySystemComponent() const;

	// 일시정지 메뉴를 열어 달라는 요청(HUD 의 Esc 바인딩이 부른다). 열어도 되는지 - 사망 연출 중이 아닌지 - 는 PC 가 안다.
	//   화면을 여는 것 자체와 정지·입력 모드는 UI 층이 한다(.sw/ui-foundation 3절).
	void RequestPauseMenu();

	// 인벤토리 화면을 열어 달라는 요청(HUD 의 인벤토리 바인딩). 일시정지와 같은 판단(사망 연출 중엔 안 연다).
	void RequestInventoryScreen();

	// 퀘스트 일지 화면을 열어 달라는 요청(HUD 의 일지 바인딩). 인벤토리와 같은 판단.
	void RequestJournalScreen();

	// 스킬 트리 화면을 열어 달라는 요청(HUD 의 스킬 트리 바인딩). 인벤토리와 같은 판단.
	void RequestSkillTreeScreen();

	// 일시정지·게임오버에서 위임받는 레벨 전환(위젯 → PC). 메뉴 화면을 내려 정지를 푼 뒤 OpenLevel(MainMenuLevel).
	void QuitToMainMenu();

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

	// 칸의 키 액션(HUD 칸 줄이 키 이름을 읽는다). 없으면 nullptr.
	const UInputAction* GetQuickSlotAction(int32 SlotIndex) const;
	int32 GetQuickSlotActionCount() const { return QuickSlotActions.Num(); }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override; // 퀵슬롯 키 + 디버그 키. 그 밖의 게임플레이 입력은 폰이, 일시정지 입력은 HUD 화면이 받는다.

#if !UE_BUILD_SHIPPING
	// 디버그 즉사(K). 사망 연출을 반복 확인하기 위한 개발용이며 Shipping 에서는 컴파일되지 않는다.
	// 새 InputAction 에셋이나 IMC 행을 만들지 않고 raw BindKey 로 붙인다 — 디버그 편의를 위해
	//   Content 를 오염시키지 않기 위함이다(에셋이 생기면 SVN 에 남고 실수로 배포된다).
	// 데미지 GE 가 아니라 Health 를 직접 0 으로 만든다. 회피 무적이나 가드에 막히지 않아야
	//   사망 자체를 반복 시험할 수 있기 때문이다.
	void DebugKillSelf();
#endif

	// 슬로모 종료 타이머 콜백 — dilation 해제 → 게임오버 화면 요청. 정지하지 않는다(user 결정 2026-07-28).
	void ShowGameOverMenu();

	// "계속하기"의 성공/실패 두 경로가 모두 여기로 수렴 — 메뉴 상태 해제 후 현재 레벨 재진입.
	void RestartCurrentLevel();

	// 레벨을 떠나기 전 메뉴 화면(일시정지·게임오버)을 내린다. 일시정지 화면이 내려가면 UI 층이 정지를 푼다 -
	//   정지 상태로 레벨을 옮기지 않는다. 소비처 둘(QuitToMainMenu / RestartCurrentLevel).
	void TeardownMenuState();

	// 세이브 로드 완료 콜백(1회용 구독). 성공/실패 무관하게 RestartCurrentLevel 로 수렴.
	UFUNCTION()
	void HandleContinueLoadCompleted(int32 SlotIndex, bool bSuccess);

	// Quit to Menu 가 열 메인 메뉴 레벨. 소프트 참조(레벨 리네임 시 갱신).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSoftObjectPtr<UWorld> MainMenuLevel;

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

	// 대화 진행의 주인(서버). 대화 화면은 이 PC 가 그 보기를 듣고 여닫는다. 설계 .sw/narrative 11.2절·13.1절.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Dialogue")
	TObjectPtr<UVBDialogueComponent> DialogueComponent;

	// 거래 진행의 주인(서버). 상점 화면도 이 PC 가 여닫는다. 설계 .sw/narrative 13.4절.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Shop")
	TObjectPtr<UVBShopComponent> ShopComponent;

	// 조종을 멈추지 않는 한 줄(혼잣말)을 서버에서 이 플레이어 화면으로 보낸다. 설계 .sw/prologue-opening 3.4.
	//   대화와 같은 이유로 PC 에 있다 - 소유 클라에만 있는 액터라 그 사람의 화면으로 가는 길이 된다. HUD 의 자막 위젯이 듣는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Dialogue")
	TObjectPtr<class UVBSpokenLineComponent> SpokenLineComponent;

	// 퀵슬롯 키(설계 .sw/quickslot-input 4.1절). 번호 = 칸(0 = 퀵슬롯 1). 칸보다 많은 액션은 묶지 않고 경고한다.
	//   폰이 아니라 PC 가 받는 이유: 칸의 주인(인벤토리)이 PlayerState 에 있어 폰과 수명이 다르고, 화면 열기가 아니라서 HUD 의 CommonUI 바인딩 자리도 아니다.
	//   HUD 칸 줄은 이 목록에서 칸의 키 이름을 읽는다(GetQuickSlotAction).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TArray<TObjectPtr<UInputAction>> QuickSlotActions;

private:
	void HandleQuickSlotPressed(int32 SlotIndex);

	// 주인 클라: 대화·상점 컴포넌트의 보기가 바뀌면 그 화면을 맞춘다. 화면 태그를 아는 게임 쪽 층이 여기 하나다 -
	//   컴포넌트(모델)가 UI 관리자를 부르면 모델이 뷰를 알게 된다(구조 조사 2026-10-01 2.3절).
	void HandleDialogueViewChanged();
	void HandleShopViewChanged();
	void SyncScreen(const FGameplayTag& ScreenTag, bool bWanted);

	// 사망 연출이 시작됐는가. 사망부터 게임오버 화면이 뜨기까지(애니 길이 + 여유) 일시정지가 열리지 않게 한다.
	//   게임오버 화면이 뜬 뒤에는 그 화면이 맨 위라 HUD 의 Esc 바인딩에 입력이 안 가지만, 그 전 구간은
	//   HUD 가 맨 위다. 일시정지 입력은 PC 의 InputComponent 가 아니라 HUD 화면이 받으므로 DisableInput 이 못 막는다.
	bool bGameOverActive = false;

	// "계속하기" 비동기 로드 진행 중인가. 중복 클릭으로 로드가 2회 겹치는 것을 막는다.
	bool bContinueInProgress = false;

	// 슬로모 종료 → ShowGameOverMenu 타이머. UObject 참조가 아니라 UPROPERTY 불요.
	FTimerHandle GameOverTimerHandle;
};
