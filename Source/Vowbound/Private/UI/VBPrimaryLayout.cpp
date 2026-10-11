// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBPrimaryLayout.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "UI/VBScreenWidget.h"
#include "UI/VBUIManagerSubsystem.h"
#include "UI/VBUISettings.h"
#include "UI/Notice/VBNoticeFeedWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 층 위젯 이름. 코드가 만들 때와 WBP 파생에서 찾을 때 같은 이름을 쓴다.
	const FName LayerName_Game(TEXT("GameLayer"));
	const FName LayerName_GameMenu(TEXT("GameMenuLayer"));
	const FName LayerName_Menu(TEXT("MenuLayer"));
	const FName LayerName_Modal(TEXT("ModalLayer"));

	// 알림 줄을 층 넷 위에 겹으로 붙인다(설계 .sw/remaining-features/a2-notices.md). 활성화 화면 스택에 넣지 않는 이유:
	//   스택의 맨 위 화면이 되면 입력 설정·초점을 가져간다. 겹은 HitTestInvisible 이라 입력을 안 받고, Overlay 의 마지막 자식이라 맨 위에 그려진다.
	void AttachNoticeFeed(UUserWidget& Layout, UOverlay& Root)
	{
		const TSoftClassPtr<UVBNoticeFeedWidget>& ClassRef = GetDefault<UVBUISettings>()->NoticeFeedClass;
		if (ClassRef.IsNull())
		{
			VB_LOG(Warning, "UI 설정 NoticeFeedClass 가 비어 알림 줄이 안 보인다 (DefaultGame.ini - Vowbound UI)");
			return;
		}
		// 뿌리 위젯은 시작 때 한 번 만들어지고 이 클래스는 작다 - 동기 로드로 둔다.
		UClass* FeedClass = ClassRef.LoadSynchronous();
		UVBNoticeFeedWidget* Feed = FeedClass ? CreateWidget<UVBNoticeFeedWidget>(Layout.GetOwningPlayer(), FeedClass) : nullptr;
		if (!Feed)
		{
			VB_LOG(Warning, "알림 줄 위젯 %s 를 만들지 못했다", *ClassRef.ToString());
			return;
		}
		Feed->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOverlaySlot* FeedSlot = Root.AddChildToOverlay(Feed))
		{
			FeedSlot->SetHorizontalAlignment(HAlign_Fill);
			FeedSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
}

void UVBPrimaryLayout::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 뿌리는 화면 전체를 덮지만 클릭을 먹으면 안 된다 - 층이 비어 있을 때 아래 게임 뷰포트가 입력을 받아야 한다.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	BuildDefaultTreeIfEmpty();
	RegisterLayer(VBGameplayTags::UI_Layer_Game, LayerName_Game);
	RegisterLayer(VBGameplayTags::UI_Layer_GameMenu, LayerName_GameMenu);
	RegisterLayer(VBGameplayTags::UI_Layer_Menu, LayerName_Menu);
	RegisterLayer(VBGameplayTags::UI_Layer_Modal, LayerName_Modal);

	if (UOverlay* Root = WidgetTree ? Cast<UOverlay>(WidgetTree->RootWidget) : nullptr)
	{
		AttachNoticeFeed(*this, *Root);
	}
	else
	{
		VB_LOG(Warning, "뿌리 위젯 %s 의 루트가 Overlay 가 아니라 알림 줄을 못 붙인다", *GetName());
	}
}

void UVBPrimaryLayout::BuildDefaultTreeIfEmpty()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;
	for (const FName LayerName : { LayerName_Game, LayerName_GameMenu, LayerName_Menu, LayerName_Modal })
	{
		UCommonActivatableWidgetStack* Stack =
			WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(), LayerName);
		if (UOverlaySlot* LayerSlot = Root->AddChildToOverlay(Stack))
		{
			LayerSlot->SetHorizontalAlignment(HAlign_Fill);
			LayerSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
}

void UVBPrimaryLayout::NativeDestruct()
{
	// 전환 도중에 레이아웃이 내려가면(레벨 이동) 전환 끝 알림이 안 온다. 걸어 둔 입력 멈춤을 여기서 푼다 -
	//   안 풀면 다음 레벨에서 그 플레이어의 입력이 전부 막힌 채로 시작한다.
	UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this);
	for (const TPair<FGameplayTag, TObjectPtr<UCommonActivatableWidgetContainerBase>>& Pair : Layers)
	{
		if (Pair.Value)
		{
			Pair.Value->OnTransitioningChanged.RemoveAll(this);
			if (UI)
			{
				UI->SetInputSuspended(MakeTransitionReason(Pair.Value), false);
			}
		}
	}
	Layers.Reset();

	Super::NativeDestruct();
}

void UVBPrimaryLayout::RegisterLayer(FGameplayTag LayerTag, FName LayerWidgetName)
{
	UCommonActivatableWidgetContainerBase* Layer = WidgetTree
		? Cast<UCommonActivatableWidgetContainerBase>(WidgetTree->FindWidget(LayerWidgetName))
		: nullptr;
	if (!Layer)
	{
		VB_LOG(Error, "%s: 층 %s 의 위젯 %s 이 없다 - WBP 파생이면 그 이름의 CommonActivatableWidgetStack 을 두어야 한다",
		       *GetName(), *LayerTag.ToString(), *LayerWidgetName.ToString());
		return;
	}

	// 엔진 기본 전환 시간은 0.4초다(CommonActivatableWidgetContainer.h). Lyra 주석: 0 이 아니면 게임패드 초점이
	//   새 화면으로 제대로 안 넘어간다. 화면 등장 연출은 화면 자신의 애니메이션으로 한다.
	Layer->SetTransitionDuration(0.0f);
	Layer->OnTransitioningChanged.AddUObject(this, &UVBPrimaryLayout::HandleLayerTransitioning);
	Layers.Add(LayerTag, Layer);
}

void UVBPrimaryLayout::HandleLayerTransitioning(UCommonActivatableWidgetContainerBase* Layer, bool bIsTransitioning)
{
	// 전환 중에 들어온 입력은 사라지는 화면과 나타나는 화면 중 어디로 갈지 정해져 있지 않다. 전환이 끝날 때까지 막는다.
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		UI->SetInputSuspended(MakeTransitionReason(Layer), bIsTransitioning);
	}
}

FName UVBPrimaryLayout::MakeTransitionReason(const UCommonActivatableWidgetContainerBase* Layer) const
{
	// 층마다 고정 이름. 층 넷이 동시에 전환해도 서로의 멈춤을 풀지 않는다.
	return FName(*FString::Printf(TEXT("VBUILayerTransition_%s"), *GetNameSafe(Layer)));
}

UCommonActivatableWidgetContainerBase* UVBPrimaryLayout::GetLayer(FGameplayTag LayerTag) const
{
	const TObjectPtr<UCommonActivatableWidgetContainerBase>* Found = Layers.Find(LayerTag);
	return Found ? Found->Get() : nullptr;
}

UVBScreenWidget* UVBPrimaryLayout::PushToLayer(FGameplayTag LayerTag, TSubclassOf<UVBScreenWidget> ScreenClass)
{
	UCommonActivatableWidgetContainerBase* Layer = GetLayer(LayerTag);
	if (!Layer || !ScreenClass)
	{
		VB_LOG(Warning, "%s: 화면을 못 올린다 - 층 %s %s, 클래스 %s", *GetName(), *LayerTag.ToString(),
		       Layer ? TEXT("있음") : TEXT("없음"), *GetNameSafe(ScreenClass.Get()));
		return nullptr;
	}
	return Layer->AddWidget<UVBScreenWidget>(ScreenClass);
}

UCommonActivatableWidget* UVBPrimaryLayout::FindScreen(TSubclassOf<UVBScreenWidget> ScreenClass) const
{
	if (!ScreenClass)
	{
		return nullptr;
	}
	for (const TPair<FGameplayTag, TObjectPtr<UCommonActivatableWidgetContainerBase>>& Pair : Layers)
	{
		if (!Pair.Value)
		{
			continue;
		}
		for (UCommonActivatableWidget* Widget : Pair.Value->GetWidgetList())
		{
			if (Widget && Widget->IsA(ScreenClass))
			{
				return Widget;
			}
		}
	}
	return nullptr;
}

bool UVBPrimaryLayout::RemoveScreen(UCommonActivatableWidget& Screen)
{
	for (const TPair<FGameplayTag, TObjectPtr<UCommonActivatableWidgetContainerBase>>& Pair : Layers)
	{
		if (Pair.Value && Pair.Value->GetWidgetList().Contains(&Screen))
		{
			Pair.Value->RemoveWidget(Screen);
			return true;
		}
	}
	return false;
}
