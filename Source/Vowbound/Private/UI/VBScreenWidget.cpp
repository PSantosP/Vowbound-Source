// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBScreenWidget.h"

#include "Input/UIActionBindingHandle.h"   // FUIInputConfig
#include "CommonInputModeTypes.h"          // ECommonInputMode
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Vowbound/Vowbound.h"

#if WITH_EDITOR
#include "Editor/WidgetCompilerLog.h"
#endif

TOptional<FUIInputConfig> UVBScreenWidget::GetDesiredInputConfig() const
{
	switch (InputMode)
	{
	case EVBScreenInputMode::Game:
		return FUIInputConfig(ECommonInputMode::Game, GameMouseCaptureMode);
	case EVBScreenInputMode::GameAndMenu:
		return FUIInputConfig(ECommonInputMode::All, GameMouseCaptureMode);
	case EVBScreenInputMode::Menu:
		return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
	case EVBScreenInputMode::Default:
	default:
		// 부모 구현은 BP 의 GetDesiredInputConfig 를 대체 경로로 쓴다. 그 길을 막지 않는다.
		return Super::GetDesiredInputConfig();
	}
}

void UVBScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnClose)
	{
		BtnClose->OnClicked.AddUniqueDynamic(this, &UVBScreenWidget::HandleCloseClicked);
	}
}

void UVBScreenWidget::HandleCloseClicked()
{
	NativeOnCloseClicked();
}

UWidget* UVBScreenWidget::NativeGetDesiredFocusTarget() const
{
	if (!DefaultFocusWidgetName.IsNone() && WidgetTree)
	{
		if (UWidget* Target = WidgetTree->FindWidget(DefaultFocusWidgetName))
		{
			return Target;
		}
		VB_LOG(Warning, "%s: 초점 위젯 %s 를 위젯 트리에서 못 찾았다 - 게임패드 초점이 없다",
		       *GetName(), *DefaultFocusWidgetName.ToString());
	}
	return Super::NativeGetDesiredFocusTarget();
}

#if WITH_EDITOR
void UVBScreenWidget::ValidateCompiledWidgetTree(const UWidgetTree& BlueprintWidgetTree, IWidgetCompilerLog& CompileLog) const
{
	Super::ValidateCompiledWidgetTree(BlueprintWidgetTree, CompileLog);

	const bool bTakesMenuInput = InputMode == EVBScreenInputMode::Menu || InputMode == EVBScreenInputMode::GameAndMenu;
	const bool bHasFocusTarget = ProvidesNativeFocusTarget()
		|| GetClass()->IsFunctionImplementedInScript(GET_FUNCTION_NAME_CHECKED(UVBScreenWidget, BP_GetDesiredFocusTarget));
	if (bTakesMenuInput && !bHasFocusTarget)
	{
		CompileLog.Warning(FText::FromString(TEXT(
			"메뉴 입력을 받는 화면인데 GetDesiredFocusTarget 을 구현하지 않았다 - 게임패드로 이 화면의 아무것도 고를 수 없다. "
			"BP 에서 Get Desired Focus Target 을 구현하거나, C++ 파생이면 NativeGetDesiredFocusTarget 과 ProvidesNativeFocusTarget 을 재정의한다.")));
	}

	// 닫기 버튼이 필수인 화면: 버튼이 없거나, 있어도 기본 닫기(뒤로 가기)를 안 받으면 닫을 길이 없다 - 컴파일을 막는다.
	if (bRequireCloseButton)
	{
		if (!Cast<UButton>(BlueprintWidgetTree.FindWidget(GET_MEMBER_NAME_CHECKED(UVBScreenWidget, BtnClose))))
		{
			CompileLog.Error(FText::FromString(TEXT(
				"닫기 버튼이 필수인 화면(bRequireCloseButton)인데 위젯 트리에 BtnClose 라는 Button 이 없다 - "
				"폰과 PIE 에는 뒤로 가기 키가 없어 이 화면을 닫을 길이 없다.")));
		}
		if (!bIsBackHandler)
		{
			CompileLog.Error(FText::FromString(TEXT(
				"닫기 버튼이 필수인 화면인데 bIsBackHandler 가 거짓이다 - 기본 닫기는 뒤로 가기를 타므로 눌러도 닫히지 않는다.")));
		}
	}
}
#endif
