// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBTouchControls.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Vowbound/Vowbound.h"

// ---- 공통 ----

UEnhancedInputLocalPlayerSubsystem* UVBTouchInputWidget::GetInputSubsystem() const
{
	const APlayerController* PC = GetOwningPlayer();
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

FReply UVBTouchInputWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	// 이미 다른 손가락을 붙잡고 있으면 두 번째 손가락은 받지 않는다 - 뒤의 위젯(시점 패드 등)으로 넘긴다.
	if (ActivePointerIndex != INDEX_NONE)
	{
		return FReply::Unhandled();
	}
	if (!InputAction)
	{
		// 배치 사고다. 막을 수 없으니 말한다 - 조용히 무시하면 「버튼이 안 먹는다」로만 보인다.
		VB_LOG(Warning, "터치 조작 %s 에 InputAction 이 없다", *GetName());
		return FReply::Unhandled();
	}
	ActivePointerIndex = InGestureEvent.GetPointerIndex();
	OnTouchBegin(InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition()));
	// 캡처는 이 이벤트의 손가락에만 걸린다. 손가락이 위젯 밖으로 나가도 이동·떼기가 여기로 온다.
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UVBTouchInputWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	if (InGestureEvent.GetPointerIndex() != ActivePointerIndex)
	{
		return FReply::Unhandled();
	}
	OnTouchMove(InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition()), InGestureEvent.GetCursorDelta());
	return FReply::Handled();
}

FReply UVBTouchInputWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	if (InGestureEvent.GetPointerIndex() != ActivePointerIndex)
	{
		return FReply::Unhandled();
	}
	EndActiveTouch();
	return FReply::Handled().ReleaseMouseCapture();
}

void UVBTouchInputWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// 창 포커스를 잃는 등으로 떼기 이벤트 없이 캡처가 풀린다. 그대로 두면 버튼이 눌린 채 남는다.
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
	if (CaptureLostEvent.PointerIndex == ActivePointerIndex)
	{
		EndActiveTouch();
	}
}

void UVBTouchInputWidget::NativeDestruct()
{
	EndActiveTouch();
	Super::NativeDestruct();
}

void UVBTouchInputWidget::EndActiveTouch()
{
	if (ActivePointerIndex == INDEX_NONE)
	{
		return;
	}
	ActivePointerIndex = INDEX_NONE;
	OnTouchEnd();
	// 파생이 멈추는 것을 잊어도 여기서 멈춘다. 계속 주입이 남으면 입력이 영영 눌린 상태다.
	StopInjection();
}

void UVBTouchInputWidget::StartInjection(const FInputActionValue& Value)
{
	if (UEnhancedInputLocalPlayerSubsystem* Input = GetInputSubsystem(); Input && InputAction)
	{
		Input->StartContinuousInputInjectionForAction(InputAction, Value, {}, {});
		bInjecting = true;
	}
}

void UVBTouchInputWidget::UpdateInjection(const FInputActionValue& Value)
{
	if (!bInjecting)
	{
		StartInjection(Value);
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Input = GetInputSubsystem(); Input && InputAction)
	{
		Input->UpdateValueOfContinuousInputInjectionForAction(InputAction, Value);
	}
}

void UVBTouchInputWidget::StopInjection()
{
	if (!bInjecting)
	{
		return;
	}
	bInjecting = false;
	if (UEnhancedInputLocalPlayerSubsystem* Input = GetInputSubsystem(); Input && InputAction)
	{
		Input->StopContinuousInputInjectionForAction(InputAction);
	}
}

void UVBTouchInputWidget::InjectOnce(const FInputActionValue& Value)
{
	if (UEnhancedInputLocalPlayerSubsystem* Input = GetInputSubsystem(); Input && InputAction)
	{
		Input->InjectInputForAction(InputAction, Value, {}, {});
	}
}

// ---- 스틱 ----

FVector2D UVBTouchStick::ComputeValue(const FVector2D& LocalPosition) const
{
	const FVector2D Center = GetCachedGeometry().GetLocalSize() * 0.5f;
	// 화면 좌표는 아래가 +Y 라 뒤집는다 - 위로 밀면 앞(+Y)이다.
	FVector2D Value((LocalPosition.X - Center.X) / Radius, -(LocalPosition.Y - Center.Y) / Radius);
	if (Value.SizeSquared() > 1.0f)
	{
		Value.Normalize();
	}
	return Value;
}

void UVBTouchStick::PlaceThumb(const FVector2D& Value)
{
	if (Thumb)
	{
		Thumb->SetRenderTranslation(FVector2D(Value.X, -Value.Y) * Radius);
	}
}

void UVBTouchStick::OnTouchBegin(const FVector2D& LocalPosition)
{
	const FVector2D Value = ComputeValue(LocalPosition);
	StartInjection(FInputActionValue(Value));
	PlaceThumb(Value);
}

void UVBTouchStick::OnTouchMove(const FVector2D& LocalPosition, const FVector2D& ScreenDelta)
{
	const FVector2D Value = ComputeValue(LocalPosition);
	UpdateInjection(FInputActionValue(Value));
	PlaceThumb(Value);
}

void UVBTouchStick::OnTouchEnd()
{
	PlaceThumb(FVector2D::ZeroVector);
}

// ---- 시점 패드 ----

void UVBTouchLookPad::OnTouchMove(const FVector2D& LocalPosition, const FVector2D& ScreenDelta)
{
	InjectOnce(FInputActionValue(ScreenDelta * LookScale));
}

// ---- 버튼 ----

void UVBTouchButton::OnTouchBegin(const FVector2D& LocalPosition)
{
	ReleasedOpacity = GetRenderOpacity();
	SetRenderOpacity(PressedOpacity);
	// 화면 열기(일시정지·가방·일지·스킬 트리)는 주입으로 안 열린다 - HUD 의 CommonUI 묶음은 액션이 아니라 그 액션에 매핑된 «키» 로
	//   발화하고(엔진 UIActionRouterTypes.cpp), 주입에는 키가 없다. 그래서 HUD 의 같은 처리 함수로 바로 보낸다.
	//   화면이 열리면 HUD 가 메뉴 모드에서 ReleaseAllTouches 를 불러 이 손가락을 놓는다(불투명도도 거기서 돌아온다).
	if (UIActionHandler.IsBound() && UIActionHandler.Execute(InputAction))
	{
		return;
	}
	StartInjection(FInputActionValue(true));
}

void UVBTouchButton::OnTouchEnd()
{
	SetRenderOpacity(ReleasedOpacity);
}

void UVBTouchButton::SetLabel(const FText& InLabel)
{
	if (Label)
	{
		Label->SetText(InLabel);
	}
}

// ---- 묶음 ----

void UVBTouchControlsWidget::ReleaseAllTouches()
{
	for (UVBTouchInputWidget* Control : CreatedControls)
	{
		if (Control)
		{
			Control->ReleaseTouch();
		}
	}
}

void UVBTouchControlsWidget::SetUIActionHandler(const FVBTouchUIActionHandler& InHandler)
{
	for (UVBTouchInputWidget* Control : CreatedControls)
	{
		if (UVBTouchButton* Button = Cast<UVBTouchButton>(Control))
		{
			Button->SetUIActionHandler(InHandler);
		}
	}
}

void UVBTouchControlsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget());
	if (!RootCanvas)
	{
		VB_LOG(Warning, "터치 조작 묶음 %s: 루트가 CanvasPanel 이 아니라 조작을 만들 수 없다", *GetName());
		return;
	}

	for (const FVBTouchControlSlot& Entry : Controls)
	{
		if (!Entry.WidgetClass || !Entry.InputAction)
		{
			VB_LOG(Warning, "터치 조작 묶음 %s: 클래스나 입력 액션이 빈 칸이 있어 건너뛴다", *GetName());
			continue;
		}
		UVBTouchInputWidget* Control = CreateWidget<UVBTouchInputWidget>(GetOwningPlayer(), Entry.WidgetClass);
		if (!Control)
		{
			continue;
		}
		Control->SetInputAction(Entry.InputAction);
		CreatedControls.Add(Control);
		if (UVBTouchButton* Button = Cast<UVBTouchButton>(Control))
		{
			Button->SetLabel(Entry.Label);
		}
		if (UCanvasPanelSlot* ControlSlot = RootCanvas->AddChildToCanvas(Control))
		{
			ControlSlot->SetAnchors(Entry.Anchors);
			ControlSlot->SetAlignment(Entry.Alignment);
			ControlSlot->SetPosition(Entry.Position);
			ControlSlot->SetSize(Entry.Size);
			ControlSlot->SetZOrder(Entry.ZOrder);
		}
		// 조작은 손가락을 받아야 한다. 묶음 자신은 HUD 가 SelfHitTestInvisible 로 둔다.
		Control->SetVisibility(ESlateVisibility::Visible);
	}
}
