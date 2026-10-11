// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/Progression/VBSkillTreeScreenWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Input/CommonUIInputTypes.h"
#include "Rendering/DrawElements.h"
#include "Progression/VBProgressionComponent.h"
#include "Progression/VBSkillTreeDefinition.h"
#include "UI/VBListRowWidget.h"
#include "Vowbound/Vowbound.h"

void UVBSkillTreeScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnUnlock)
	{
		BtnUnlock->OnClicked.AddUniqueDynamic(this, &UVBSkillTreeScreenWidget::HandleUnlockClicked);
	}
	if (CloseInputAction)
	{
		RegisterUIActionBinding(FBindUIActionArgs(CloseInputAction, /*bShouldDisplayInActionBar=*/false,
		                                          FSimpleDelegate::CreateUObject(this, &UVBSkillTreeScreenWidget::HandleCloseInput)));
	}
}

void UVBSkillTreeScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	Progression = UVBProgressionComponent::FindFor(GetOwningPlayer());
	if (UVBProgressionComponent* Owned = Progression.Get())
	{
		ProgressionChangedHandle = Owned->OnProgressionChanged.AddUObject(this, &UVBSkillTreeScreenWidget::Rebuild);
	}
	else
	{
		VB_LOG(Warning, "스킬 트리 화면: 이 플레이어의 성장 컴포넌트를 찾지 못했다(PlayerState 에 UVBProgressionComponent)");
	}
	SelectedIndex = INDEX_NONE;
	Rebuild();
}

void UVBSkillTreeScreenWidget::NativeOnDeactivated()
{
	if (UVBProgressionComponent* Owned = Progression.Get())
	{
		Owned->OnProgressionChanged.Remove(ProgressionChangedHandle);
	}
	ProgressionChangedHandle.Reset();
	Super::NativeOnDeactivated();
}

UWidget* UVBSkillTreeScreenWidget::NativeGetDesiredFocusTarget() const
{
	// 게임패드로 열었을 때 첫 칸의 버튼에 초점.
	if (!NodeWidgets.IsEmpty() && NodeWidgets[0])
	{
		return NodeWidgets[0]->GetButton();
	}
	return Super::NativeGetDesiredFocusTarget();
}

void UVBSkillTreeScreenWidget::Rebuild()
{
	RefreshHeader();
	const UVBProgressionComponent* Owned = Progression.Get();
	const UVBSkillTreeDefinition* Tree = Owned ? Owned->GetSkillTree() : nullptr;
	if (!NodeCanvas || !Tree)
	{
		RefreshDetail();
		return;
	}
	if (!NodeWidgetClass)
	{
		VB_LOG(Warning, "스킬 트리 화면: NodeWidgetClass 미할당 - 칸이 안 나온다(WBP 클래스 기본값)");
		RefreshDetail();
		return;
	}
	// 칸 수가 같으면 위젯을 다시 쓰고 글만 바꾼다 - 게임패드 초점이 열기 뒤에도 그 칸에 남는다(인벤토리 칸 버튼과 같은 판단).
	if (NodeWidgets.Num() != Tree->Nodes.Num())
	{
		NodeCanvas->ClearChildren();
		NodeWidgets.Reset();
		for (int32 Index = 0; Index < Tree->Nodes.Num(); ++Index)
		{
			UVBListRowWidget* NodeWidget = CreateWidget<UVBListRowWidget>(GetOwningPlayer(), NodeWidgetClass);
			if (!NodeWidget)
			{
				continue;
			}
			NodeWidget->OnRowClicked.BindUObject(this, &UVBSkillTreeScreenWidget::Select);
			if (UCanvasPanelSlot* NodeSlot = NodeCanvas->AddChildToCanvas(NodeWidget))
			{
				NodeSlot->SetAutoSize(false);
				NodeSlot->SetSize(NodeSize);
				NodeSlot->SetAlignment(FVector2D(0.5, 0.5));
				NodeSlot->SetPosition(Tree->Nodes[Index].LayoutPosition);
			}
			NodeWidgets.Add(NodeWidget);
		}
	}
	for (int32 Index = 0; Index < NodeWidgets.Num() && Index < Tree->Nodes.Num(); ++Index)
	{
		const FVBSkillNode& Node = Tree->Nodes[Index];
		const EVBSkillUnlockResult State = Owned->IsUnlocked(Node.NodeId) ? EVBSkillUnlockResult::AlreadyUnlocked : Owned->CanUnlock(Node.NodeId);
		// 칸 버튼은 늘 눌린다(상세를 보려고). 열 수 있는가는 상세의 «열기» 버튼이 보인다.
		NodeWidgets[Index]->Setup(Index, Node.DisplayName, StateText(State), true);
		NodeWidgets[Index]->SetIcon(Node.Icon);
	}
	RefreshDetail();
}

void UVBSkillTreeScreenWidget::RefreshHeader()
{
	const UVBProgressionComponent* Owned = Progression.Get();
	if (!Owned)
	{
		return;
	}
	if (TxtLevel)
	{
		TxtLevel->SetText(Owned->GetLevel() >= Owned->GetMaxLevel()
			? FText::Format(MaxLevelFormat, Owned->GetLevel())
			: FText::Format(LevelFormat, Owned->GetLevel(), Owned->GetXPIntoLevel(), Owned->GetXPForNextLevel()));
	}
	if (TxtPoints)
	{
		TxtPoints->SetText(FText::Format(PointsFormat, Owned->GetPointsAvailable()));
	}
}

void UVBSkillTreeScreenWidget::RefreshDetail()
{
	const UVBProgressionComponent* Owned = Progression.Get();
	const UVBSkillTreeDefinition* Tree = Owned ? Owned->GetSkillTree() : nullptr;
	const FVBSkillNode* Node = Tree && Tree->Nodes.IsValidIndex(SelectedIndex) ? &Tree->Nodes[SelectedIndex] : nullptr;
	if (TxtNodeName)
	{
		TxtNodeName->SetText(Node ? Node->DisplayName : FText::GetEmpty());
	}
	if (TxtNodeDescription)
	{
		TxtNodeDescription->SetText(Node ? Node->Description : FText::GetEmpty());
	}
	EVBSkillUnlockResult State = EVBSkillUnlockResult::UnknownNode;
	if (Node)
	{
		State = Owned->IsUnlocked(Node->NodeId) ? EVBSkillUnlockResult::AlreadyUnlocked : Owned->CanUnlock(Node->NodeId);
	}
	if (TxtNodeInfo)
	{
		TxtNodeInfo->SetText(Node
			? FText::Format(INVTEXT("{0}\n{1}"), FText::Format(RequirementFormat, Node->Cost, Node->RequiredLevel), StateText(State))
			: FText::GetEmpty());
	}
	if (BtnUnlock)
	{
		BtnUnlock->SetVisibility(Node ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		BtnUnlock->SetIsEnabled(State == EVBSkillUnlockResult::Unlocked);
	}
	if (TxtUnlock)
	{
		TxtUnlock->SetText(UnlockText);
	}
}

void UVBSkillTreeScreenWidget::Select(int32 NodeIndex)
{
	SelectedIndex = NodeIndex;
	RefreshDetail();
}

FText UVBSkillTreeScreenWidget::StateText(EVBSkillUnlockResult Result) const
{
	if (const FText* Found = StateTexts.Find(Result))
	{
		return *Found;
	}
	return FText::FromString(VBGrowth::LexToString(Result));
}

void UVBSkillTreeScreenWidget::HandleUnlockClicked()
{
	UVBProgressionComponent* Owned = Progression.Get();
	const UVBSkillTreeDefinition* Tree = Owned ? Owned->GetSkillTree() : nullptr;
	if (Tree && Tree->Nodes.IsValidIndex(SelectedIndex))
	{
		// 요청만 보낸다 - 서버가 다시 판정하고, 결과는 OnProgressionChanged 로 돌아온다.
		Owned->RequestUnlock(Tree->Nodes[SelectedIndex].NodeId);
	}
}

void UVBSkillTreeScreenWidget::HandleCloseInput()
{
	DeactivateWidget();
}

int32 UVBSkillTreeScreenWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// 자식(배경·칸)을 먼저 그리고 선을 맨 위에 그린다 - 아래에 그리면 WBP 의 전체 배경 그림이 선을 가린다.
	//   대신 선을 칸 상자 가장자리에서 끊어 칸 글을 가리지 않는다.
	int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const UVBProgressionComponent* Owned = Progression.Get();
	const UVBSkillTreeDefinition* Tree = Owned ? Owned->GetSkillTree() : nullptr;
	if (!Tree || !NodeCanvas || NodeWidgets.Num() != Tree->Nodes.Num())
	{
		return MaxLayer;
	}
	// 칸 중심을 캔버스 좌표 -> 절대 좌표 -> 이 위젯 좌표로 옮긴다. 첫 프레임은 캔버스 기하가 비어 한 프레임 늦게 그려진다(설계 5절).
	const FGeometry& CanvasGeometry = NodeCanvas->GetCachedGeometry();
	auto ToLocal = [&](const FVector2D& CanvasPoint)
	{
		return FVector2D(AllottedGeometry.AbsoluteToLocal(CanvasGeometry.LocalToAbsolute(CanvasPoint)));
	};
	const FVector2D Half = NodeSize * 0.5;
	const int32 LineLayer = MaxLayer + 1;
	for (const FVBSkillNode& Node : Tree->Nodes)
	{
		for (const FGameplayTag& PrerequisiteId : Node.Prerequisites)
		{
			const FVBSkillNode* Prerequisite = Tree->FindNode(PrerequisiteId);
			if (!Prerequisite)
			{
				continue;
			}
			// 같은 크기의 두 상자 중심을 잇는 선이 상자를 빠져나오는 비율 t(양 끝 같다). 0.5 이상이면 상자가 겹쳐 그릴 선이 없다.
			const FVector2D From = Prerequisite->LayoutPosition;
			const FVector2D Delta = Node.LayoutPosition - From;
			const double TX = FMath::IsNearlyZero(Delta.X) ? TNumericLimits<double>::Max() : Half.X / FMath::Abs(Delta.X);
			const double TY = FMath::IsNearlyZero(Delta.Y) ? TNumericLimits<double>::Max() : Half.Y / FMath::Abs(Delta.Y);
			const double T = FMath::Min(TX, TY);
			if (T >= 0.5)
			{
				continue;
			}
			const TArray<FVector2D> Points = {ToLocal(From + Delta * T), ToLocal(Node.LayoutPosition - Delta * T)};
			FSlateDrawElement::MakeLines(OutDrawElements, LineLayer, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
				Owned->IsUnlocked(Node.NodeId) ? LineColorUnlocked : LineColor, true, LineThickness);
		}
	}
	return LineLayer;
}
