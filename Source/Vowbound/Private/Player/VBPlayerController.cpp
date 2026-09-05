// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Player/VBPlayerController.h"

#include "Player/VBPlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Character/VBCharacter.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "TimeControl/VBTimeDilationSubsystem.h"
#include "UI/VBGameOverWidget.h"
#include "UI/VBHUDWidget.h"
#include "UI/VBPauseMenuWidget.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 사망 슬로모의 dilation 요청 핸들. Push 와 Pop 이 반드시 같은 이름을 써야 짝이 맞으므로
	// 문자열 리터럴을 두 곳에 흩뿌리지 않고 한 곳에 고정한다(오타 = 슬로모가 영영 안 풀리는 버그).
	const FName GDeathSlowMoHandle = TEXT("PlayerDeath");
}

AVBPlayerController::AVBPlayerController()
{
	// 기본 설정
}


void AVBPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 게임플레이 입력 모드를 명시적으로 확정한다.
	// 왜: 메인 메뉴 PC 가 FInputModeUIOnly + 마우스 커서를 켜면 그 UI 마우스 캡처 상태가
	//     GameViewportClient(레벨 전환에도 파괴되지 않고 유지됨)에 남는다. New Game/Continue 로
	//     이 게임플레이 레벨에 들어온 새 PC 가 재설정하지 않으면 마우스 시점(캡처 필요)과
	//     클릭(게임이 아닌 UI로 전달)이 모두 죽는다. 직접 PIE 진입은 이미 GameOnly 라 idempotent.
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}

	// 로컬 플레이어만 HUD 생성
	if (IsLocalController() && HUDWidgetClass)
	{
		HUDWidget = CreateWidget<UVBHUDWidget>(this, HUDWidgetClass);
		
		if (HUDWidgetClass)
		{
			// 뷰포트에 추가해야 화면에 보인다.
			HUDWidget->AddToViewport();
			
			// Possess된 캐릭터가 있으면 초기화
			if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetPawn()))
			{
				HUDWidget->InitializeHUD(VBChar);
			}
		}
	}
}

UVBAbilitySystemComponent* AVBPlayerController::GetVBAbilitySystemComponent() const
{
	// PlayerState에서 ASC를 가져옴
	if (const AVBPlayerState* PS = GetPlayerState<AVBPlayerState>())
	{
		return PS->GetVBAbilitySystemComponent();
	}

	return nullptr;
}

void AVBPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// 게임플레이 입력은 폰(VBCharacter)이 바인드한다. PC 는 pause 만 소유 — 폰 사망과 무관하게 유지.
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (PauseAction)
		{
			EIC->BindAction(PauseAction, ETriggerEvent::Started, this, &AVBPlayerController::OpenPauseMenu);
		}
	}

#if !UE_BUILD_SHIPPING
	// 디버그 즉사 키. Enhanced Input 이 아니라 raw BindKey 라 IMC 와 무관하게 항상 먹는다.
	// 사망 후에는 OnPlayerDeath 의 DisableInput(this) 가 이 바인딩까지 걷어가므로 시체 상태에서는 안 눌린다.
	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::K, IE_Pressed, this, &AVBPlayerController::DebugKillSelf);
	}
#endif
}

void AVBPlayerController::OpenPauseMenu()
{
	if (!IsLocalController())
	{
		return;
	}
	if (bGameOverActive)
	{
		// 게임오버 연출/화면 위에 일시정지가 겹치지 않게 한다. 폰의 DisableInput 은 폰 바인딩만 죽이고
		// PC 자신의 PauseAction 은 살아남으므로, 이 가드가 없으면 죽은 뒤에도 Esc 가 먹힌다.
		return;
	}
	if (PauseMenuWidget)
	{
		return; // 이미 열림 — 중복 방지.
	}
	if (!PauseMenuWidgetClass)
	{
		VB_LOG(Warning, "[VBPC] PauseMenuWidgetClass 미할당 — 일시정지 메뉴가 뜨지 않습니다.");
		return;
	}

	// SP/host 전용 경계: APlayerController::SetPause 는 NM_Client 에서 조기 반환한다(엔진 확인).
	// MP 로 확장하면 리모트 클라는 "메뉴는 떴는데 월드는 계속 도는" 상태가 되므로, 그때는
	// 정지 자체를 서버 권위 경로로 옮겨야 한다(현 범위=SP/host, SystemMap 15_PauseMenu).
	UGameplayStatics::SetGamePaused(this, true);

	PauseMenuWidget = CreateWidget<UVBPauseMenuWidget>(this, PauseMenuWidgetClass);
	if (!PauseMenuWidget)
	{
		UGameplayStatics::SetGamePaused(this, false); // 위젯 실패면 정지 되돌림.
		return;
	}
	PauseMenuWidget->AddToViewport();

	// 게임 입력 정지, UI 로 전환 + 커서. 위젯 버튼으로 조작(paused 중 Enhanced Input 미발화).
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(PauseMenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void AVBPlayerController::TeardownMenuState()
{
	// 메뉴 상태 해제의 단일 소스. 소비처 3곳(ResumeGame / QuitToMainMenu / RestartCurrentLevel).
	// 순서가 계약이다: 위젯 제거 → unpause → 입력모드 GameOnly + 커서 off.
	//   ① 위젯을 먼저 지워야 "정지 해제됐는데 오버레이만 남은" 중간 상태가 안 생긴다.
	//   ② 입력모드 복원을 마지막에 둬야 UI 캡처가 확실히 걷힌다 — 안 하면 시점/클릭이 죽는다(입력모드 누수 교훈).
	// 위젯 수명은 PC 가 모든 종료 경로에서 대칭으로 정리한다. OpenLevel 이 월드째 파괴하므로 기능상 필수는
	// 아니지만, 비대칭을 남기면 "레벨 전환 없는 종료 경로"가 추가될 때 조용히 새는 지점이 된다.
	if (PauseMenuWidget)
	{
		PauseMenuWidget->RemoveFromParent();
		PauseMenuWidget = nullptr;
	}
	if (GameOverWidget)
	{
		GameOverWidget->RemoveFromParent();
		GameOverWidget = nullptr;
	}

	UGameplayStatics::SetGamePaused(this, false);

	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void AVBPlayerController::ResumeGame()
{
	TeardownMenuState();
}

void AVBPlayerController::QuitToMainMenu()
{
	// unpause(정지 상태로 레벨 전환 금지) + 위젯 정리 + 방어적 입력모드 복원.
	// 새 메뉴 PC 가 UIOnly 를 재확정하지만, 전환 전 GameOnly 로 되돌려 캡처 잔류를 막는다.
	TeardownMenuState();

	if (!MainMenuLevel.IsNull())
	{
		// SP/host 전용: OpenLevel 은 클라에서도 그대로 실행되어(SetClientTravel) 세션을
		// 조용히 이탈시킨다. MP 확장 시엔 여기서 권위 체크 후 정식 디스커넥트로 바꿔야 한다.
		UGameplayStatics::OpenLevelBySoftObjectPtr(this, MainMenuLevel);
	}
	else
	{
		VB_LOG(Warning, "[VBPC] MainMenuLevel 미할당 — 메뉴 복귀 불가.");
	}
}

// ──────────────────────────────────────────────────────────────────────────────
// 사망 연출 / 게임오버 (FIND-062)
// ──────────────────────────────────────────────────────────────────────────────

void AVBPlayerController::OnPlayerDeath(float DeathAnimSeconds)
{
	// 로컬 프레젠테이션 전용 — 호출자(AVBCharacter::HandleDeathCosmetic)가 IsLocallyControlled 로 이미 걸렀지만
	// 진입점 계약을 자기 자신이 보증하도록 이중화한다(전용 데디 서버에서 오발화 시 무해한 조기 반환).
	if (!IsLocalController())
	{
		return;
	}
	if (bGameOverActive)
	{
		return; // 같은 프레임 다중 킬링 GE 등으로 사망 통지가 겹쳐도 연출은 1회만.
	}
	bGameOverActive = true;

	// PC 자신의 입력도 끊는다. 폰의 DisableInput 은 폰이 바인드한 것만 죽이고 PC 가 바인드한 액션
	// (현재 PauseAction)은 살아남기 때문이다.
	// 왜 bGameOverActive 가드만으로 부족한가: 그 방식은 "PC 에 새 액션을 추가하는 사람이 매번 가드를
	// 기억해야" 성립하는 규율 의존형 방어다. DisableInput 은 InputComponent 를 스택에서 통째로 pop 하므로
	// 앞으로 어떤 액션이 추가돼도 자동으로 막힌다. bGameOverActive 는 이중화로 남긴다.
	// 부작용 없음: 게임오버 위젯 버튼은 Slate 경로로 동작하므로 InputComponent 와 무관하다.
	// 비가역이어도 무해한 이유는 아래 HUD 주석과 같다 - 두 출구가 모두 OpenLevel 이다.
	DisableInput(this);

	// HUD 는 Collapsed 로 감춘다. RemoveFromParent 가 아니다. 근거 4중:
	//   ① PC 가 HUDWidget 을 직접 소유하므로 비용 1줄.
	//   ② Collapsed 는 레이아웃뿐 아니라 히트테스트에서도 제거된다 → HP 0 짜리 바가 게임오버 버튼 클릭을
	//      가로챌 여지가 원천 차단된다.
	//   ③ Collapsed 위젯은 tick 되지 않는다 → UVBHUDWidget::NativeTick(락온 마커 화면 투영)이 죽은
	//      플레이어에게 매 프레임 계속 도는 낭비가 자동으로 사라진다.
	//      엔진 근거(2026-07-29 확인): NativeTick 은 SObjectWidget::Tick(SObjectWidget.cpp::SObjectWidget::Tick)에서만
	//      불리고, 그건 SWidget::Paint(SWidget.cpp) 안에서만 불린다. Paint 는 arranged
	//      children 을 따라가는데 FArrangedChildren::Accepts 가 가시성으로 거르고 Prepass 는
	//      EVisibility::Collapsed 를 명시적으로 배제한다(SWidget.cpp::SWidget::Paint) → 배열 안 됨 = Paint 안 됨 = Tick 안 됨.
	//   ④ 가역적 — 같은 레벨 부활을 나중에 붙일 때 Visible 한 줄로 되돌린다(RemoveFromParent 는 비가역).
	if (HUDWidget)
	{
		HUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 사망 직후 짧은 슬로모 한 박자. PushTimed(자동 만료)를 쓴다 — 서브시스템이 실시간 만료를 스스로
	// 계산하므로 여기서 시간축 변환 수식을 복제할 필요가 없다(이전 구현의 R12 중복이 이 선택으로 사라졌다).
	// Scale>0 전제는 PushTimed 가 내부에서 floor 로 보증한다(VBTimeDilationSubsystem.cpp:64).
	if (UVBTimeDilationSubsystem* TimeDilation = UVBTimeDilationSubsystem::Get(this))
	{
		TimeDilation->PushTimed(GDeathSlowMoHandle, DeathSlowMoScale, DeathSlowMoRealDuration);
	}

	// 게임오버 화면은 '실시간 N초 뒤'가 아니라 '사망 애니가 끝난 뒤'에 뜬다.
	// 왜 바뀌었나(2026-07-28): 종전엔 실시간 2.0초 고정이었는데, 사망 클립이 1.833초이고 슬로모 0.3배가
	//   걸리면 애니가 실시간 6.1초 걸린다 → 화면이 33% 지점에 떠서 pause 가 쓰러지다 만 자세를 얼린다.
	// SetTimer 는 게임시간 누적이고 애니메이션도 게임시간으로 전진하므로, 지연을 게임초로 주면
	//   dilation 이 얼마든 "애니 종료 + 여유 한 박자" 시점이 정확히 유지된다 — 변환 수식이 아예 불필요하다.
	// 폴백: 클립 미할당(길이 0)이면 시체가 선 채로 굳으므로 최소 1초는 보여주고 화면을 띄운다.
	const float AnimSeconds = FMath::Max(DeathAnimSeconds, MinDeathScreenDelay);
	GetWorldTimerManager().SetTimer(
		GameOverTimerHandle,
		this,
		&AVBPlayerController::ShowGameOverMenu,
		AnimSeconds + DeathScreenExtraDelay,
		false);
}

void AVBPlayerController::ShowGameOverMenu()
{
	if (!IsLocalController())
	{
		return;
	}

	// 슬로모는 PushTimed 가 이미 자동 만료시켰을 것이다(지속 < 화면 지연). Pop 은 그 가정이 깨졌을 때를 위한
	// 안전망 — 이미 만료된 handle 이면 무해한 no-op 이고, 살아 있으면 pause 중 dilation 이 stale 하게 남는 것을 막는다.
	if (UVBTimeDilationSubsystem* TimeDilation = UVBTimeDilationSubsystem::Get(this))
	{
		TimeDilation->Pop(GDeathSlowMoHandle);
	}

	if (GameOverWidget)
	{
		return; // 이미 열림 — 중복 방지(OpenPauseMenu 동형).
	}
	if (!GameOverWidgetClass)
	{
		// 조용한 실패 금지: 화면이 없으면 조작 불능인 시체만 남으므로 원인을 로그로 남긴다.
		VB_LOG(Warning, "[VBPC] GameOverWidgetClass 미할당 — 게임오버 화면이 뜨지 않습니다.");
		return;
	}

	GameOverWidget = CreateWidget<UVBGameOverWidget>(this, GameOverWidgetClass);
	if (!GameOverWidget)
	{
		VB_LOG(Warning, "[VBPC] 게임오버 위젯 생성 실패 — 정지/입력모드 전환을 건너뜁니다.");
		return;
	}
	GameOverWidget->AddToViewport();

	// UUserWidget 은 기본이 non-focusable 이라, 이 세터가 없으면 아래 SetWidgetToFocus 가 실패한다.
	// (실측 로그: "InputMode:UIOnly - Attempting to focus Non-Focusable widget SObjectWidget").
	// 위젯 에셋의 IsFocusable 체크에 의존하지 않고 코드에서 보장한다 — WBP 를 새로 만들 때마다 같은 함정을
	// 밟지 않게 하기 위함. UE5.2 부터 bIsFocusable 직접 접근이 deprecated 라 세터를 쓴다(UserWidget.h::UUserWidget::SetIsFocusable).
	GameOverWidget->SetIsFocusable(true);

	// 게임을 멈추지 않는다 (2026-07-28 user 결정: "타임 안멈춰도 돼 그냥 진행해도돼").
	// 일시정지 메뉴와의 결정적 차이다. 그쪽은 '플레이어가 자리를 비우는' 상황이라 정지가 계약이지만,
	// 게임오버는 월드가 계속 돌아가는 편이 연출로 낫고 시체·적·이펙트가 얼어붙지 않는다.
	// 그래서 입력 차단을 pause 에 의존할 수 없고 코드가 직접 든다. 방어선 3중:
	//   ① 폰의 DisableInput — 폰이 바인드한 Enhanced Input 전량 (AVBCharacter::HandleDeathCosmetic)
	//   ② PC 자신의 DisableInput(this) — PC 가 바인드한 액션 전량, 앞으로 추가될 것 포함 (OnPlayerDeath)
	//   ③ bGameOverActive — OpenPauseMenu 조기 반환(이중화)
	//   ②가 없던 시절엔 ③이 OpenPauseMenu 한 곳만 막아, PC 에 액션을 추가하는 사람이 매번 가드를
	//   기억해야 성립하는 규율 의존형 방어였다(FIND-071).
	// TeardownMenuState 의 unpause 는 그대로 둔다 — 멈춘 적 없으면 무해한 no-op 이고, 일시정지 메뉴 경로와
	//   해제 절차를 하나로 유지하는 편이 안전하다.

	// 버튼 조작만 남긴다(마우스 커서 + 클릭).
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(GameOverWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void AVBPlayerController::ContinueFromLastSave()
{
	if (bContinueInProgress)
	{
		return; // 중복 클릭으로 비동기 로드가 2회 겹치는 것 방지.
	}

	UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this);
	if (!Subsystem)
	{
		VB_LOG(Warning, "[VBPC] 세이브 서브시스템 없음 — 현재 레벨 재시작으로 폴백합니다.");
		RestartCurrentLevel();
		return;
	}

	// 정책은 서브시스템이 소유한다. 메인메뉴 Continue 와 반드시 같은 슬롯을 가리켜야 한다.
	const int32 RecentSlot = Subsystem->GetMostRecentSlotIndex();
	if (RecentSlot < 0)
	{
		// 세이브가 하나도 없으면 현재 레벨을 처음부터 시작한다(user 결정 2). 조용한 no-op 금지.
		// OpenLevel(non-seamless)이 PlayerState 를 재생성 → bStartupEffectsApplied 래치 초기화 →
		// 새 폰의 PossessedBy 가 GE_InitStats 를 재적용하므로 HP 는 만재로 복귀한다.
		VB_LOG(Log, "[VBPC] 세이브 없음 — 현재 레벨을 처음부터 재시작합니다.");
		RestartCurrentLevel();
		return;
	}

	// 1회용 구독 후 비동기 로드. pause 중이어도 완료 콜백은 발화한다 —
	// AsyncLoadGameFromSlot 의 게임스레드 복귀가 FTSTicker(코어 티커)라 월드 pause 와 무관하기 때문.
	bContinueInProgress = true;
	Subsystem->OnGameLoadCompleted.AddDynamic(this, &AVBPlayerController::HandleContinueLoadCompleted);
	Subsystem->LoadGameSlot(RecentSlot);
}

void AVBPlayerController::HandleContinueLoadCompleted(int32 SlotIndex, bool bSuccess)
{
	if (UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this))
	{
		Subsystem->OnGameLoadCompleted.RemoveDynamic(this, &AVBPlayerController::HandleContinueLoadCompleted);
	}
	bContinueInProgress = false;

	if (!bSuccess)
	{
		// 실패해도 화면에 갇히지 않는다 — 재시작으로 수렴(조용한 no-op 금지).
		VB_LOG(Warning, "[VBPC] 세이브 슬롯 %d 로드 실패 — 현재 레벨을 처음부터 재시작합니다.", SlotIndex);
	}

	// 성공/실패 두 경로가 같은 곳으로 수렴한다. 성공이면 서브시스템의 bPendingContinueApply 가 서 있어
	// 새 레벨의 AVBGameMode::HandleStartingNewPlayer 가 ApplyBuild + BeginRestore 를 수행한다(추가 코드 0).
	RestartCurrentLevel();
}

void AVBPlayerController::RestartCurrentLevel()
{
	// 정지 상태로 레벨 전환 금지 + 입력모드 복원(QuitToMainMenu 와 동일 계약).
	TeardownMenuState();

	// 대상 레벨을 세이브의 LastLevelName 이 아니라 현재 레벨명으로 잡는 이유:
	//   FVBProgressSaveData::LastLevelName 은 필드만 있고 기록 코드가 0줄이라 항상 None — 신뢰 불가.
	//   GetCurrentLevelName(bRemovePrefixString=true) 은 PIE 접두어(UEDPIE_0_)를 제거해 주므로 에디터/패키지 동형.
	const FString LevelName = UGameplayStatics::GetCurrentLevelName(this, /*bRemovePrefixString=*/true);
	if (LevelName.IsEmpty())
	{
		VB_LOG(Warning, "[VBPC] 현재 레벨명 해석 실패 — 레벨 재진입 불가.");
		return;
	}

	UGameplayStatics::OpenLevel(this, FName(*LevelName));
}

#if !UE_BUILD_SHIPPING
void AVBPlayerController::DebugKillSelf()
{
	AVBCharacter* VBChar = Cast<AVBCharacter>(GetPawn());
	if (!VBChar)
	{
		VB_LOG(Warning, "[DEBUG] 즉사 키: 조종 중인 VBCharacter 가 없습니다.");
		return;
	}

	// 어트리뷰트는 서버 권위다. 클라에서 눌러도 다음 복제에 덮여 무의미하므로 조용히 넘기지 않고 알린다.
	if (!VBChar->HasAuthority())
	{
		VB_LOG(Warning, "[DEBUG] 즉사 키: 권위 없음(클라이언트) — 무시합니다.");
		return;
	}

	UAbilitySystemComponent* ASC = VBChar->GetAbilitySystemComponent();
	if (!ASC)
	{
		VB_LOG(Warning, "[DEBUG] 즉사 키: ASC 가 없습니다.");
		return;
	}

	// Override 로 Health 를 0 으로. 이 경로가 사망 델리게이트를 실제로 태우는 것은 엔진에서 확인했다:
	// ApplyModToAttribute -> SetAttributeBaseValue -> InternalUpdateNumericalAttribute(GameplayEffect.cpp::FActiveGameplayEffectsContainer::InternalUpdateNumericalAttribute)
	// -> AttributeValueChangeDelegates 브로드캐스트(:3912) -> AVBCharacter::OnHealthChanged -> HandleDeath.
	ASC->ApplyModToAttribute(UVBHealthAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, 0.0f);
	VB_LOG(Warning, "[DEBUG] 즉사 키 — Health 0 적용: %s", *VBChar->GetName());
}
#endif
