// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBUIManagerSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "CommonActivatableWidget.h"
#include "CommonInputSubsystem.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Engine/AssetManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/StreamableManager.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/VBPrimaryLayout.h"
#include "UI/VBScreenWidget.h"
#include "UI/VBUISettings.h"
#include "Subsystems/SubsystemCollection.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 뿌리 레이아웃의 뷰포트 Z 순서. HUD 류를 AddToViewport 로 올리던 옛 위젯(Z 0)보다 위에 둔다.
	//   Lyra 의 레이아웃도 1000 이다(GameUIPolicy::AddLayoutToViewport).
	constexpr int32 PrimaryLayoutZOrder = 1000;

	const FVBScreenRoute* FindRoute(FGameplayTag ScreenTag)
	{
		return GetDefault<UVBUISettings>()->Screens.Find(ScreenTag);
	}
}

UVBUIManagerSubsystem::FInputPolicyScope::FInputPolicyScope(UVBUIManagerSubsystem& InOwner) : Owner(InOwner)
{
	if (++Owner.InputPolicyUpdateDepth == 1) Owner.OnInputAvailabilityChanged.Broadcast();
}

UVBUIManagerSubsystem::FInputPolicyScope::~FInputPolicyScope()
{
	if (--Owner.InputPolicyUpdateDepth == 0) Owner.NotifyInputPolicyChanged();
}

void UVBUIManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UCommonUIActionRouterBase>();
	Super::Initialize(Collection);
	InputRouter = GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>();
	if (UCommonUIActionRouterBase* Router = InputRouter.Get())
	{
		InputConfigChangedHandle = Router->OnActiveInputConfigChanged().AddWeakLambda(this,
			[this](const FUIInputConfig) { NotifyInputPolicyChanged(); });
		RouterActionsChangedHandle = Router->OnBoundActionsUpdated().AddUObject(this, &ThisClass::NotifyInputPolicyChanged);
	}
}

FVBUIInputAvailability UVBUIManagerSubsystem::QueryInputAvailability(const APlayerController* Controller) const
{
	FVBUIInputAvailability Result;
	UCommonUIActionRouterBase* Router = InputRouter.Get();
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	Result.bAvailable = !bDeinitializing && IsValid(Controller) && IsValid(World) && !World->bIsTearingDown
		&& Controller == LayoutOwner.Get() && IsValid(Layout) && Router
		&& Controller->GetLocalPlayer() == GetLocalPlayer()
		&& UCommonInputSubsystem::Get(GetLocalPlayer());
	if (Result.bAvailable)
	{
		Result.bBlocked = InputPolicyUpdateDepth > 0 || !PendingPushes.IsEmpty() || !SuspendedInputReasons.IsEmpty()
			|| !PausingScreens.IsEmpty() || World->IsPaused() || Router->IsPendingTreeChange()
			|| Router->GetActiveInputMode() == ECommonInputMode::Menu
			|| !Router->CanProcessNormalGameInput() || Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored();
	}
	return Result;
}

void UVBUIManagerSubsystem::NotifyInputPolicyChanged()
{
	if (InputPolicyUpdateDepth > 0) return;
	OnInputAvailabilityChanged.Broadcast();
	ScheduleTreeUpdateNotification();
}

void UVBUIManagerSubsystem::ScheduleTreeUpdateNotification()
{
	if (!bDeinitializing && !InputTreeTickerHandle.IsValid() && InputRouter.IsValid() && InputRouter->IsPendingTreeChange())
	{
		InputTreeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &ThisClass::NotifyAfterTreeUpdate));
	}
}

bool UVBUIManagerSubsystem::NotifyAfterTreeUpdate(float DeltaSeconds)
{
	if (bDeinitializing || !InputRouter.IsValid())
	{
		InputTreeTickerHandle.Reset();
		return false;
	}
	if (InputPolicyUpdateDepth > 0 || InputRouter->IsPendingTreeChange()) return true;
	OnInputAvailabilityChanged.Broadcast();
	// Retain this handle if a listener starts another tree rebuild during the notification.
	if (!bDeinitializing && InputRouter.IsValid() && (InputPolicyUpdateDepth > 0 || InputRouter->IsPendingTreeChange())) return true;
	InputTreeTickerHandle.Reset();
	return false;
}

UVBUIManagerSubsystem* UVBUIManagerSubsystem::Get(const UObject* WorldContextObject)
{
	const ULocalPlayer* LocalPlayer = Cast<ULocalPlayer>(WorldContextObject);
	if (!LocalPlayer)
	{
		if (const UUserWidget* Widget = Cast<UUserWidget>(WorldContextObject))
		{
			LocalPlayer = Widget->GetOwningLocalPlayer();
		}
		else if (const APlayerController* PC = Cast<APlayerController>(WorldContextObject))
		{
			LocalPlayer = PC->GetLocalPlayer();
		}
		else if (const APawn* Pawn = Cast<APawn>(WorldContextObject))
		{
			const APlayerController* PawnPC = Cast<APlayerController>(Pawn->GetController());
			LocalPlayer = PawnPC ? PawnPC->GetLocalPlayer() : nullptr;
		}
	}
	// 월드 전체에서 «첫 플레이어» 를 고르는 대체 경로는 두지 않는다 - 분할 화면에서 남의 화면을 연다.
	return LocalPlayer ? LocalPlayer->GetSubsystem<UVBUIManagerSubsystem>() : nullptr;
}

void UVBUIManagerSubsystem::Deinitialize()
{
	bDeinitializing = true;
	FInputPolicyScope InputScope(*this);
	FTSTicker::RemoveTicker(InputTreeTickerHandle);
	InputTreeTickerHandle.Reset();
	if (UCommonUIActionRouterBase* Router = InputRouter.Get())
	{
		Router->OnActiveInputConfigChanged().Remove(InputConfigChangedHandle);
		Router->OnBoundActionsUpdated().Remove(RouterActionsChangedHandle);
	}
	InputRouter.Reset();
	DestroyLayout();
	ApplyUIInputMapping(false);
	Super::Deinitialize();
}

void UVBUIManagerSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	FInputPolicyScope InputScope(*this);
	Super::PlayerControllerChanged(NewPlayerController);

	if (NewPlayerController && NewPlayerController == LayoutOwner.Get() && Layout)
	{
		return;
	}
	// 레벨이 바뀌면 PC 가 새로 붙는다. 옛 레이아웃은 옛 월드의 것이라 버리고 새로 만든다.
	DestroyLayout();
	if (NewPlayerController)
	{
		CreateLayout(NewPlayerController);
	}
}

void UVBUIManagerSubsystem::ApplyUIInputMapping(bool bAdd)
{
	UEnhancedInputLocalPlayerSubsystem* EnhancedInput = GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!EnhancedInput)
	{
		return;
	}
	const UVBUISettings* Settings = GetDefault<UVBUISettings>();
	UInputMappingContext* Mapping = Settings->UIInputMapping.LoadSynchronous();
	if (!Mapping)
	{
		VB_CLOG(bAdd, Warning, "[VBUI] Project Settings > Vowbound UI > UIInputMapping 이 비어 있다 - 메뉴의 확인·뒤로 키가 안 먹는다");
		return;
	}
	// 같은 IMC 를 다시 더하면 엔진이 우선순위만 갱신한다(중복으로 쌓이지 않는다). 레벨마다 불러도 된다.
	if (bAdd)
	{
		EnhancedInput->AddMappingContext(Mapping, Settings->UIInputMappingPriority);
	}
	else
	{
		EnhancedInput->RemoveMappingContext(Mapping);
	}
}

void UVBUIManagerSubsystem::SetLayerHidden(FGameplayTag LayerTag, bool bHidden)
{
	FInputPolicyScope InputScope(*this);
	UCommonActivatableWidgetContainerBase* Layer = Layout ? Layout->GetLayer(LayerTag) : nullptr;
	if (!Layer)
	{
		VB_LOG(Warning, "[VBUI] 층 %s 를 %s 수 없다 - 레이아웃 또는 층이 없다", *LayerTag.ToString(), bHidden ? TEXT("감출") : TEXT("보일"));
		return;
	}
	// 층 컨테이너는 나중에 이 층에 올라오는 화면까지 가리려고 감춘다. 화면 자신은 CommonUI 의 맨 위 판정에서
	//   빠지게 하려고 감춘다(헤더 주석 - 부모만 감추면 옛 paint layer 가 남는다).
	Layer->SetVisibility(bHidden ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	for (UCommonActivatableWidget* Screen : Layer->GetWidgetList())
	{
		if (!Screen)
		{
			continue;
		}
		if (bHidden)
		{
			if (!HiddenScreenVisibility.Contains(Screen))
			{
				HiddenScreenVisibility.Add(Screen, Screen->GetVisibility());
			}
			Screen->SetVisibility(ESlateVisibility::Collapsed);
		}
		else if (const ESlateVisibility* Previous = HiddenScreenVisibility.Find(Screen))
		{
			Screen->SetVisibility(*Previous);
			HiddenScreenVisibility.Remove(Screen);
		}
	}
	VB_LOG(Log, "[VBUI] 층 %s %s (화면 %d)", *LayerTag.ToString(), bHidden ? TEXT("감춤") : TEXT("보임"), Layer->GetWidgetList().Num());
}

void UVBUIManagerSubsystem::CreateLayout(APlayerController* OwningController)
{
	FInputPolicyScope InputScope(*this);
	ApplyUIInputMapping(true);

	const TSubclassOf<UVBPrimaryLayout> LayoutClass = GetDefault<UVBUISettings>()->PrimaryLayoutClass.LoadSynchronous();
	if (!LayoutClass)
	{
		VB_LOG(Warning, "[VBUI] Project Settings > Vowbound UI > PrimaryLayoutClass 가 비어 있다 - 화면 층이 없어 PushScreen 이 아무것도 못 연다");
		return;
	}

	Layout = CreateWidget<UVBPrimaryLayout>(OwningController, LayoutClass);
	if (!Layout)
	{
		VB_LOG(Error, "[VBUI] 뿌리 레이아웃 %s 생성 실패", *GetNameSafe(LayoutClass.Get()));
		return;
	}
	Layout->AddToPlayerScreen(PrimaryLayoutZOrder);
	LayoutOwner = OwningController;
	VB_LOG(Log, "[VBUI] 뿌리 레이아웃 %s 를 %s 에 올림", *GetNameSafe(LayoutClass.Get()), *GetNameSafe(OwningController));
}

void UVBUIManagerSubsystem::DestroyLayout()
{
	FInputPolicyScope InputScope(*this);
	TArray<FGameplayTag> Pending;
	PendingPushes.GetKeys(Pending);
	for (const FGameplayTag& ScreenTag : Pending)
	{
		CancelPendingPush(ScreenTag);
	}

	if (Layout)
	{
		Layout->RemoveFromParent();
		Layout = nullptr;
	}

	// 레이아웃과 함께 모든 화면이 사라진다. 정지를 원하던 화면도 없어졌으니 옛 월드의 정지를 푼다 -
	//   새 월드는 멈춘 채 시작하지 않지만, 레이아웃만 다시 만드는 경우(같은 월드에서 PC 교체)에는 옛 정지가 남는다.
	WatchedScreens.Reset();
	PausingScreens.Reset();
	HiddenScreenVisibility.Reset();
	ApplyPause();
	LayoutOwner.Reset();
}

void UVBUIManagerSubsystem::PushScreen(FGameplayTag ScreenTag)
{
	FInputPolicyScope InputScope(*this);
	const FVBScreenRoute* Route = FindRoute(ScreenTag);
	if (!Route || Route->ScreenClass.IsNull())
	{
		VB_LOG(Warning, "[VBUI] 화면 %s 의 층·위젯이 Project Settings > Vowbound UI > Screens 에 없다", *ScreenTag.ToString());
		return;
	}
	if (!Layout)
	{
		VB_LOG(Warning, "[VBUI] 화면 %s 를 못 연다 - 뿌리 레이아웃이 없다", *ScreenTag.ToString());
		return;
	}
	if (PendingPushes.Contains(ScreenTag) || IsScreenOpen(ScreenTag))
	{
		VB_LOG(Verbose, "[VBUI] 화면 %s 는 이미 떠 있거나 여는 중 - 무시", *ScreenTag.ToString());
		return;
	}

	// 로드하는 동안 들어온 입력은 아직 없는 화면으로 못 간다. 아래 화면이 그 입력을 먹지 않게 막는다.
	SetInputSuspended(MakePushReason(ScreenTag), true);

	TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Route->ScreenClass.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &UVBUIManagerSubsystem::FinishPush, ScreenTag));

	// 이미 메모리에 있으면 요청 안에서 곧바로 FinishPush 가 불린다. 그때는 여는 중 목록에 남기지 않는다.
	if (Handle.IsValid() && !Handle->HasLoadCompleted())
	{
		PendingPushes.Add(ScreenTag, Handle);
	}
}

void UVBUIManagerSubsystem::FinishPush(FGameplayTag ScreenTag)
{
	FInputPolicyScope InputScope(*this);
	PendingPushes.Remove(ScreenTag);
	SetInputSuspended(MakePushReason(ScreenTag), false);

	const FVBScreenRoute* Route = FindRoute(ScreenTag);
	const TSubclassOf<UVBScreenWidget> ScreenClass = Route ? Route->ScreenClass.Get() : nullptr;
	if (!Layout || !ScreenClass)
	{
		VB_LOG(Warning, "[VBUI] 화면 %s 로드 뒤 올리지 못함 - 레이아웃 %s, 클래스 %s", *ScreenTag.ToString(),
		       Layout ? TEXT("있음") : TEXT("없음"), ScreenClass ? TEXT("있음") : TEXT("로드 실패"));
		return;
	}

	UVBScreenWidget* Screen = Layout->PushToLayer(Route->Layer, ScreenClass);
	if (!Screen)
	{
		return;
	}
	WatchScreen(*Screen);
	// 스택에 올라간 화면은 AddWidget 안에서 이미 활성화됐다(5.5~ Push 가 활성화까지 한다). 그 알림은
	//   WatchScreen 이 걸리기 전에 지나갔으므로 여기서 한 번 직접 반영한다.
	if (Screen->IsActivated())
	{
		HandleScreenActivated(Screen);
	}
	VB_LOG(Log, "[VBUI] Push %s -> %s (%s)", *ScreenTag.ToString(), *Route->Layer.ToString(), *GetNameSafe(Screen));
}

void UVBUIManagerSubsystem::PopScreen(FGameplayTag ScreenTag)
{
	FInputPolicyScope InputScope(*this);
	if (PendingPushes.Contains(ScreenTag))
	{
		CancelPendingPush(ScreenTag);
		return;
	}

	const FVBScreenRoute* Route = FindRoute(ScreenTag);
	const TSubclassOf<UVBScreenWidget> ScreenClass = Route ? Route->ScreenClass.Get() : nullptr;
	if (!Layout || !ScreenClass)
	{
		return;
	}
	if (UCommonActivatableWidget* Screen = Layout->FindScreen(ScreenClass))
	{
		Layout->RemoveScreen(*Screen);
		VB_LOG(Log, "[VBUI] Pop %s", *ScreenTag.ToString());
	}
}

bool UVBUIManagerSubsystem::IsScreenOpen(FGameplayTag ScreenTag) const
{
	const FVBScreenRoute* Route = FindRoute(ScreenTag);
	// 클래스가 아직 로드 안 됐으면(Get 이 null) 그 화면의 인스턴스도 있을 수 없다.
	const TSubclassOf<UVBScreenWidget> ScreenClass = Route ? Route->ScreenClass.Get() : nullptr;
	return Layout && ScreenClass && Layout->FindScreen(ScreenClass) != nullptr;
}

void UVBUIManagerSubsystem::CancelPendingPush(FGameplayTag ScreenTag)
{
	FInputPolicyScope InputScope(*this);
	TSharedPtr<FStreamableHandle> Handle;
	if (PendingPushes.RemoveAndCopyValue(ScreenTag, Handle) && Handle.IsValid())
	{
		Handle->CancelHandle();
	}
	SetInputSuspended(MakePushReason(ScreenTag), false);
}

FName UVBUIManagerSubsystem::MakePushReason(FGameplayTag ScreenTag)
{
	// 화면마다 고정 이름. 엔진 이유 표가 항목을 지우지 않으므로 화면 수만큼만 자란다.
	return FName(*FString::Printf(TEXT("VBUIPush_%s"), *ScreenTag.ToString()));
}

void UVBUIManagerSubsystem::SetInputSuspended(FName Reason, bool bSuspended)
{
	FInputPolicyScope InputScope(*this);
	if (bSuspended) SuspendedInputReasons.Add(Reason);
	else SuspendedInputReasons.Remove(Reason);
	UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetLocalPlayer());
	if (!CommonInput)
	{
		return;
	}
	CommonInput->SetInputTypeFilter(ECommonInputType::MouseAndKeyboard, Reason, bSuspended);
	CommonInput->SetInputTypeFilter(ECommonInputType::Gamepad, Reason, bSuspended);
	CommonInput->SetInputTypeFilter(ECommonInputType::Touch, Reason, bSuspended);
	VB_LOG(Verbose, "[VBUI] 입력 %s: %s", bSuspended ? TEXT("멈춤") : TEXT("풂"), *Reason.ToString());
}

void UVBUIManagerSubsystem::WatchScreen(UVBScreenWidget& Screen)
{
	const TWeakObjectPtr<UVBScreenWidget> WeakScreen(&Screen);
	if (WatchedScreens.Contains(WeakScreen))
	{
		return;
	}
	WatchedScreens.Add(WeakScreen);
	Screen.OnActivated().AddUObject(this, &UVBUIManagerSubsystem::HandleScreenActivated, WeakScreen);
	Screen.OnDeactivated().AddUObject(this, &UVBUIManagerSubsystem::HandleScreenDeactivated, WeakScreen);
}

void UVBUIManagerSubsystem::HandleScreenActivated(TWeakObjectPtr<UVBScreenWidget> Screen)
{
	FInputPolicyScope InputScope(*this);
	if (Screen.IsValid() && Screen->PausesGame())
	{
		PausingScreens.Add(Screen);
		ApplyPause();
	}
}

void UVBUIManagerSubsystem::HandleScreenDeactivated(TWeakObjectPtr<UVBScreenWidget> Screen)
{
	FInputPolicyScope InputScope(*this);
	if (PausingScreens.Remove(Screen) > 0)
	{
		ApplyPause();
	}
}

void UVBUIManagerSubsystem::ApplyPause()
{
	const bool bWantPause = PausingScreens.Num() > 0;
	if (bWantPause == bPauseApplied)
	{
		return;
	}
	APlayerController* PC = LayoutOwner.Get();
	if (!PC)
	{
		// PC 가 없으면 멈출 월드도 없다. 상태만 맞춰 둔다 - 다음 레이아웃은 정지 없이 시작한다.
		bPauseApplied = false;
		return;
	}
	// SP/리슨 호스트만 실제로 멈춘다(엔진 APlayerController::SetPause 가 순수 클라에서 조기 반환 - 15_PauseMenu 지도).
	UGameplayStatics::SetGamePaused(PC, bWantPause);
	bPauseApplied = bWantPause;
	VB_LOG(Log, "[VBUI] 게임 %s (정지를 원하는 화면 %d)", bWantPause ? TEXT("정지") : TEXT("재개"), PausingScreens.Num());
}
