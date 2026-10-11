// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayTagContainer.h"
#include "VBPrimaryLayout.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetContainerBase;
class UCommonActivatableWidgetStack;
class UVBScreenWidget;

/**
 * 로컬 플레이어마다 하나인 뿌리 위젯. 화면 층 넷(UI.Layer.Game/GameMenu/Menu/Modal)을 담는다.
 * 설계 .sw/ui-foundation/design.md 2.1. Lyra UPrimaryGameLayout 과 같은 자리지만 층을 코드가 만든다 -
 *   층이 넷으로 정해져 있고, BP 배선(RegisterLayer 호출)을 빠뜨리면 그 층이 조용히 없어지는 길을 없애려고.
 *   위젯 트리가 비어 있으면(이 C++ 클래스를 그대로 쓰면) Overlay 하나에 스택 넷을 만든다. 장식을 얹으려고
 *   WBP 파생을 만들면 그 트리에서 같은 이름(GameLayer/GameMenuLayer/MenuLayer/ModalLayer)의 스택을 찾는다.
 *
 * 화면을 여닫는 입구는 이 위젯이 아니라 UVBUIManagerSubsystem 이다. 여기 함수는 그 서브시스템만 부른다.
 */
UCLASS()
class VOWBOUND_API UVBPrimaryLayout : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	UCommonActivatableWidgetContainerBase* GetLayer(FGameplayTag LayerTag) const;

	// 층에 화면을 올린다. 층이 없으면 경고 후 null.
	UVBScreenWidget* PushToLayer(FGameplayTag LayerTag, TSubclassOf<UVBScreenWidget> ScreenClass);

	// 이 클래스의 화면이 어느 층에든 떠 있으면 그것을 돌려준다.
	UCommonActivatableWidget* FindScreen(TSubclassOf<UVBScreenWidget> ScreenClass) const;

	// 어느 층에 있든 그 화면을 내린다. 찾았으면 참.
	bool RemoveScreen(UCommonActivatableWidget& Screen);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

private:
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UCommonActivatableWidgetContainerBase>> Layers;

	// 트리가 비었으면 Overlay + 스택 넷을 만든다. 순서가 곧 그리는 순서다(뒤에 넣은 층이 위).
	void BuildDefaultTreeIfEmpty();
	void RegisterLayer(FGameplayTag LayerTag, FName LayerWidgetName);
	void HandleLayerTransitioning(UCommonActivatableWidgetContainerBase* Layer, bool bIsTransitioning);
	FName MakeTransitionReason(const UCommonActivatableWidgetContainerBase* Layer) const;
};
