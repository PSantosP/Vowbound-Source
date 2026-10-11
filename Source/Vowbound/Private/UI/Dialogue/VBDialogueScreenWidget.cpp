// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/Dialogue/VBDialogueScreenWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Narrative/Dialogue/VBDialogueAsset.h"
#include "Narrative/Dialogue/VBDialogueComponent.h"
#include "Narrative/Dialogue/VBSpeakerDefinition.h"
#include "UI/VBListRowWidget.h"
#include "Vowbound/Vowbound.h"

UVBDialogueScreenWidget::UVBDialogueScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 뒤로 가기를 받아 «떠나기» 로 돌린다(NativeOnHandleBackAction). 끄면 뒤로 가기로 대화를 벗어날 길이 없다.
	bIsBackHandler = true;
}

void UVBDialogueScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnContinue)
	{
		BtnContinue->OnClicked.AddUniqueDynamic(this, &UVBDialogueScreenWidget::HandleContinueClicked);
	}
}

void UVBDialogueScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	DialogueComponent = UVBDialogueComponent::FindFor(GetOwningPlayer());
	if (UVBDialogueComponent* Component = DialogueComponent.Get())
	{
		ViewChangedHandle = Component->OnViewChanged.AddUObject(this, &UVBDialogueScreenWidget::Refresh);
	}
	else
	{
		VB_LOG(Warning, "대화 화면: 이 플레이어의 대화 컴포넌트를 찾지 못했다(PlayerController 에 UVBDialogueComponent)");
	}
	Refresh();
}

void UVBDialogueScreenWidget::NativeOnDeactivated()
{
	if (UVBDialogueComponent* Component = DialogueComponent.Get())
	{
		Component->OnViewChanged.Remove(ViewChangedHandle);
	}
	ViewChangedHandle.Reset();
	Super::NativeOnDeactivated();
}

bool UVBDialogueScreenWidget::NativeOnHandleBackAction()
{
	if (!bIsBackHandler)
	{
		return false;
	}
	if (UVBDialogueComponent* Component = DialogueComponent.Get())
	{
		Component->RequestLeave();
	}
	else
	{
		// 진행의 주인을 못 찾았다 - 화면만이라도 내려 입력이 갇히지 않게 한다.
		DeactivateWidget();
	}
	return true;
}

UWidget* UVBDialogueScreenWidget::NativeGetDesiredFocusTarget() const
{
	// 게임패드: 고를 수 있는 첫 선택지, 없으면 계속 버튼.
	if (ChoiceBox)
	{
		for (int32 Index = 0; Index < ChoiceBox->GetChildrenCount(); ++Index)
		{
			const UVBListRowWidget* Row = Cast<UVBListRowWidget>(ChoiceBox->GetChildAt(Index));
			if (Row && Row->GetButton() && Row->GetButton()->GetIsEnabled())
			{
				return Row->GetButton();
			}
		}
	}
	if (BtnContinue && BtnContinue->IsVisible())
	{
		return BtnContinue;
	}
	return Super::NativeGetDesiredFocusTarget();
}

void UVBDialogueScreenWidget::Refresh()
{
	const UVBDialogueComponent* Component = DialogueComponent.Get();
	const FVBDialogueView* View = Component ? &Component->GetView() : nullptr;
	const FVBDialogueNode* Node = View ? View->GetNode() : nullptr;

	if (ChoiceBox)
	{
		ChoiceBox->ClearChildren();
	}
	if (!Node)
	{
		// 끝났거나 아직 첫 노드가 안 왔다. 곧 컴포넌트가 화면을 내리거나 채운다.
		if (TxtSpeaker)
		{
			TxtSpeaker->SetText(FText::GetEmpty());
		}
		if (TxtLine)
		{
			TxtLine->SetText(FText::GetEmpty());
		}
		if (BtnContinue)
		{
			BtnContinue->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	if (TxtSpeaker)
	{
		// 화자 태그가 없는 노드는 서술이다 - 이름 칸을 접는다.
		const UVBSpeakerDefinition* Speaker = View->Dialogue->FindSpeaker(Node->Speaker);
		TxtSpeaker->SetText(Speaker ? Speaker->DisplayName : FText::GetEmpty());
		TxtSpeaker->SetVisibility(Speaker ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (TxtLine)
	{
		TxtLine->SetText(Node->Line);
	}

	bool bAnyAvailable = false;
	if (!ChoiceWidgetClass && !Node->Choices.IsEmpty())
	{
		VB_LOG(Warning, "대화 화면: ChoiceWidgetClass 미지정 - 선택지를 그릴 수 없다(WBP 클래스 기본값)");
	}
	for (int32 Index = 0; Index < Node->Choices.Num(); ++Index)
	{
		const FVBDialogueChoice& Choice = Node->Choices[Index];
		const bool bAvailable = View->ChoiceAvailable.IsValidIndex(Index) && View->ChoiceAvailable[Index];
		bAnyAvailable |= bAvailable;
		if ((!bAvailable && !Choice.bShowWhenUnavailable) || !ChoiceBox || !ChoiceWidgetClass)
		{
			continue;
		}
		UVBListRowWidget* Row = CreateWidget<UVBListRowWidget>(this, ChoiceWidgetClass);
		Row->Setup(Index, Choice.Text, FText::GetEmpty(), bAvailable);
		Row->OnRowClicked.BindUObject(this, &UVBDialogueScreenWidget::HandleChoiceClicked);
		ChoiceBox->AddChild(Row);
	}

	// 고를 수 있는 선택지가 없으면 계속 버튼을 띄운다 - 서버도 같은 규칙으로 «계속» 을 받는다(UVBDialogueComponent::ServerContinue).
	if (BtnContinue)
	{
		BtnContinue->SetVisibility(bAnyAvailable ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (TxtContinue)
	{
		const bool bHasNext = Node->Choices.IsEmpty() && !Node->NextNodeId.IsNone();
		TxtContinue->SetText(bHasNext ? ContinueText : EndText);
	}
	// 초점이 있던 버튼이 방금 지워졌다 - 새 첫 버튼으로 옮긴다(게임패드).
	RequestRefreshFocus();
}

void UVBDialogueScreenWidget::HandleChoiceClicked(int32 ChoiceIndex)
{
	if (UVBDialogueComponent* Component = DialogueComponent.Get())
	{
		Component->RequestChoose(ChoiceIndex);
	}
}

void UVBDialogueScreenWidget::HandleContinueClicked()
{
	if (UVBDialogueComponent* Component = DialogueComponent.Get())
	{
		Component->RequestContinue();
	}
}
