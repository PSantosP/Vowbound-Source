// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBHealthBarWidget.h"
#include "VBLockOnMarkerWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "CommonInputTypeEnum.h"   // ECommonInputType - 장치 교체 델리게이트 인자
#include "CommonInputModeTypes.h"  // ECommonInputMode - 입력 모드 바뀜 인자
#include "GameplayTagContainer.h"  // FGameplayTag - 해금 알림의 직전 목록
#include "Progression/VBGrowthRules.h" // VBGrowth::FNoticeBaseline - 레벨업·해금 알림 기준
#include "UI/VBScreenWidget.h"
#include "UI/Notice/VBNoticeFeed.h" // EVBNoticeCategory - PushNotice 인자
#include "VBHUDWidget.generated.h"

class AVBCharacter;
class UInputAction;
class UVBOffscreenThreatWidget;
class UVBInteractPromptWidget;
class UVBInteractableComponent;

/**
 * 게임 중 HUD. UI 층의 Game 층에 올라가는 바닥 화면이다(.sw/ui-foundation 2.2).
 *
 * 바닥이라는 것의 뜻: CommonUI 는 메뉴가 닫힐 때 이전 입력 설정을 되돌리지 않고 새로 맨 위가 된 화면에게 다시 묻는다.
 *   이 화면이 절대 꺼지지 않고 Game 입력을 말하고 있어야 메뉴를 닫은 뒤 캐릭터 조작이 돌아온다.
 *   그래서 사망 연출도 이 화면을 끄지 않고 층을 감춘다(UVBUIManagerSubsystem::SetLayerHidden).
 */
UCLASS()
class VOWBOUND_API UVBHUDWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBHUDWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	
	
	
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeDestruct() override;

	// 일시정지를 여는 입력(IA_Pause). CommonUI 가 이 IA 의 키를 활성 IMC 에서 찾아 이 화면의 바인딩으로 부른다.
	//   PC 가 Enhanced Input 으로 직접 받던 것을 옮긴 이유: 화면을 여는 주인을 UI 층 하나로 두려고(Lyra HUD 와 같은 자리).
	//   이 IA 에 CommonInputMetadata 를 붙이지 않는다 - 붙이면 Enhanced Input 주입 경로로 갈라진다(설계 2.2).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	TObjectPtr<UInputAction> PauseInputAction;

	// 인벤토리를 여는 입력(IA_Inventory). 일시정지와 같은 자리·같은 이유(화면을 여는 주인 = UI 층, PC 가 열어도 되는지 판단).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	TObjectPtr<UInputAction> InventoryInputAction;

	// 퀘스트 일지를 여는 입력(IA_Journal). 인벤토리와 같은 자리·같은 이유.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	TObjectPtr<UInputAction> JournalInputAction;

	// 스킬 트리를 여는 입력(IA_SkillTree). 인벤토리와 같은 자리·같은 이유(.sw/remaining-features/b2-growth.md).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	TObjectPtr<UInputAction> SkillTreeInputAction;

	// 레벨·경험치 줄. {0} = 레벨, {1} = 이 레벨에서 모은 양, {2} = 다음 레벨까지 필요한 양. 비면 줄과 막대를 만들지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FText ProgressionLineFormat = NSLOCTEXT("VBHUD", "ProgressionLine", "Lv {0}   {1} / {2}");

	// 최대 레벨일 때의 줄. {0} = 레벨.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FText ProgressionMaxFormat = NSLOCTEXT("VBHUD", "ProgressionMax", "Lv {0}");

	// 레벨업 알림(저장·획득과 같은 한 줄). {0} = 새 레벨. 비면 안 띄운다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FText LevelUpFormat = NSLOCTEXT("VBHUD", "LevelUp", "레벨 {0} - 스킬 포인트를 얻었다");

	// 레벨·경험치 줄의 자리(앵커·정렬·거리)와 막대 크기. 기본 = 왼쪽 위.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D ProgressionAnchor = FVector2D(0.0f, 0.0f);

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D ProgressionAlignment = FVector2D(0.0f, 0.0f);

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D ProgressionOffset = FVector2D(40.0f, 120.0f);

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D ProgressionBarSize = FVector2D(260.0f, 6.0f);

	// 퀘스트 알림 글. {0} = 퀘스트 제목. 비어 있으면 그 알림을 안 띄운다(알림 줄은 저장·획득과 같은 한 줄이다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FText QuestStartedFormat = NSLOCTEXT("VBHUD", "QuestStarted", "새 퀘스트: {0}");

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FText QuestAdvancedFormat = NSLOCTEXT("VBHUD", "QuestAdvanced", "퀘스트 갱신: {0}");

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FText QuestCompletedFormat = NSLOCTEXT("VBHUD", "QuestCompleted", "퀘스트 완료: {0}");

	// HUD 안에 HealthBar 위젯을 자식으로 배치한다.
	// BP에서는 PlayerHealthBar 이름의 VBHealthBarWidget을 추가한다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UVBHealthBarWidget> PlayerHealthBar;
	
	// 기존 WBP_HUD 자식과의 호환용. 락온 이미지는 표시하지 않는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UVBLockOnMarkerWidget> LockOnMarker;

	// 화면 밖 위협 화살표 위젯 클래스(WBP_OffscreenThreat, design §11). 런타임에 만들어 루트 캔버스에 붙인다.
	// 왜 BindWidget 이 아닌가: WBP_HUD 트리는 MCP 저작 도구가 통째로만 덮고 UserWidget 자식(체력바·락온 마커)을 못 넣는다.
	//   런타임 생성이면 WBP_HUD 를 건드리지 않고, 화살표의 그림·크기는 여전히 WBP 자산이 든다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	TSubclassOf<UVBOffscreenThreatWidget> OffscreenThreatWidgetClass;

	// 화살표가 화면 가장자리에서 안쪽으로 물러나는 여백(DPI 스케일 단위). 화살표 절반 크기보다 커야 잘리지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD", meta=(ClampMin="0.0"))
	float OffscreenThreatEdgeMargin = 80.0f;

	// 화살표 위젯의 캔버스 크기. 위젯 자산의 그림 크기와 맞춘다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D OffscreenThreatSize = FVector2D(64.0f, 64.0f);

	// 상호작용 프롬프트 위젯 클래스(WBP_InteractPrompt). 화면 밖 화살표와 같은 이유로 런타임에 루트 캔버스에 붙인다.
	// 대상 컴포넌트 위치에 투영해 따라간다(user 결정 2026-09-23 - 하단 중앙 고정이 아니다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	TSubclassOf<UVBInteractPromptWidget> InteractPromptWidgetClass;

	// 투영점에 대한 프롬프트의 정렬. (0.5, 1) = 가로 가운데, 프롬프트 아래끝이 대상 위치에 닿는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D InteractPromptAlignment = FVector2D(0.5f, 1.0f);

	// 퀵슬롯 칸 줄(WBP_QuickSlotBar). 화면 밖 화살표와 같은 이유로 런타임에 루트 캔버스에 붙인다(설계 .sw/quickslot-input 4.1절 (가)).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	TSubclassOf<class UVBQuickSlotBarWidget> QuickSlotBarWidgetClass;

	// 칸 줄의 자리. 앵커(0~1, 화면 비율)·정렬(칸 줄 크기 비율)·앵커에서의 거리(픽셀). 기본 = 화면 아래 가운데에서 위로 40.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D QuickSlotBarAnchor = FVector2D(0.5f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D QuickSlotBarAlignment = FVector2D(0.5f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D QuickSlotBarOffset = FVector2D(0.0f, -40.0f);

	// 혼잣말 자막(WBP_Subtitle). 퀵슬롯 칸 줄과 같은 이유로 런타임에 루트 캔버스에 붙인다(설계 .sw/prologue-opening 3.4).
	//   PC 의 UVBSpokenLineComponent 에 묶는다 - 위젯은 그리기만 한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	TSubclassOf<class UVBSubtitleWidget> SubtitleWidgetClass;

	// 자막의 자리. 기본 = 화면 아래 가운데, 퀵슬롯 칸 줄 위.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D SubtitleAnchor = FVector2D(0.5f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D SubtitleAlignment = FVector2D(0.5f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FVector2D SubtitleOffset = FVector2D(0.0f, -150.0f);

	// 화면 터치 조작(WBP_TouchControls - 스틱·시점 패드·버튼). 화면 전체를 덮게 루트 캔버스에 붙인다.
	// 마지막 입력이 터치일 때만 보인다. 콘솔 변수 vb.TouchControls 로 강제할 수 있다(설계 .sw/touch-controls).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	TSubclassOf<UUserWidget> TouchControlsWidgetClass;

	// 저장이 끝나면 화면 위쪽에 잠깐 띄울 글(「저장됨」). 비어 있으면 알림을 안 띄운다.
	// 저장 포인트(자동)와 일시정지 메뉴 저장이 같은 알림을 쓴다 - 저장 완료 신호 하나를 듣기 때문이다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FText SaveCompletedText;

	// 아이템을 얻었을 때 띄울 글. {0} = 아이템 이름, {1} = 수. 비어 있으면 획득 알림을 안 띄운다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FText ItemAcquiredFormat = NSLOCTEXT("VBHUD", "ItemAcquired", "{0} x{1} 획득");

	// 스킬 칸을 열었을 때 띄울 글. {0} = 칸 이름(스킬 트리 정의의 DisplayName). 비어 있으면 안 띄운다.
	//   알림이 떠 있는 시간·최대 줄 수는 UVBUISettings(NoticeSeconds·MaxNotices)가 든다 - 옛 SaveNoticeDuration 은 2026-10-07 거기로 옮겼다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|HUD")
	FText SkillUnlockedFormat = NSLOCTEXT("VBHUD", "SkillUnlocked", "해금: {0}");

private:
	// 조종 중인 캐릭터가 생기면 한 번 초기화한다. 화면은 캐릭터보다 먼저 뜰 수 있어(비동기 로드·빙의 순서)
	//   캐릭터가 없으면 PC 의 빙의 알림을 기다린다.
	void TryInitializeFromOwningPawn();
	void InitializeHUD(AVBCharacter* Owner);
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);
	bool bHUDInitialized = false;

	void HandlePauseInput();
	void HandleInventoryInput();
	void HandleJournalInput();
	void HandleSkillTreeInput();

	// 터치 버튼이 누른 액션이 위 넷 중 하나면 같은 Handle*Input 으로 보내고 참. 터치 칸은 IA 만 가리키고,
	//   어느 액션이 어느 화면인지는 여기 한 곳이 안다(.sw/touch-controls 2단계 - 주입으로는 CommonUI 묶음이 발화하지 않는다).
	bool TryHandleScreenAction(const UInputAction* Action);

	// 입력 모드가 Menu 로 바뀌면(메뉴가 맨 위) 터치 조작을 놓고 감춘다. 터치 조작은 키가 아니라 IA 에 값을 직접
	//   넣으므로 Menu 모드의 게임 입력 차단을 안 지난다 - 스틱을 쥔 채 메뉴가 뜨면 캐릭터가 계속 걷는다(설계 2.2 터치).
	void HandleInputModeChanged(ECommonInputMode NewMode);
	bool bGameInputActive = true;

	TWeakObjectPtr<AVBCharacter> OwnerCharacter;

	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> HUDFadeIn;

	// 런타임 생성 화살표와 그 캔버스 슬롯. 클래스 미할당·루트가 캔버스가 아니면 둘 다 null 이고 경고를 남긴다.
	UPROPERTY()
	TObjectPtr<UVBOffscreenThreatWidget> OffscreenThreat;
	UPROPERTY()
	TObjectPtr<UCanvasPanelSlot> OffscreenThreatSlot;

	// 상호작용 프롬프트와 그 슬롯. 클래스 미할당·루트가 캔버스가 아니면 둘 다 null 이고 경고를 남긴다.
	UPROPERTY()
	TObjectPtr<UVBInteractPromptWidget> InteractPrompt;
	UPROPERTY()
	TObjectPtr<UCanvasPanelSlot> InteractPromptSlot;

	// 지금 프롬프트가 가리키는 대상. 초점 델리게이트가 바꾼다.
	TWeakObjectPtr<UVBInteractableComponent> PromptTarget;
	// 홀드 진행을 매 틱 읽는다. InitializeHUD 에서 한 번 찾는다.
	TWeakObjectPtr<class UVBInteractorComponent> Interactor;
	// 상호작용 거절 글(조건 칸)을 알림 줄로 받는 구독. 폰이 바뀌어 다시 묶을 때 앞 것을 뺀다.
	FDelegateHandle InteractionRefusedHandle;

	// 마지막으로 띄운 행동 이름. 대상이 상태에 따라 이름을 바꾸므로(열기 -> 닫기) 매 틱 비교해 바뀔 때만 다시 쓴다.
	FText ShownActionText;
	// 키 이름. 초점·입력 장치가 바뀔 때만 다시 구한다.
	FText ShownKeyText;

	UFUNCTION()
	void OnInteractFocusChanged(UVBInteractableComponent* NewFocus);
	UFUNCTION()
	void OnInputDeviceChanged(ECommonInputType NewDeviceType);

	void RefreshInteractKeyText();
	void UpdateInteractPrompt();

	// 지금 장치에서 Action 에 매핑된 첫 키의 이름. 상호작용 프롬프트와 퀵슬롯 칸 줄이 같이 쓴다.
	//   터치면 빈 글(누를 키가 없다). 지금 IMC 에 매핑이 없으면 빈 글이고 bOutUnmapped = true - 말할지는 부르는 쪽이 정한다.
	FText KeyTextFor(const UInputAction* Action, bool& bOutUnmapped) const;

	// 퀵슬롯 칸 줄. 클래스 미할당·루트가 캔버스가 아니면 null 이고 경고를 남긴다.
	UPROPERTY()
	TObjectPtr<class UVBQuickSlotBarWidget> QuickSlotBar;

	// 혼잣말 자막. 클래스 미할당·루트가 캔버스가 아니면 null 이고 경고를 남긴다.
	UPROPERTY()
	TObjectPtr<class UVBSubtitleWidget> Subtitle;
	// 칸마다 키 이름을 다시 구해 칸 줄에 넣는다. 장치가 바뀔 때·메뉴를 닫고 게임 입력으로 돌아올 때(설정에서 키를 바꿨을 수 있다).
	void RefreshQuickSlotKeyTexts();

	// 터치 조작. 입력 종류는 장치 델리게이트가 갱신한다.
	UPROPERTY()
	TObjectPtr<UUserWidget> TouchControls;
	ECommonInputType LastInputType = ECommonInputType::MouseAndKeyboard;
	void UpdateTouchControlsVisibility();

	// 알림 한 줄을 알림 서브시스템에 넘긴다(UVBNoticeSubsystem - 줄의 주인·그리기는 거기, .sw/remaining-features/a2-notices.md).
	//   HUD 는 이 플레이어의 사건(저장·획득·퀘스트·레벨·해금·거절)을 듣는 생산자일 뿐이다. 빈 글은 안 띄운다.
	void PushNotice(EVBNoticeCategory Category, const FText& Text, FName MergeKey = NAME_None);

	UFUNCTION()
	void OnGameSaveCompleted(int32 SlotIndex, bool bSuccess);

	// 이 플레이어의 인벤토리가 알린 획득. InitializeHUD 가 구독한다.
	void HandleItemAcquired(const class UVBItemDefinition* Item, int32 Count);
	TWeakObjectPtr<class UVBInventoryComponent> BoundInventory;
	FDelegateHandle ItemAcquiredHandle;

	// 퀘스트 목표 줄(화면 오른쪽 위). 지금 따라가는 퀘스트 = 가장 최근에 시작·진행된 진행 중 퀘스트(UVBQuestLogComponent::GetTrackedEntry).
	//   InitializeHUD 가 루트 캔버스에 만든다(알림 줄과 같은 길). 글의 모양은 VBQuestText 가 정한다 - 일지와 같은 글.
	UPROPERTY()
	TObjectPtr<class UTextBlock> QuestTracker;
	TWeakObjectPtr<class UVBQuestLogComponent> BoundQuestLog;
	FDelegateHandle QuestLogChangedHandle;
	FDelegateHandle QuestNoticeHandle;
	void RefreshQuestTracker();
	// 퀘스트 알림 글. 알림 종류(EVBQuestNotice)는 구독하는 람다가 글 양식으로 바꿔 넘긴다 - 이 헤더가 퀘스트 헤더를 끌어오지 않게.
	void ShowQuestNotice(const class UVBQuestDefinition* Quest, const FText& Format);

	// 레벨·경험치 줄과 막대(퀘스트 목표 줄과 같은 길 - InitializeHUD 가 루트 캔버스에 만든다). 성장은 PlayerState 에 있어 한 번만 구독한다.
	UPROPERTY()
	TObjectPtr<class UTextBlock> ProgressionText;
	UPROPERTY()
	TObjectPtr<class UProgressBar> ProgressionBar;
	TWeakObjectPtr<class UVBProgressionComponent> BoundProgression;
	FDelegateHandle ProgressionChangedHandle;
	// 레벨업·해금 알림의 기준(직전에 본 레벨·연 칸·세이브 읽기 표시). 처음 그림과 불러오기는 기준만 잡는다 - 판단은 VBGrowth::StepNoticeBaseline 하나.
	VBGrowth::FNoticeBaseline NoticeBaseline;
	void RefreshProgression();

	// 추적기(UVBThreatTrackerSubsystem)의 예고 중인 적 가운데 화면 밖인 첫 놈을 가장자리 화살표로 가리킨다. 없으면 숨긴다.
	void UpdateOffscreenThreat(const FGeometry& MyGeometry);
};
