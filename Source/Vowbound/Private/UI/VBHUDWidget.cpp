// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "UI/VBHUDWidget.h"

#include "Character/VBCharacter.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "Input/VBInputDeviceManager.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "UI/VBInteractPromptWidget.h"
#include "UI/Inventory/VBQuickSlotBarWidget.h"
#include "UI/Dialogue/VBSubtitleWidget.h"
#include "Narrative/Dialogue/VBSpokenLineComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Progression/VBProgressionComponent.h"
#include "Progression/VBSkillTreeDefinition.h"
#include "UI/Notice/VBNoticeSubsystem.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Quest/VBQuestText.h"
#include "UI/VBOffscreenThreatWidget.h"
#include "AbilitySystem/VBThreatTrackerSubsystem.h"
#include "UI/VBTouchControls.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/CommonUIInputTypes.h"
#include "Player/VBPlayerController.h"
#include "Vowbound/Vowbound.h"

// 화면 터치 조작 표시. 시험용(폰 스트리밍) 이라 값의 홈을 설정 자산이 아니라 콘솔 변수에 둔다 -
//   실행 중에 바꿔 보는 것이 목적이고 저장할 이유가 없다.
static TAutoConsoleVariable<int32> CVarTouchControls(
	TEXT("vb.TouchControls"), 0,
	TEXT("화면 터치 조작 표시. 0 = 마지막 입력이 터치일 때 / 1 = 늘 보임 / 2 = 늘 숨김"),
	ECVF_Default);

UVBHUDWidget::UVBHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 입력의 바닥: 게임 입력. 첫 클릭까지 게임으로 보낸다 - 그냥 CapturePermanently 면 메뉴를 닫은 직후
	//   첫 클릭을 마우스 캡처가 먹어 공격이 한 번 안 나간다(Lyra 포럼 보고, 설계 2.2).
	InputMode = EVBScreenInputMode::Game;
	GameMouseCaptureMode = EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown;
}

void UVBHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 락온 상태와 관계없이 이미지를 숨긴다. Pawn 초기화 전에도 표시되지 않아야 한다.
	if (LockOnMarker)
	{
		LockOnMarker->SetVisibility(ESlateVisibility::Collapsed);
	}

	// HUD 는 Game 모드 화면이다. CommonUI 바인딩의 기본 모드는 Menu 이고, 바인딩은 «지금 입력 모드가 All 이거나 바인딩 모드와 같을 때» 만 반응한다
	//   (CommonUI UIActionRouterTypes.cpp::FActionRouterBindingCollection::ProcessNormalInput). 기본값 그대로면 Game 모드가 실제로 걸린 뒤
	//   (메뉴에서 이어하기로 넘어온 뒤, 메뉴를 닫은 뒤) 이 두 키가 안 먹는다. 에디터 PIE 첫 시작은 Game 모드 적용이 건너뛰어져 가려져 있었다.
	if (PauseInputAction)
	{
		FBindUIActionArgs PauseArgs(PauseInputAction, /*bShouldDisplayInActionBar=*/false,
		                            FSimpleDelegate::CreateUObject(this, &UVBHUDWidget::HandlePauseInput));
		PauseArgs.InputMode = ECommonInputMode::Game;
		RegisterUIActionBinding(PauseArgs);
	}
	else
	{
		VB_LOG(Warning, "HUD: PauseInputAction 미할당 - Esc 로 일시정지가 안 열린다 (WBP_HUD 클래스 기본값)");
	}
	if (InventoryInputAction)
	{
		FBindUIActionArgs InventoryArgs(InventoryInputAction, /*bShouldDisplayInActionBar=*/false,
		                                FSimpleDelegate::CreateUObject(this, &UVBHUDWidget::HandleInventoryInput));
		InventoryArgs.InputMode = ECommonInputMode::Game;
		RegisterUIActionBinding(InventoryArgs);
	}
	else
	{
		VB_LOG(Warning, "HUD: InventoryInputAction 미할당 - 인벤토리가 안 열린다 (WBP_HUD 클래스 기본값)");
	}
	if (JournalInputAction)
	{
		// Game 모드 화면의 바인딩이라 InputMode = Game(기본값 Menu 면 Game 모드가 실제로 걸린 뒤 무시된다 - 인벤토리와 같은 함정).
		FBindUIActionArgs JournalArgs(JournalInputAction, /*bShouldDisplayInActionBar=*/false,
		                              FSimpleDelegate::CreateUObject(this, &UVBHUDWidget::HandleJournalInput));
		JournalArgs.InputMode = ECommonInputMode::Game;
		RegisterUIActionBinding(JournalArgs);
	}
	else
	{
		VB_LOG(Warning, "HUD: JournalInputAction 미할당 - 퀘스트 일지가 안 열린다 (WBP_HUD 클래스 기본값)");
	}
	if (SkillTreeInputAction)
	{
		// 인벤토리·일지와 같은 Game 모드 바인딩(기본값 Menu 면 Game 모드가 실제로 걸린 뒤 무시된다).
		FBindUIActionArgs SkillTreeArgs(SkillTreeInputAction, /*bShouldDisplayInActionBar=*/false,
		                                FSimpleDelegate::CreateUObject(this, &UVBHUDWidget::HandleSkillTreeInput));
		SkillTreeArgs.InputMode = ECommonInputMode::Game;
		RegisterUIActionBinding(SkillTreeArgs);
	}
	else
	{
		VB_LOG(Warning, "HUD: SkillTreeInputAction 미할당 - 스킬 트리가 안 열린다 (WBP_HUD 클래스 기본값)");
	}

	if (const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
		{
			Router->OnActiveInputModeChanged().AddUObject(this, &UVBHUDWidget::HandleInputModeChanged);
		}
	}
}

void UVBHUDWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	TryInitializeFromOwningPawn();
}

void UVBHUDWidget::NativeDestruct()
{
	if (const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
		{
			Router->OnActiveInputModeChanged().RemoveAll(this);
		}
	}
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UVBHUDWidget::HandlePossessedPawnChanged);
	}
	if (UVBInventoryComponent* Inventory = BoundInventory.Get())
	{
		Inventory->OnItemAcquired.Remove(ItemAcquiredHandle);
	}
	if (UVBInteractorComponent* BoundInteractor = Interactor.Get())
	{
		BoundInteractor->OnInteractionRefused.Remove(InteractionRefusedHandle);
	}
	if (UVBQuestLogComponent* QuestLog = BoundQuestLog.Get())
	{
		QuestLog->OnQuestLogChanged.Remove(QuestLogChangedHandle);
		QuestLog->OnQuestNotice.Remove(QuestNoticeHandle);
	}
	if (UVBProgressionComponent* Progression = BoundProgression.Get())
	{
		Progression->OnProgressionChanged.Remove(ProgressionChangedHandle);
	}
	Super::NativeDestruct();
}

void UVBHUDWidget::TryInitializeFromOwningPawn()
{
	if (bHUDInitialized)
	{
		return;
	}
	if (AVBCharacter* VBChar = GetOwningPlayerPawn<AVBCharacter>())
	{
		InitializeHUD(VBChar);
		return;
	}
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UVBHUDWidget::HandlePossessedPawnChanged);
	}
}

void UVBHUDWidget::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (AVBCharacter* VBChar = Cast<AVBCharacter>(NewPawn))
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			PC->OnPossessedPawnChanged.RemoveDynamic(this, &UVBHUDWidget::HandlePossessedPawnChanged);
		}
		if (!bHUDInitialized)
		{
			InitializeHUD(VBChar);
		}
	}
}

void UVBHUDWidget::HandlePauseInput()
{
	// 열어도 되는지(사망 연출 중이면 안 된다)는 PC 가 안다. HUD 는 요청만 한다.
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		PC->RequestPauseMenu();
	}
}

void UVBHUDWidget::HandleInventoryInput()
{
	// 일시정지와 같다 - 열어도 되는지는 PC 가 안다.
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		PC->RequestInventoryScreen();
	}
}

void UVBHUDWidget::HandleJournalInput()
{
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		PC->RequestJournalScreen();
	}
}

void UVBHUDWidget::HandleSkillTreeInput()
{
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		PC->RequestSkillTreeScreen();
	}
}

bool UVBHUDWidget::TryHandleScreenAction(const UInputAction* Action)
{
	if (!Action)
	{
		return false;
	}
	if (Action == PauseInputAction)
	{
		HandlePauseInput();
		return true;
	}
	if (Action == InventoryInputAction)
	{
		HandleInventoryInput();
		return true;
	}
	if (Action == JournalInputAction)
	{
		HandleJournalInput();
		return true;
	}
	if (Action == SkillTreeInputAction)
	{
		HandleSkillTreeInput();
		return true;
	}
	return false;
}

void UVBHUDWidget::RefreshProgression()
{
	const UVBProgressionComponent* Progression = BoundProgression.Get();
	if (!Progression)
	{
		return;
	}
	const int32 Level = Progression->GetLevel();
	const bool bMax = Level >= Progression->GetMaxLevel();
	if (ProgressionText)
	{
		ProgressionText->SetText(bMax ? FText::Format(ProgressionMaxFormat, Level)
			: FText::Format(ProgressionLineFormat, Level, Progression->GetXPIntoLevel(), Progression->GetXPForNextLevel()));
	}
	if (ProgressionBar)
	{
		const int32 Needed = Progression->GetXPForNextLevel();
		ProgressionBar->SetPercent(bMax || Needed <= 0 ? 1.0f : static_cast<float>(Progression->GetXPIntoLevel()) / Needed);
	}
	// 무엇을 알릴지는 성장 규칙 한 곳이 정한다 - 처음 그림·불러오기(세이브 읽기 표시가 바뀐 갱신)는 기준만 잡고, 그 뒤의 오름·해금만 알린다.
	//   종전에는 «0 이 아니면» 만 걸러, 레벨 1 을 먼저 본 HUD 가 이어하기의 레벨을 «오름» 으로 읽어 알림이 몰려 떴다(f3 verify 10-08 Q3).
	//   한 번에 여러 레벨이 오르면 한 줄(최신 레벨)로 합친다.
	const VBGrowth::FNotices Notices = VBGrowth::StepNoticeBaseline(NoticeBaseline, Level, Progression->GetUnlocked(), Progression->GetSaveReadSerial());
	if (Notices.LevelUpTo > 0 && !LevelUpFormat.IsEmpty())
	{
		PushNotice(EVBNoticeCategory::Progression, FText::Format(LevelUpFormat, Notices.LevelUpTo), TEXT("LevelUp"));
	}
	if (!SkillUnlockedFormat.IsEmpty())
	{
		const UVBSkillTreeDefinition* Tree = Progression->GetSkillTree();
		for (const FGameplayTag& NodeId : Notices.NewlyUnlocked)
		{
			const FVBSkillNode* Node = Tree ? Tree->FindNode(NodeId) : nullptr;
			const FText Name = Node && !Node->DisplayName.IsEmpty() ? Node->DisplayName : FText::FromName(NodeId.GetTagName());
			PushNotice(EVBNoticeCategory::Progression, FText::Format(SkillUnlockedFormat, Name));
		}
	}
}

void UVBHUDWidget::HandleInputModeChanged(ECommonInputMode NewMode)
{
	bGameInputActive = NewMode != ECommonInputMode::Menu;
	if (!bGameInputActive)
	{
		if (UVBTouchControlsWidget* Touch = Cast<UVBTouchControlsWidget>(TouchControls))
		{
			Touch->ReleaseAllTouches();
		}
	}
	else
	{
		// 메뉴를 닫고 돌아왔다 - 그 사이 설정 화면에서 키를 바꿨을 수 있다. 다시 지어진 매핑에서 칸의 키 이름을 다시 읽는다.
		RefreshQuickSlotKeyTexts();
	}
	UpdateTouchControlsVisibility();
}

void UVBHUDWidget::InitializeHUD(AVBCharacter* Owner)
{
	bHUDInitialized = true;

	// Owner 캐싱
	OwnerCharacter = Owner;

	// ASC 가져와서 HealthBar 초기화
	// 굳이 Initialize를 한 이유는 ASC가 초기화가 준비된 이후에 호출하는게 안전하기 때문
	// BeginPlay보다 안전
	if (UAbilitySystemComponent* ASC = OwnerCharacter->GetAbilitySystemComponent(); ASC && PlayerHealthBar)
	{
		PlayerHealthBar->InitializeWithASC(ASC);
	}
	

	// 화면 밖 위협 화살표 (design §11). 루트 캔버스에 런타임으로 붙인다 - WBP_HUD 트리를 건드리지 않는 이유는 헤더 주석.
	// 못 붙이는 두 경우(클래스 미할당·루트가 캔버스 아님)는 Warning - 조용히 빠지면 «화살표가 안 뜬다» 를 자산 문제로 못 잡는다.
	if (OffscreenThreatWidgetClass)
	{
		if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget()))
		{
			OffscreenThreat = CreateWidget<UVBOffscreenThreatWidget>(GetOwningPlayer(), OffscreenThreatWidgetClass);
			if (OffscreenThreat)
			{
				OffscreenThreatSlot = RootCanvas->AddChildToCanvas(OffscreenThreat);
				if (OffscreenThreatSlot)
				{
					OffscreenThreatSlot->SetAlignment(FVector2D(0.5f, 0.5f));
					OffscreenThreatSlot->SetAutoSize(false);
					OffscreenThreatSlot->SetSize(OffscreenThreatSize);
				}
				OffscreenThreat->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		else
		{
			VB_LOG(Warning, "HUD: 루트 위젯이 CanvasPanel 이 아니라 화면 밖 위협 화살표를 붙일 수 없다 (%s)", *GetNameSafe(GetRootWidget()));
		}
	}
	else
	{
		VB_LOG(Warning, "HUD: OffscreenThreatWidgetClass 미할당 - 화면 밖 위협 화살표 없음 (WBP_HUD 클래스 기본값)");
	}

	// 상호작용 프롬프트. 화면 밖 화살표와 같은 방식으로 붙인다 - 못 붙이는 두 경우는 같은 이유로 Warning.
	if (InteractPromptWidgetClass)
	{
		if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget()))
		{
			InteractPrompt = CreateWidget<UVBInteractPromptWidget>(GetOwningPlayer(), InteractPromptWidgetClass);
			if (InteractPrompt)
			{
				InteractPromptSlot = RootCanvas->AddChildToCanvas(InteractPrompt);
				if (InteractPromptSlot)
				{
					InteractPromptSlot->SetAlignment(InteractPromptAlignment);
					InteractPromptSlot->SetAutoSize(true);
				}
				InteractPrompt->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		else
		{
			VB_LOG(Warning, "HUD: 루트 위젯이 CanvasPanel 이 아니라 상호작용 프롬프트를 붙일 수 없다 (%s)", *GetNameSafe(GetRootWidget()));
		}
	}
	else
	{
		VB_LOG(Warning, "HUD: InteractPromptWidgetClass 미할당 - 상호작용 프롬프트 없음 (WBP_HUD 클래스 기본값)");
	}

	Interactor = OwnerCharacter->FindComponentByClass<UVBInteractorComponent>();
	if (Interactor.IsValid())
	{
		Interactor->OnFocusChanged.AddUniqueDynamic(this, &UVBHUDWidget::OnInteractFocusChanged);
		OnInteractFocusChanged(Interactor->GetFocusedInteractable());
		// 대상의 조건 칸에 걸린 시도의 답(.sw/prologue-opening 14.4). 시도에 대한 짧은 한 줄이라 저장됨·획득과 같은 알림 줄을 쓴다.
		//   같은 키로 합쳐 거듭 눌러도 줄이 쌓이지 않고 마지막 답 한 줄만 남는다.
		Interactor->OnInteractionRefused.Remove(InteractionRefusedHandle);
		InteractionRefusedHandle = Interactor->OnInteractionRefused.AddWeakLambda(this, [this](const FText& RefusedText)
		{
			PushNotice(EVBNoticeCategory::Refusal, RefusedText, TEXT("InteractionRefused"));
		});
	}
	// 터치 조작. 화면 전체를 덮는다 - 시점 패드가 넓은 영역을 받아야 해서다. 못 붙이면 말한다(위와 같은 이유).
	if (TouchControlsWidgetClass)
	{
		if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget()))
		{
			TouchControls = CreateWidget<UUserWidget>(GetOwningPlayer(), TouchControlsWidgetClass);
			if (TouchControls)
			{
				if (UCanvasPanelSlot* TouchSlot = RootCanvas->AddChildToCanvas(TouchControls))
				{
					TouchSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
					TouchSlot->SetOffsets(FMargin(0.0f));
				}
				TouchControls->SetVisibility(ESlateVisibility::Collapsed);
				if (UVBTouchControlsWidget* Touch = Cast<UVBTouchControlsWidget>(TouchControls))
				{
					Touch->SetUIActionHandler(FVBTouchUIActionHandler::CreateUObject(this, &UVBHUDWidget::TryHandleScreenAction));
				}
			}
		}
		else
		{
			VB_LOG(Warning, "HUD: 루트 위젯이 CanvasPanel 이 아니라 터치 조작을 붙일 수 없다 (%s)", *GetNameSafe(GetRootWidget()));
		}
	}
	// 터치 조작은 시험용이라 없는 것이 기본이다. 경고하지 않는다 - 미할당이 정상 상태다.

	// 알림의 생산자들(저장됨·아이템 획득). 줄을 그리는 것은 알림 서브시스템과 그 위젯이다(설계 .sw/remaining-features/a2-notices.md) -
	//   HUD 는 이 플레이어의 사건을 듣고 Push 만 한다. 글이 비면 그 알림을 끈 것이라 구독하지 않는다.
	if (!SaveCompletedText.IsEmpty())
	{
		if (UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this))
		{
			Save->OnGameSaveCompleted.AddUniqueDynamic(this, &UVBHUDWidget::OnGameSaveCompleted);
		}
	}
	if (!ItemAcquiredFormat.IsEmpty())
	{
		// 인벤토리는 PlayerState 에 있다 - 폰이 바뀌어도 같은 것이라 한 번만 구독한다.
		if (UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(Owner))
		{
			BoundInventory = Inventory;
			ItemAcquiredHandle = Inventory->OnItemAcquired.AddUObject(this, &UVBHUDWidget::HandleItemAcquired);
		}
		else
		{
			VB_LOG(Warning, "HUD: 이 플레이어의 인벤토리를 찾지 못했다 - 획득 알림이 안 뜬다(PlayerState 복제 전일 수 있다)");
		}
	}

	// 퀘스트 목표 줄 + 퀘스트 알림. 일지는 PlayerState 에 있다 - 인벤토리와 같이 한 번만 구독한다.
	if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget()))
	{
		QuestTracker = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuestTracker"));
		QuestTracker->SetVisibility(ESlateVisibility::Collapsed);
		QuestTracker->SetJustification(ETextJustify::Right);
		if (UCanvasPanelSlot* TrackerSlot = RootCanvas->AddChildToCanvas(QuestTracker))
		{
			TrackerSlot->SetAnchors(FAnchors(1.0f, 0.0f));
			TrackerSlot->SetAlignment(FVector2D(1.0f, 0.0f));
			TrackerSlot->SetPosition(FVector2D(-40.0f, 40.0f));
			TrackerSlot->SetAutoSize(true);
		}
		if (UVBQuestLogComponent* QuestLog = UVBQuestLogComponent::FindFor(Owner))
		{
			BoundQuestLog = QuestLog;
			QuestLogChangedHandle = QuestLog->OnQuestLogChanged.AddUObject(this, &UVBHUDWidget::RefreshQuestTracker);
			QuestNoticeHandle = QuestLog->OnQuestNotice.AddWeakLambda(this, [this](const UVBQuestDefinition* Quest, EVBQuestNotice Kind)
			{
				ShowQuestNotice(Quest, Kind == EVBQuestNotice::Started ? QuestStartedFormat
				                       : Kind == EVBQuestNotice::Completed ? QuestCompletedFormat : QuestAdvancedFormat);
			});
			RefreshQuestTracker();
		}
		else
		{
			VB_LOG(Warning, "HUD: 이 플레이어의 퀘스트 일지를 찾지 못했다 - 목표 줄·퀘스트 알림이 안 뜬다(PlayerState 복제 전일 수 있다)");
		}
	}

	// 레벨·경험치 줄 + 막대 + 레벨업 알림(.sw/remaining-features/b2-growth.md). 퀘스트 목표 줄과 같은 길 - 성장은 PlayerState 에 있어 한 번만 구독한다.
	if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget()); RootCanvas && !ProgressionLineFormat.IsEmpty())
	{
		UVerticalBox* ProgressionBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ProgressionBox"));
		ProgressionText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProgressionText"));
		ProgressionBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ProgressionBar"));
		ProgressionBox->AddChildToVerticalBox(ProgressionText);
		USizeBox* BarSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ProgressionBarSize"));
		BarSize->SetWidthOverride(ProgressionBarSize.X);
		BarSize->SetHeightOverride(ProgressionBarSize.Y);
		BarSize->AddChild(ProgressionBar);
		ProgressionBox->AddChildToVerticalBox(BarSize);
		if (UCanvasPanelSlot* BoxSlot = RootCanvas->AddChildToCanvas(ProgressionBox))
		{
			BoxSlot->SetAnchors(FAnchors(ProgressionAnchor.X, ProgressionAnchor.Y));
			BoxSlot->SetAlignment(ProgressionAlignment);
			BoxSlot->SetPosition(ProgressionOffset);
			BoxSlot->SetAutoSize(true);
		}
		if (UVBProgressionComponent* Progression = UVBProgressionComponent::FindFor(Owner))
		{
			BoundProgression = Progression;
			ProgressionChangedHandle = Progression->OnProgressionChanged.AddUObject(this, &UVBHUDWidget::RefreshProgression);
			RefreshProgression();
		}
		else
		{
			ProgressionBox->SetVisibility(ESlateVisibility::Collapsed);
			VB_LOG(Warning, "HUD: 이 플레이어의 성장 컴포넌트를 찾지 못했다 - 레벨 줄·레벨업 알림이 안 뜬다(PlayerState 복제 전일 수 있다)");
		}
	}

	// 퀵슬롯 칸 줄. 화면 밖 화살표와 같은 방식으로 붙이고, 못 붙이는 두 경우는 같은 이유로 Warning.
	if (QuickSlotBarWidgetClass)
	{
		if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget()))
		{
			QuickSlotBar = CreateWidget<UVBQuickSlotBarWidget>(GetOwningPlayer(), QuickSlotBarWidgetClass);
			if (QuickSlotBar)
			{
				if (UCanvasPanelSlot* BarSlot = RootCanvas->AddChildToCanvas(QuickSlotBar))
				{
					BarSlot->SetAnchors(FAnchors(QuickSlotBarAnchor.X, QuickSlotBarAnchor.Y));
					BarSlot->SetAlignment(QuickSlotBarAlignment);
					BarSlot->SetPosition(QuickSlotBarOffset);
					BarSlot->SetAutoSize(true);
				}
				// 인벤토리는 PlayerState 에 있다 - 폰이 바뀌어도 같은 것이라 한 번만 잇는다.
				UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(Owner);
				VB_CLOG(!Inventory, Warning, "HUD: 이 플레이어의 인벤토리를 찾지 못했다 - 퀵슬롯 칸 줄이 빈다(PlayerState 복제 전일 수 있다)");
				QuickSlotBar->SetInventory(Inventory);
				RefreshQuickSlotKeyTexts();
			}
		}
		else
		{
			VB_LOG(Warning, "HUD: 루트 위젯이 CanvasPanel 이 아니라 퀵슬롯 칸 줄을 붙일 수 없다 (%s)", *GetNameSafe(GetRootWidget()));
		}
	}
	else
	{
		VB_LOG(Warning, "HUD: QuickSlotBarWidgetClass 미할당 - 퀵슬롯 칸 줄 없음 (WBP_HUD 클래스 기본값)");
	}

	// 혼잣말 자막. 퀵슬롯 칸 줄과 같은 방식으로 붙이고 PC 의 줄 컴포넌트에 묶는다. 못 붙이면 줄은 로그·음성만 남는다 - 그래서 Warning.
	if (SubtitleWidgetClass)
	{
		if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget()))
		{
			Subtitle = CreateWidget<UVBSubtitleWidget>(GetOwningPlayer(), SubtitleWidgetClass);
			if (Subtitle)
			{
				if (UCanvasPanelSlot* SubtitleSlot = RootCanvas->AddChildToCanvas(Subtitle))
				{
					SubtitleSlot->SetAnchors(FAnchors(SubtitleAnchor.X, SubtitleAnchor.Y));
					SubtitleSlot->SetAlignment(SubtitleAlignment);
					SubtitleSlot->SetPosition(SubtitleOffset);
					SubtitleSlot->SetAutoSize(true);
				}
				UVBSpokenLineComponent* Lines = GetOwningPlayer() ? GetOwningPlayer()->FindComponentByClass<UVBSpokenLineComponent>() : nullptr;
				VB_CLOG(!Lines, Warning, "HUD: PC 에 UVBSpokenLineComponent 가 없다 - 자막이 아무것도 안 띄운다");
				Subtitle->BindTo(Lines);
			}
		}
		else
		{
			VB_LOG(Warning, "HUD: 루트 위젯이 CanvasPanel 이 아니라 자막을 붙일 수 없다 (%s)", *GetNameSafe(GetRootWidget()));
		}
	}
	else
	{
		VB_LOG(Warning, "HUD: SubtitleWidgetClass 미할당 - 혼잣말 자막 없음 (WBP_HUD 클래스 기본값)");
	}

	// 키 이름은 입력 장치가 바뀌면 달라진다(E -> 게임패드 버튼). 활성 IMC 가 장치마다 하나라 다시 물으면 된다.
	if (UVBInputDeviceManager* DeviceManager = OwnerCharacter->FindComponentByClass<UVBInputDeviceManager>())
	{
		DeviceManager->OnInputDeviceChanged.AddUniqueDynamic(this, &UVBHUDWidget::OnInputDeviceChanged);
		LastInputType = DeviceManager->GetCurrentDeviceType();
	}

	if (HUDFadeIn)
		PlayAnimation(HUDFadeIn);
}

void UVBHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateOffscreenThreat(MyGeometry);
	UpdateInteractPrompt();
	UpdateTouchControlsVisibility();
}

void UVBHUDWidget::OnInteractFocusChanged(UVBInteractableComponent* NewFocus)
{
	PromptTarget = NewFocus;
	ShownActionText = FText::GetEmpty();
	RefreshInteractKeyText();
}

void UVBHUDWidget::OnGameSaveCompleted(int32 SlotIndex, bool bSuccess)
{
	if (bSuccess)
	{
		PushNotice(EVBNoticeCategory::System, SaveCompletedText, TEXT("Save"));
	}
}

void UVBHUDWidget::HandleItemAcquired(const UVBItemDefinition* Item, int32 Count)
{
	UVBNoticeSubsystem* Notices = UVBNoticeSubsystem::Get(this);
	if (!Item || !Notices)
	{
		return;
	}
	// 같은 아이템을 연달아 얻으면 한 줄에 합친다(«붕대 x3 획득») - 줄마다 따로 띄우면 앞 줄이 밀려 사라진다.
	FVBNoticeRequest Request;
	Request.Category = EVBNoticeCategory::Item;
	Request.MergeKey = Item->GetFName();
	Request.Count = Count;
	Request.MakeText = [Format = ItemAcquiredFormat, Name = Item->DisplayName](int32 Total) { return FText::Format(Format, Name, Total); };
	Notices->Push(MoveTemp(Request));
}

void UVBHUDWidget::RefreshQuestTracker()
{
	if (!QuestTracker)
	{
		return;
	}
	const UVBQuestLogComponent* QuestLog = BoundQuestLog.Get();
	const FVBQuestLogEntry* Tracked = QuestLog ? QuestLog->GetTrackedEntry() : nullptr;
	const FText Text = Tracked ? VBQuestText::DescribeTracker(*Tracked) : FText::GetEmpty();
	QuestTracker->SetText(Text);
	QuestTracker->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UVBHUDWidget::ShowQuestNotice(const UVBQuestDefinition* Quest, const FText& Format)
{
	if (!Quest)
	{
		return;
	}
	if (!Format.IsEmpty())
	{
		PushNotice(EVBNoticeCategory::Quest, FText::Format(Format, Quest->Title));
	}
	// 알림은 주인 클라 RPC 로 오고 목록은 복제로 온다 - 어느 쪽이 먼저 와도 줄이 맞게 둘 다에서 다시 그린다.
	RefreshQuestTracker();
}

void UVBHUDWidget::PushNotice(EVBNoticeCategory Category, const FText& Text, FName MergeKey)
{
	if (UVBNoticeSubsystem* Notices = UVBNoticeSubsystem::Get(this))
	{
		Notices->PushText(Category, Text, MergeKey);
	}
}

void UVBHUDWidget::OnInputDeviceChanged(ECommonInputType NewDeviceType)
{
	LastInputType = NewDeviceType;
	RefreshInteractKeyText();
	RefreshQuickSlotKeyTexts();
}

void UVBHUDWidget::UpdateTouchControlsVisibility()
{
	if (!TouchControls)
	{
		return;
	}
	// 0 = 자동(마지막 입력이 터치면 보임) / 1 = 늘 보임 / 2 = 늘 숨김.
	// 강제 스위치가 있는 이유: 스트리밍으로 들어온 터치가 CommonInput 에 «터치» 로 잡히는지 확인 전이다(설계 미확인 항목).
	const int32 Mode = CVarTouchControls.GetValueOnGameThread();
	// 메뉴가 맨 위면 강제 표시(1)여도 감춘다 - 그때 누른 터치는 게임 입력 차단을 우회한다(HandleInputModeChanged).
	const bool  bShow = bGameInputActive && (Mode == 1 || (Mode == 0 && LastInputType == ECommonInputType::Touch));
	const ESlateVisibility Wanted = bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	if (TouchControls->GetVisibility() != Wanted)
	{
		TouchControls->SetVisibility(Wanted);
	}
}

void UVBHUDWidget::RefreshInteractKeyText()
{
	const UInputAction* Action = OwnerCharacter.IsValid() ? OwnerCharacter->GetInteractAction() : nullptr;
	bool bUnmapped = false;
	ShownKeyText = KeyTextFor(Action, bUnmapped);
	// 매핑 누락이다. 프롬프트는 뜨는데 무엇을 누를지 모르는 상태라 말한다.
	VB_CLOG(bUnmapped, Warning, "HUD: %s 에 매핑된 키가 지금 IMC 에 없다 - 프롬프트에 키 이름 없음", *GetNameSafe(Action));
	if (InteractPrompt && PromptTarget.IsValid())
	{
		InteractPrompt->SetPrompt(PromptTarget->GetActionText(), ShownKeyText);
	}
}

FText UVBHUDWidget::KeyTextFor(const UInputAction* Action, bool& bOutUnmapped) const
{
	bOutUnmapped = false;
	// 터치에는 누를 키가 없다 - 「E」를 띄우면 폰 화면에서 틀린 안내가 된다. 행동 이름만 보인다.
	if (LastInputType == ECommonInputType::Touch || !Action)
	{
		return FText::GetEmpty();
	}
	const APlayerController* PC = GetOwningPlayer();
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	const UEnhancedInputLocalPlayerSubsystem* Input =
		LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input)
	{
		return FText::GetEmpty();
	}
	// 활성 IMC 가 장치마다 하나뿐이라(UVBInputDeviceManager) 돌아오는 키는 지금 장치의 것이다.
	//   «키 없음» 으로 바꾼 줄은 Invalid 키로 남을 수 있어(설정 화면의 키 지우기) 유효한 키만 본다 - 「None」 을 띄우지 않는다.
	const TArray<FKey> Keys = Input->QueryKeysMappedToAction(Action);
	const FKey* Key = Keys.FindByPredicate([](const FKey& Candidate) { return Candidate.IsValid(); });
	bOutUnmapped = Key == nullptr;
	return Key ? Key->GetDisplayName(/*bLongDisplayName=*/false) : FText::GetEmpty();
}

void UVBHUDWidget::RefreshQuickSlotKeyTexts()
{
	if (!QuickSlotBar)
	{
		return;
	}
	const AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer());
	const int32 ActionCount = PC ? PC->GetQuickSlotActionCount() : 0;
	TArray<FText> Texts;
	int32 Unmapped = 0;
	for (int32 Index = 0; Index < ActionCount; ++Index)
	{
		bool bUnmapped = false;
		Texts.Add(KeyTextFor(PC->GetQuickSlotAction(Index), bUnmapped));
		Unmapped += bUnmapped ? 1 : 0;
	}
	// 키 1~4 매핑은 무기 바퀴와 같은 빌드에 들어간다(설계 2절) - 그 전에는 키 없는 칸이 정상이라 경고가 아니라 기록 한 줄이다.
	VB_CLOG(Unmapped > 0, Log, "HUD: 퀵슬롯 키 %d 중 %d 개는 지금 IMC 에 키가 없다 - 칸 줄에 키 이름 없음", ActionCount, Unmapped);
	QuickSlotBar->SetKeyTexts(Texts);
}

void UVBHUDWidget::UpdateInteractPrompt()
{
	if (!InteractPrompt || !InteractPromptSlot)
	{
		return;
	}

	auto Hide = [this]()
	{
		if (InteractPrompt->GetVisibility() != ESlateVisibility::Collapsed)
		{
			InteractPrompt->SetVisibility(ESlateVisibility::Collapsed);
		}
	};

	const UVBInteractableComponent* Target = PromptTarget.Get();
	APlayerController* PC = GetOwningPlayer();
	if (!Target || !PC)
	{
		Hide();
		return;
	}

	// 대상 위치를 따라간다. 카메라 뒤(투영 실패)면 숨긴다.
	FVector2D ScreenPosition;
	if (!PC->ProjectWorldLocationToScreen(Target->GetComponentLocation(), ScreenPosition))
	{
		Hide();
		return;
	}
	InteractPromptSlot->SetPosition(ScreenPosition / UWidgetLayoutLibrary::GetViewportScale(PC));

	const FText ActionText = Target->GetActionText();
	if (!ActionText.EqualTo(ShownActionText))
	{
		ShownActionText = ActionText;
		InteractPrompt->SetPrompt(ShownActionText, ShownKeyText);
	}

	InteractPrompt->SetHoldProgress(Interactor.IsValid() ? Interactor->GetHoldProgress() : -1.0f);

	if (InteractPrompt->GetVisibility() == ESlateVisibility::Collapsed)
	{
		InteractPrompt->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UVBHUDWidget::UpdateOffscreenThreat(const FGeometry& MyGeometry)
{
	if (!OffscreenThreat || !OffscreenThreatSlot)
	{
		return;
	}

	auto Hide = [this]()
	{
		if (OffscreenThreat->GetVisibility() != ESlateVisibility::Collapsed)
		{
			OffscreenThreat->SetVisibility(ESlateVisibility::Collapsed);
		}
	};

	APlayerController* PC = GetOwningPlayer();
	UWorld* World = GetWorld();
	UVBThreatTrackerSubsystem* Tracker = World ? World->GetSubsystem<UVBThreatTrackerSubsystem>() : nullptr;
	if (!PC || !Tracker)
	{
		Hide();
		return;
	}

	// 화면 밖인 첫 예고. 코디네이터 예고 게이트가 동시 예고를 막으므로 보통 하나뿐이다 - 화살표도 하나다.
	const FVBThreatEntry* Pick = nullptr;
	for (const FVBThreatEntry& Entry : Tracker->GetThreats())
	{
		if (Entry.Enemy.IsValid() && !UVBThreatTrackerSubsystem::IsOnScreen(PC, Entry.Enemy->GetActorLocation()))
		{
			Pick = &Entry;
			break;
		}
	}
	if (!Pick)
	{
		Hide();
		return;
	}

	// 방향 = 카메라 기준 나침반 사상. 앞 = 위, 오른쪽 = 오른쪽, 뒤 = 아래. 높이(Z)는 무시한다 -
	//   화면 밖 적은 «어느 쪽으로 돌아야 하나» 가 질문이고 그 답은 요 각도뿐이다.
	FVector  CamLoc;
	FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector Local = CamRot.UnrotateVector(Pick->Enemy->GetActorLocation() - CamLoc);
	FVector2D Dir(Local.Y, -Local.X);   // 화면 x = 오른쪽, 화면 y = 아래
	if (!Dir.Normalize())
	{
		Dir = FVector2D(0.0f, 1.0f);   // 바로 위·아래(퇴화)면 아래 가장자리
	}

	// 중심에서 Dir 방향으로 «여백 사각형» 과 만나는 점. 위젯 로컬 단위 = 슬롯 위치 단위(DPI 스케일 뒤)라 변환이 없다.
	const FVector2D ViewSize = MyGeometry.GetLocalSize();
	const FVector2D Center   = ViewSize * 0.5f;
	const FVector2D Half     = (Center - FVector2D(OffscreenThreatEdgeMargin, OffscreenThreatEdgeMargin)).ComponentMax(FVector2D::ZeroVector);
	const float Tx = FMath::Abs(Dir.X) > KINDA_SMALL_NUMBER ? Half.X / FMath::Abs(Dir.X) : TNumericLimits<float>::Max();
	const float Ty = FMath::Abs(Dir.Y) > KINDA_SMALL_NUMBER ? Half.Y / FMath::Abs(Dir.Y) : TNumericLimits<float>::Max();
	const FVector2D Pos = Center + Dir * FMath::Min(Tx, Ty);

	OffscreenThreatSlot->SetPosition(Pos);
	// 그림은 «위» 를 가리키게 저작돼 있다. atan2 의 0 은 오른쪽이라 90 을 더해 위를 0 으로 맞춘다.
	OffscreenThreat->SetRenderTransformAngle(FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)) + 90.0f);
	OffscreenThreat->SetThreatColor(Pick->Color);
	if (OffscreenThreat->GetVisibility() == ESlateVisibility::Collapsed)
	{
		OffscreenThreat->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}
