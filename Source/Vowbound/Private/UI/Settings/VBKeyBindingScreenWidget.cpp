// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/Settings/VBKeyBindingScreenWidget.h"

#include "CommonInputSubsystem.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Input/VBInputUserSettings.h"
#include "UI/VBListRowWidget.h"
#include "Vowbound/Vowbound.h"

#define LOCTEXT_NAMESPACE "VBKeyBindingScreen"

namespace
{
	/**
	 * 키를 기다리는 동안 Slate 의 모든 입력을 먼저 본다. 누름은 삼키고 뗄 때 키를 화면에 넘긴다.
	 * 뗄 때 정하는 이유: 누름에서 정하면 그 키의 뗌이 화면 뒤로 흘러 버튼을 누르거나 메뉴를 움직인다.
	 * 마우스 이동·스틱은 받지 않지만 삼킨다 - 기다리는 동안 초점이 다른 줄로 옮겨 가지 않게.
	 */
	class FVBKeyCaptureProcessor : public IInputProcessor
	{
	public:
		explicit FVBKeyCaptureProcessor(UVBKeyBindingScreenWidget& InScreen)
			: Screen(&InScreen)
		{
		}

		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
		{
		}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			return true;
		}

		virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			Capture(InKeyEvent.GetKey());
			return true;
		}

		virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
		{
			return true;
		}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			return true;
		}

		virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			Capture(MouseEvent.GetEffectingButton());
			return true;
		}

		virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			return true;
		}

		// 휠은 키 이벤트가 아니라 굴림으로 온다. 대상 바꾸기가 휠 위·아래라 받아야 한다.
		virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override
		{
			if (InWheelEvent.GetWheelDelta() != 0.f)
			{
				Capture(InWheelEvent.GetWheelDelta() > 0.f ? EKeys::MouseScrollUp : EKeys::MouseScrollDown);
			}
			return true;
		}

		virtual const TCHAR* GetDebugName() const override
		{
			return TEXT("VBKeyCapture");
		}

	private:
		void Capture(const FKey& Key)
		{
			if (UVBKeyBindingScreenWidget* Target = Screen.Get())
			{
				Target->HandleCapturedKey(Key);
			}
		}

		TWeakObjectPtr<UVBKeyBindingScreenWidget> Screen;
	};
}

UVBKeyBindingScreenWidget::UVBKeyBindingScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 뒤로 가기를 받아 기다림·확인을 먼저 거두고, 고르는 중이면 화면을 닫는다(NativeOnHandleBackAction).
	bIsBackHandler = true;
	bRequireCloseButton = true;
	InputMode = EVBScreenInputMode::Menu;
	WaitingKeyboardText = LOCTEXT("WaitingKeyboard", "새 키를 누른다. Esc 는 그대로 둔다.");
	WaitingGamepadText = LOCTEXT("WaitingGamepad", "새 버튼을 누른다. 지금 버튼을 누르면 그대로 둔다.");
	CannotBindFormat = LOCTEXT("CannotBind", "{Key} 는 여기에 쓸 수 없다.");
	ConflictFormat = LOCTEXT("Conflict", "{Key} 는 «{Actions}» 에 쓰는 키다. 바꾸면 그쪽은 키가 없어진다. 바꿀까?");
	ReboundFormat = LOCTEXT("Rebound", "{Action} = {Key}");
	RebindFailedText = LOCTEXT("RebindFailed", "바꾸지 못했다.");
	ResetDoneText = LOCTEXT("ResetDone", "기본값으로 되돌렸다.");
	UnboundKeyText = LOCTEXT("Unbound", "없음");
	NoSettingsText = LOCTEXT("NoSettings", "키 설정을 읽지 못했다.");
}

void UVBKeyBindingScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 필수 묶음들은 WBP 가 컴파일됐으면 비어 있지 않다.
	BtnKeyboardTab->OnClicked.AddUniqueDynamic(this, &UVBKeyBindingScreenWidget::HandleKeyboardTabClicked);
	BtnGamepadTab->OnClicked.AddUniqueDynamic(this, &UVBKeyBindingScreenWidget::HandleGamepadTabClicked);
	BtnConfirmYes->OnClicked.AddUniqueDynamic(this, &UVBKeyBindingScreenWidget::HandleConfirmYesClicked);
	BtnConfirmNo->OnClicked.AddUniqueDynamic(this, &UVBKeyBindingScreenWidget::HandleConfirmNoClicked);
	if (BtnReset)
	{
		BtnReset->OnClicked.AddUniqueDynamic(this, &UVBKeyBindingScreenWidget::HandleResetClicked);
	}
}

void UVBKeyBindingScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	const UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetOwningLocalPlayer());
	const bool bUsingGamepad = CommonInput && CommonInput->GetCurrentInputType() == ECommonInputType::Gamepad;
	ShowDevice(bUsingGamepad ? EVBKeyDevice::Gamepad : EVBKeyDevice::KeyboardMouse);
}

void UVBKeyBindingScreenWidget::NativeOnDeactivated()
{
	StopCapture();
	State = EState::Browsing;
	Super::NativeOnDeactivated();
}

void UVBKeyBindingScreenWidget::NativeDestruct()
{
	// 화면 위젯은 재사용되지만 파괴될 때도 Slate 에 전처리기가 남지 않게 한다 - 남으면 모든 입력을 삼킨다.
	StopCapture();
	Super::NativeDestruct();
}

bool UVBKeyBindingScreenWidget::NativeOnHandleBackAction()
{
	if (State != EState::Browsing)
	{
		StopCapture();
		EnterBrowsing(FText::GetEmpty());
		return true;
	}
	return Super::NativeOnHandleBackAction();
}

UWidget* UVBKeyBindingScreenWidget::NativeGetDesiredFocusTarget() const
{
	if (State == EState::Confirming)
	{
		return BtnConfirmNo;
	}
	if (EditingRow != INDEX_NONE && EntryBox->GetChildrenCount() > EditingRow)
	{
		if (const UVBListRowWidget* Row = Cast<UVBListRowWidget>(EntryBox->GetChildAt(EditingRow)))
		{
			return Row->GetButton();
		}
	}
	if (EntryBox->GetChildrenCount() > 0)
	{
		if (const UVBListRowWidget* Row = Cast<UVBListRowWidget>(EntryBox->GetChildAt(0)))
		{
			return Row->GetButton();
		}
	}
	return BtnClose;
}

UVBInputUserSettings* UVBKeyBindingScreenWidget::GetSettings() const
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	return Input ? Input->GetUserSettings<UVBInputUserSettings>() : nullptr;
}

void UVBKeyBindingScreenWidget::ShowDevice(EVBKeyDevice Device)
{
	StopCapture();
	ShownDevice = Device;
	BtnKeyboardTab->SetIsEnabled(Device != EVBKeyDevice::KeyboardMouse);
	BtnGamepadTab->SetIsEnabled(Device != EVBKeyDevice::Gamepad);
	EditingRow = INDEX_NONE;
	EnterBrowsing(FText::GetEmpty());
}

void UVBKeyBindingScreenWidget::Rebuild()
{
	EntryBox->ClearChildren();
	Rows.Reset();

	const UVBInputUserSettings* Settings = GetSettings();
	if (!Settings)
	{
		VB_LOG(Warning, "키 다시 지정 화면: 사용자 설정이 UVBInputUserSettings 가 아니다 - DefaultInput.ini 의 UserSettingsClass 를 본다");
		TxtStatus->SetText(NoSettingsText);
		return;
	}
	Rows = Settings->GetRows(ShownDevice);
	VBKeyBinding::OrderByCategory(Rows);
	if (Rows.Num() > 0 && !EntryWidgetClass)
	{
		VB_LOG(Warning, "키 다시 지정 화면: EntryWidgetClass 미지정 - 목록을 그릴 수 없다(WBP 클래스 기본값)");
		return;
	}
	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		const FVBKeyBindingRow& Row = Rows[RowIndex];
		UVBListRowWidget* Entry = CreateWidget<UVBListRowWidget>(this, EntryWidgetClass);
		Entry->Setup(RowIndex, RowName(Row.MappingName), KeyText(Row.Key), /*bEnabled=*/true);
		Entry->OnRowClicked.BindUObject(this, &UVBKeyBindingScreenWidget::SelectRow);
		EntryBox->AddChild(Entry);
	}
}

void UVBKeyBindingScreenWidget::SelectRow(int32 RowIndex)
{
	if (State != EState::Browsing || !Rows.IsValidIndex(RowIndex))
	{
		return;
	}
	EditingRow = RowIndex;
	State = EState::Waiting;
	TxtStatus->SetText(ShownDevice == EVBKeyDevice::Gamepad ? WaitingGamepadText : WaitingKeyboardText);
	StartCapture();
}

void UVBKeyBindingScreenWidget::HandleCapturedKey(const FKey& CapturedKey)
{
	if (State != EState::Waiting || !Rows.IsValidIndex(EditingRow))
	{
		return;
	}
	StopCapture();
	// 패드 트리거는 버튼 키로 들어오고 줄에는 축 키가 있다 - 같은 버튼으로 견주게 모은다.
	const FKey Key = VBKeyBinding::NormalizeKey(CapturedKey);
	const FVBKeyBindingRow& Row = Rows[EditingRow];

	// 키보드의 취소. Esc 는 받을 수 없는 키라(CanBind) 겹칠 일이 없다.
	if (Key == EKeys::Escape || Key == Row.Key)
	{
		EnterBrowsing(FText::GetEmpty());
		return;
	}
	if (!VBKeyBinding::CanBind(Key, ShownDevice))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Key"), KeyText(Key));
		EnterBrowsing(FText::Format(CannotBindFormat, Args));
		return;
	}
	const TArray<FName> Conflicts = VBKeyBinding::FindConflicts(Rows, Row.MappingName, ShownDevice, Key);
	if (Conflicts.Num() == 0)
	{
		ApplyRebind(Row.MappingName, Key);
		return;
	}

	TArray<FText> ConflictNames;
	for (const FName Conflict : Conflicts)
	{
		ConflictNames.Add(RowName(Conflict));
	}
	FFormatNamedArguments Args;
	Args.Add(TEXT("Key"), KeyText(Key));
	Args.Add(TEXT("Actions"), FText::Join(LOCTEXT("ConflictJoin", ", "), ConflictNames));
	TxtConfirm->SetText(FText::Format(ConflictFormat, Args));
	// 기다림 안내(«새 키를 누른다»)를 지운다 - 확인 칸이 떠 있는데 위에서 아직 키를 기다린다고 말하면 무엇에 답할지 헷갈린다(10-02 user 스크린샷).
	TxtStatus->SetText(FText::GetEmpty());
	PendingKey = Key;
	State = EState::Confirming;
	ConfirmBox->SetVisibility(ESlateVisibility::Visible);
	RequestRefreshFocus();
}

void UVBKeyBindingScreenWidget::ApplyRebind(FName MappingName, const FKey& NewKey)
{
	UVBInputUserSettings* Settings = GetSettings();
	TArray<FName> Cleared;
	if (!Settings || !Settings->RebindKey(MappingName, ShownDevice, NewKey, Cleared))
	{
		// RebindKey 가 실패 까닭을 로그에 남긴다. 화면에는 바뀌지 않았다는 것만 보인다.
		EnterBrowsing(Settings ? RebindFailedText : NoSettingsText);
		return;
	}
	SaveSettings();
	FFormatNamedArguments Args;
	Args.Add(TEXT("Action"), RowName(MappingName));
	Args.Add(TEXT("Key"), KeyText(NewKey));
	EnterBrowsing(FText::Format(ReboundFormat, Args));
}

void UVBKeyBindingScreenWidget::EnterBrowsing(const FText& Status)
{
	State = EState::Browsing;
	PendingKey = FKey();
	ConfirmBox->SetVisibility(ESlateVisibility::Collapsed);
	TxtStatus->SetText(Status);
	// 줄의 옆 글(지금 키)과 겹침 판정이 같은 사용자 설정을 보게 매번 다시 읽는다. 다시 지으면 초점이 사라지므로 고르던 줄로 되돌린다.
	Rebuild();
	RequestRefreshFocus();
}

void UVBKeyBindingScreenWidget::StartCapture()
{
	if (!CaptureProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		CaptureProcessor = MakeShared<FVBKeyCaptureProcessor>(*this);
		// 맨 앞(0번) - CommonUI·CommonInput 의 전처리기가 패드 확인 키나 뒤로 가기를 먼저 먹지 않게.
		FSlateApplication::Get().RegisterInputPreProcessor(CaptureProcessor, 0);
	}
}

void UVBKeyBindingScreenWidget::StopCapture()
{
	if (CaptureProcessor.IsValid())
	{
		// 전처리기 자기 콜백 안에서 불려도 된다 - Slate 가 순회 중의 제거를 미뤘다가 지운다(FInputPreProcessorsHelper).
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(CaptureProcessor);
		}
		CaptureProcessor.Reset();
	}
}

void UVBKeyBindingScreenWidget::SaveSettings()
{
	// 키 배치는 진행이 아니라 기계·사람의 설정이라 우리 저장 슬롯이 아니라 엔진의 사용자 설정 저장이 주인이다(설계 3절).
	if (UVBInputUserSettings* Settings = GetSettings())
	{
		Settings->AsyncSaveSettings();
	}
}

FText UVBKeyBindingScreenWidget::KeyText(const FKey& Key) const
{
	// HUD 의 상호작용 프롬프트와 같은 짧은 이름이다(UVBHUDWidget).
	return Key.IsValid() ? Key.GetDisplayName(/*bLongDisplayName=*/false) : UnboundKeyText;
}

FText UVBKeyBindingScreenWidget::RowName(FName MappingName) const
{
	for (const FVBKeyBindingRow& Row : Rows)
	{
		if (Row.MappingName == MappingName)
		{
			// 표시 이름이 비면 줄 이름이라도 보인다 - 빈 줄은 무엇을 바꾸는지 모른다. 비지 않게 하는 것은 IMC 이름표 시험의 몫이다.
			return Row.DisplayName.IsEmpty() ? FText::FromName(MappingName) : Row.DisplayName;
		}
	}
	return FText::FromName(MappingName);
}

void UVBKeyBindingScreenWidget::HandleKeyboardTabClicked()
{
	ShowDevice(EVBKeyDevice::KeyboardMouse);
}

void UVBKeyBindingScreenWidget::HandleGamepadTabClicked()
{
	ShowDevice(EVBKeyDevice::Gamepad);
}

void UVBKeyBindingScreenWidget::HandleResetClicked()
{
	if (State != EState::Browsing)
	{
		return;
	}
	UVBInputUserSettings* Settings = GetSettings();
	if (!Settings)
	{
		EnterBrowsing(NoSettingsText);
		return;
	}
	// 보이는 탭의 장치만 되돌린다 - 안 보이는 장치의 키까지 말없이 바뀌지 않게.
	Settings->ResetDeviceToDefault(ShownDevice);
	SaveSettings();
	EditingRow = INDEX_NONE;
	EnterBrowsing(ResetDoneText);
}

void UVBKeyBindingScreenWidget::NativeOnCloseClicked()
{
	StopCapture();
	DeactivateWidget();
}

void UVBKeyBindingScreenWidget::HandleConfirmYesClicked()
{
	if (State != EState::Confirming || !Rows.IsValidIndex(EditingRow))
	{
		return;
	}
	ApplyRebind(Rows[EditingRow].MappingName, PendingKey);
}

void UVBKeyBindingScreenWidget::HandleConfirmNoClicked()
{
	EnterBrowsing(FText::GetEmpty());
}

#undef LOCTEXT_NAMESPACE
