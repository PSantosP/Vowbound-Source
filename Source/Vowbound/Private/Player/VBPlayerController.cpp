// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Player/VBPlayerController.h"
#include "Camera/VBPlayerCameraManager.h"

#include "Player/VBPlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBTargetLockComponent.h"
#include "Narrative/Dialogue/VBDialogueComponent.h"
#include "Narrative/Dialogue/VBSpokenLineComponent.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "TimeControl/VBTimeDilationSubsystem.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "UI/VBUIManagerSubsystem.h"
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
	PlayerCameraManagerClass = AVBPlayerCameraManager::StaticClass();
	DialogueComponent = CreateDefaultSubobject<UVBDialogueComponent>(TEXT("Dialogue"));
	ShopComponent = CreateDefaultSubobject<UVBShopComponent>(TEXT("Shop"));
	SpokenLineComponent = CreateDefaultSubobject<UVBSpokenLineComponent>(TEXT("SpokenLine"));
}


void AVBPlayerController::UpdateRotation(float DeltaTime)
{
	// 락온 입력은 이미 오프셋으로 수집됐다. 엔진의 pitch 제한/FaceRotation 전에 합성한다.
	if (IsLocalController())
	{
		if (const AVBCharacter* VBCharacter = Cast<AVBCharacter>(GetPawn()))
		{
			if (UVBTargetLockComponent* Lock = VBCharacter->GetTargetLockComponent())
			{
				if (Lock->CanTrackCamera())
				{
					RotationInput = FRotator::ZeroRotator;
					Lock->UpdateCameraRotation(DeltaTime);
				}
				else
				{
					// 다른 시점 입력 경로도 우선하며 자유 시점의 RotationInput은 지우지 않는다.
					Lock->NotifyManualCameraInput(RotationInput.Yaw, RotationInput.Pitch);
					Lock->UpdateHitRecenter();
				}
			}
		}
	}
	Super::UpdateRotation(DeltaTime);
}

void AVBPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// HUD 화면을 연다. 입력 모드(게임, 마우스 캡처)는 HUD 화면의 설정을 CommonUI 가 적용한다.
	//   예전에는 여기서 SetInputMode(GameOnly) 로 메인 메뉴가 남긴 UI 캡처를 걷어냈다(GameViewportClient 는 레벨 전환에도
	//   살아남는다). 지금은 HUD 가 맨 위가 되는 순간 CommonUI 가 캡처를 다시 잡는다 - 같은 일을 하는 주인이 하나다.
	//   HUD 의 캐릭터 연결은 HUD 가 스스로 한다(빙의가 늦으면 기다린다) - 로드가 비동기라 여기서 넘길 수 없다.
	if (IsLocalController())
	{
		if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
		{
			UI->PushScreen(VBGameplayTags::UI_Screen_HUD);
		}
		else
		{
			VB_LOG(Warning, "[VBPC] UI 관리자를 못 찾았다 - HUD 가 안 뜬다");
		}
		// 대화·상점 화면은 그 컴포넌트가 아니라 여기서 연다 - 컴포넌트는 보기 바뀜만 알린다(설계 .sw/narrative 13.1절 1번).
		DialogueComponent->OnViewChanged.AddUObject(this, &AVBPlayerController::HandleDialogueViewChanged);
		ShopComponent->OnViewChanged.AddUObject(this, &AVBPlayerController::HandleShopViewChanged);
	}
}

void AVBPlayerController::HandleDialogueViewChanged()
{
	SyncScreen(VBGameplayTags::UI_Screen_Dialogue, DialogueComponent->GetView().IsActive());
}

void AVBPlayerController::HandleShopViewChanged()
{
	SyncScreen(VBGameplayTags::UI_Screen_Shop, ShopComponent->GetView().IsActive());
}

void AVBPlayerController::SyncScreen(const FGameplayTag& ScreenTag, bool bWanted)
{
	// 이미 떠 있으면 PushScreen 이, 안 떠 있으면 PopScreen 이 아무것도 안 한다 - 보기가 여러 번 바뀌어도 한 줄로 맞춘다.
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		if (bWanted)
		{
			UI->PushScreen(ScreenTag);
		}
		else
		{
			UI->PopScreen(ScreenTag);
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

	// 게임플레이 입력은 폰(VBCharacter)이 바인드한다 - 퀵슬롯 키만 여기다(헤더 QuickSlotActions). 일시정지 입력은 HUD 화면이 CommonUI 로 받는다(RequestPauseMenu).
	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(this);
		for (int32 SlotIndex = 0; SlotIndex < QuickSlotActions.Num(); ++SlotIndex)
		{
			if (QuickSlotActions[SlotIndex])
			{
				EnhancedInput->BindAction(QuickSlotActions[SlotIndex], ETriggerEvent::Started, this, &AVBPlayerController::HandleQuickSlotPressed, SlotIndex);
			}
		}
		// 칸 수는 인벤토리가 정한다. PlayerState 가 아직 없으면(SetupInputComponent 는 빙의보다 먼저 돌 수 있다) 견주지 못한다 - 누를 때 서버가 범위를 다시 본다.
		VB_CLOG(Inventory && QuickSlotActions.Num() != Inventory->GetQuickSlots().Num(), Warning,
			"PlayerController: 퀵슬롯 키 %d 개와 칸 %d 개가 다르다 - 키 없는 칸, 또는 칸 없는 키가 있다(QuickSlotActions / QuickSlotCount)",
			QuickSlotActions.Num(), Inventory ? Inventory->GetQuickSlots().Num() : 0);
	}
	else
	{
		VB_LOG(Warning, "PlayerController: InputComponent 가 Enhanced Input 이 아니다 - 퀵슬롯 키를 묶지 못했다(DefaultInput.ini DefaultInputComponentClass)");
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

const UInputAction* AVBPlayerController::GetQuickSlotAction(int32 SlotIndex) const
{
	return QuickSlotActions.IsValidIndex(SlotIndex) ? QuickSlotActions[SlotIndex].Get() : nullptr;
}

void AVBPlayerController::HandleQuickSlotPressed(int32 SlotIndex)
{
	// 판단은 서버가 한다(빈 칸·가진 수·조건). 여기서는 칸 번호만 보낸다.
	if (UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(this))
	{
		Inventory->RequestUseQuickSlot(SlotIndex);
	}
	else
	{
		VB_LOG(Warning, "PlayerController: 퀵슬롯 %d - 인벤토리를 찾지 못했다(PlayerState 에 UVBInventoryComponent)", SlotIndex + 1);
	}
}

void AVBPlayerController::RequestPauseMenu()
{
	if (!IsLocalController())
	{
		return;
	}
	if (bGameOverActive)
	{
		// 사망 연출 중에는 열지 않는다(헤더 주석: 게임오버 화면이 뜨기 전 구간은 HUD 가 맨 위다).
		return;
	}
	// 정지는 일시정지 화면의 데이터(bPausesGame)를 보고 UI 관리자가 건다. SP/host 전용 경계는 그대로다 -
	//   SetPause 는 NM_Client 에서 조기 반환한다(SystemMap 15_PauseMenu).
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		UI->PushScreen(VBGameplayTags::UI_Screen_Pause);
	}
}

void AVBPlayerController::RequestInventoryScreen()
{
	if (!IsLocalController() || bGameOverActive)
	{
		return;
	}
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		UI->PushScreen(VBGameplayTags::UI_Screen_Inventory);
	}
}

void AVBPlayerController::RequestJournalScreen()
{
	if (!IsLocalController() || bGameOverActive)
	{
		return;
	}
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		UI->PushScreen(VBGameplayTags::UI_Screen_Journal);
	}
}

void AVBPlayerController::RequestSkillTreeScreen()
{
	if (!IsLocalController() || bGameOverActive)
	{
		return;
	}
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		UI->PushScreen(VBGameplayTags::UI_Screen_SkillTree);
	}
}

void AVBPlayerController::TeardownMenuState()
{
	// 레벨을 떠나기 전 메뉴 화면을 내린다. 일시정지 화면이 비활성화되는 순간 UI 관리자의 정지 목록이 비어
	//   정지가 풀린다 - 정지 상태로 레벨을 옮기지 않는다. 입력 모드는 따로 되돌리지 않는다: 다음 레벨의 HUD 화면이
	//   맨 위가 될 때 CommonUI 가 그 설정을 적용한다.
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		UI->PopScreen(VBGameplayTags::UI_Screen_Pause);
		UI->PopScreen(VBGameplayTags::UI_Screen_GameOver);
	}
}

void AVBPlayerController::QuitToMainMenu()
{
	// 메뉴 화면을 내려 정지를 푼다(정지 상태로 레벨 전환 금지). 메뉴 레벨의 입력 모드는 그 레벨의 메인 메뉴 화면이 정한다.
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

	// PC 자신의 입력도 끊는다. 폰의 DisableInput 은 폰이 바인드한 것만 죽이고 PC 가 바인드한 것(지금은 디버그 키)은
	// 살아남기 때문이다. DisableInput 은 InputComponent 를 스택에서 통째로 pop 하므로 앞으로 PC 에 어떤 액션이 추가돼도
	// 자동으로 막힌다. 일시정지 입력은 여기서 막히지 않는다 - HUD 화면이 CommonUI 로 받으므로 bGameOverActive 가 막는다.
	// 비가역이어도 무해한 이유는 아래 HUD 주석과 같다 - 두 출구가 모두 OpenLevel 이다.
	DisableInput(this);

	// HUD 층을 Collapsed 로 감춘다. HUD 화면을 내리는 것이 아니다. 근거:
	//   ① HUD 는 입력의 바닥이다(.sw/ui-foundation 2.2). 내리면 게임오버 화면이 뜨기 전 구간에 활성 화면이 하나도 없어
	//      CommonUI 가 입력 설정을 아무도 말하지 않는 상태가 된다.
	//   ② Collapsed 는 레이아웃뿐 아니라 히트테스트에서도 제거된다 → HP 0 짜리 바가 게임오버 버튼 클릭을 가로챌 여지가 없다.
	//   ③ Collapsed 위젯은 tick 되지 않는다 → 락온 마커 투영이 죽은 플레이어에게 도는 낭비가 없다.
	//      엔진 근거(2026-07-29 확인): NativeTick 은 SObjectWidget::Tick 에서만 불리고, 그건 SWidget::Paint 안에서만 불린다.
	//      Prepass 는 EVisibility::Collapsed 를 배제한다(SWidget.cpp::SWidget::Paint) → Paint 안 됨 = Tick 안 됨.
	//   ④ 가역적 — 같은 레벨 부활을 나중에 붙일 때 SetLayerHidden(false) 한 줄로 되돌린다.
	UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this);
	if (UI)
	{
		UI->SetLayerHidden(VBGameplayTags::UI_Layer_Game, true);
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

	// 게임을 멈추지 않는다 (2026-07-28 user 결정: "타임 안멈춰도 돼 그냥 진행해도돼"). 게임오버 화면의 데이터가
	//   bPausesGame=false 다. 그래서 입력 차단을 정지에 기대지 않는다. 방어선:
	//   ① 게임오버 화면의 Menu 입력 모드 — 게임 입력 전체를 뷰포트에서 거른다(UVBGameOverWidget 생성자)
	//   ② 폰의 DisableInput — 폰이 바인드한 Enhanced Input 전량 (AVBCharacter::HandleDeathCosmetic)
	//   ③ PC 자신의 DisableInput(this) (OnPlayerDeath)
	//   ④ bGameOverActive — 화면이 뜨기 전 구간의 일시정지 요청 거절(RequestPauseMenu)
	// 초점·커서는 게임오버 화면과 CommonUI 가 맡는다. 이미 떠 있으면 UI 관리자가 무시한다.
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		UI->PushScreen(VBGameplayTags::UI_Screen_GameOver);
	}
	else
	{
		// 조용한 실패 금지: 화면이 없으면 조작 불능인 시체만 남으므로 원인을 로그로 남긴다.
		VB_LOG(Warning, "[VBPC] UI 관리자를 못 찾았다 — 게임오버 화면이 뜨지 않습니다.");
	}
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
