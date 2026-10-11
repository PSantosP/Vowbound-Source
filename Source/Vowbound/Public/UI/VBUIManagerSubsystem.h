// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "Components/SlateWrapperTypes.h"   // ESlateVisibility
#include "Containers/Ticker.h"
#include "VBUIManagerSubsystem.generated.h"

class UCommonActivatableWidget;
class UVBPrimaryLayout;
class UVBScreenWidget;
struct FStreamableHandle;
class UCommonUIActionRouterBase;

struct FVBUIInputAvailability
{
	bool bAvailable = false;
	bool bBlocked = true;
};

/**
 * 화면을 여닫는 유일한 입구. 로컬 플레이어마다 하나. 설계 .sw/ui-foundation/design.md 2.1~2.3, 7절.
 *
 * 주인인 것:
 *   - 어떤 화면이 떠 있나: 뿌리 레이아웃(UVBPrimaryLayout)의 층 넷. 누구도 AddToViewport 로 화면을 띄우지 않는다.
 *   - 게임 정지: 정지를 원하는 활성 화면의 목록. 비었다가 차면 멈추고, 차 있다가 비면 푼다.
 *   - 화면 전환·로드 중 입력 멈춤: CommonInput 의 입력 종류 거르개에 이유 이름을 붙여 켜고 끈다.
 * 주인이 아닌 것:
 *   - 입력 모드·커서: CommonUI 액션 라우터가 맨 위 화면의 설정을 적용한다. 여기서 SetInputMode 를 부르지 않는다.
 *
 * 틱에 기대지 않는다. 공식 문서: 게임이 멈추면 CommonUI 가 쓰는 LocalPlayerSubsystem 들은 틱하지 않는다.
 *   정지 계산·화면 열기는 전부 이벤트(활성화·비활성화·로드 완료)로 돈다.
 */
UCLASS()
class VOWBOUND_API UVBUIManagerSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	FVBUIInputAvailability QueryInputAvailability(const APlayerController* Controller) const;
	FSimpleMulticastDelegate OnInputAvailabilityChanged;
	// 위젯·PC·로컬 플레이어 어느 쪽에서든 찾는다. 로컬 플레이어가 없으면(데디케이티드 서버·원격 PC) null.
	static UVBUIManagerSubsystem* Get(const UObject* WorldContextObject);

	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

	// 화면 태그(UI.Screen.*)로 연다. 층과 위젯은 UVBUISettings 의 표가 정한다. 로드는 비동기이고
	//   로드하는 동안 이 플레이어의 입력을 멈춘다. 이미 떠 있거나 여는 중이면 아무것도 하지 않는다(화면 태그당 하나).
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI", meta=(GameplayTagFilter="UI.Screen"))
	void PushScreen(FGameplayTag ScreenTag);

	// 그 화면을 내린다. 여는 중이면 로드를 취소한다. 화면이 스스로 닫을 때는 DeactivateWidget 이면 된다 -
	//   스택이 비활성화된 화면을 스스로 내린다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI", meta=(GameplayTagFilter="UI.Screen"))
	void PopScreen(FGameplayTag ScreenTag);

	UFUNCTION(BlueprintPure, Category="Vowbound|UI", meta=(GameplayTagFilter="UI.Screen"))
	bool IsScreenOpen(FGameplayTag ScreenTag) const;

	// 이 플레이어의 모든 입력 종류(키보드·마우스, 게임패드, 터치)를 이유 이름을 붙여 막거나 푼다.
	//   이유마다 따로 세므로 하나를 풀어도 다른 이유가 남아 있으면 계속 막힌다(엔진 FCommonInputPreprocessor::SetInputTypeFilter).
	//   이유 이름은 고정 문자열이어야 한다 - 엔진이 이유 표에서 항목을 지우지 않아 매번 새 이름을 쓰면 표가 자란다.
	void SetInputSuspended(FName Reason, bool bSuspended);

	// 한 층을 통째로 보이거나 감춘다. 층 안의 화면은 활성인 채로 남는다 - 입력 모드의 바닥(HUD)을 치우지 않고
	//   화면에서만 걷어낼 때 쓴다(사망 연출). 감춘 층은 그리지도 틱하지도 않는다(Collapsed).
	// 층 컨테이너만이 아니라 그 안의 화면 자신도 감춘다. CommonUI 는 활성 화면 중 paint layer 가 가장 큰 것을 맨 위로
	//   고르는데(UCommonUIActionRouterBase::Tick), 그 값을 읽는 FActivatableTreeNode::GetLastPaintLayer 는 화면 «자신의»
	//   가시성만 본다. 부모만 감추면 마지막으로 그려졌을 때의 큰 값이 남아, 나중에 뜬 화면(게임오버)이 영영 맨 위가 못 된다
	//   (2026-09-29 user PIE: 사망 후 게임오버 화면에 입력 모드·커서·초점이 안 걸렸다).
	void SetLayerHidden(FGameplayTag LayerTag, bool bHidden);

	UVBPrimaryLayout* GetLayout() const { return Layout; }

private:
	struct FInputPolicyScope
	{
		explicit FInputPolicyScope(UVBUIManagerSubsystem& InOwner);
		~FInputPolicyScope();
		UVBUIManagerSubsystem& Owner;
	};
	void NotifyInputPolicyChanged();
	bool NotifyAfterTreeUpdate(float DeltaSeconds);
	void ScheduleTreeUpdateNotification();
	TWeakObjectPtr<UCommonUIActionRouterBase> InputRouter;
	FDelegateHandle InputConfigChangedHandle;
	FDelegateHandle RouterActionsChangedHandle;
	FTSTicker::FDelegateHandle InputTreeTickerHandle;
	TSet<FName> SuspendedInputReasons;
	int32 InputPolicyUpdateDepth = 0;
	bool bDeinitializing = false;
	void CreateLayout(APlayerController* OwningController);
	void DestroyLayout();
	void ApplyUIInputMapping(bool bAdd);

	void FinishPush(FGameplayTag ScreenTag);
	void CancelPendingPush(FGameplayTag ScreenTag);
	static FName MakePushReason(FGameplayTag ScreenTag);

	void WatchScreen(UVBScreenWidget& Screen);
	void HandleScreenActivated(TWeakObjectPtr<UVBScreenWidget> Screen);
	void HandleScreenDeactivated(TWeakObjectPtr<UVBScreenWidget> Screen);
	void ApplyPause();

	UPROPERTY(Transient)
	TObjectPtr<UVBPrimaryLayout> Layout;

	TWeakObjectPtr<APlayerController> LayoutOwner;

	// 여는 중인 화면. 같은 화면을 두 번 열지 않고, PopScreen 이 로드를 취소하는 데 쓴다.
	TMap<FGameplayTag, TSharedPtr<FStreamableHandle>> PendingPushes;

	// 활성화 알림을 이미 걸어 둔 화면. 화면 위젯은 컨테이너가 재사용하므로 두 번 걸지 않으려고 센다.
	TSet<TWeakObjectPtr<UVBScreenWidget>> WatchedScreens;

	// 지금 정지를 원하는 활성 화면. 수가 아니라 목록인 이유: 같은 화면의 활성화 알림이 두 번 와도 한 번으로 친다.
	TSet<TWeakObjectPtr<UVBScreenWidget>> PausingScreens;

	// 마지막으로 적용한 정지 상태. 목록이 바뀔 때마다가 아니라 «원함» 이 뒤집힐 때만 SetGamePaused 를 부른다.
	bool bPauseApplied = false;

	// SetLayerHidden 이 감춘 화면과 감추기 전의 가시성. 다시 보일 때 그 값으로 돌린다 - 화면마다 원래 가시성이
	//   다르므로(HUD 는 SelfHitTestInvisible 등) 고정값으로 되돌리면 화면의 설정을 덮어쓴다.
	TMap<TWeakObjectPtr<UCommonActivatableWidget>, ESlateVisibility> HiddenScreenVisibility;
};
