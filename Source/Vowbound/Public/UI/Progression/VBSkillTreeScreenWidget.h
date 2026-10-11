// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Progression/VBGrowthRules.h"
#include "UI/VBScreenWidget.h"
#include "VBSkillTreeScreenWidget.generated.h"

class UButton;
class UCanvasPanel;
class UInputAction;
class UTextBlock;
class UVBListRowWidget;
class UVBProgressionComponent;

/**
 * 스킬 트리 화면(UI.Screen.SkillTree). 설계 .sw/remaining-features/b2-growth.md.
 *
 * 표시는 아무것도 결정하지 않는다(인벤토리 화면과 같은 규칙): 칸 상태는 UVBProgressionComponent::CanUnlock 을 묻고,
 *   «열기» 는 요청(RequestUnlock)만 보낸다. 바뀐 것은 OnProgressionChanged 를 듣고 다시 그린다.
 * 칸 = 공용 목록 줄(UVBListRowWidget)을 NodeCanvas 의 칸 자리(FVBSkillNode::LayoutPosition, 칸 중심)에 놓는다. 선행 선은 NativePaint 가 그린다.
 * 화면 위젯은 재사용되므로 켜질 때마다 처음부터 짓는다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBSkillTreeScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// 칸 하나의 WBP(공용 목록 줄). 비면 칸이 안 나오고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBListRowWidget> NodeWidgetClass;

	// 칸 하나의 크기(캔버스 단위). 칸 자리는 칸 중심이다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FVector2D NodeSize = FVector2D(200.0, 64.0);

	// 화면이 떠 있는 동안 이 입력을 한 번 더 누르면 닫는다(HUD 가 여는 입력과 같은 IA).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TObjectPtr<UInputAction> CloseInputAction;

	// 선행 선. 끝 칸이 열렸으면 Unlocked 색.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FLinearColor LineColor = FLinearColor(0.35f, 0.35f, 0.35f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FLinearColor LineColorUnlocked = FLinearColor(0.85f, 0.7f, 0.3f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI", meta=(ClampMin="1.0"))
	float LineThickness = 3.0f;

	// {0} = 레벨, {1} = 이 레벨에서 모은 경험치, {2} = 다음 레벨까지 필요한 양. 최대 레벨이면 MaxLevelFormat({0} = 레벨).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText LevelFormat = NSLOCTEXT("VBSkillTree", "Level", "레벨 {0}   경험치 {1} / {2}");

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText MaxLevelFormat = NSLOCTEXT("VBSkillTree", "MaxLevel", "레벨 {0} (최대)");

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText PointsFormat = NSLOCTEXT("VBSkillTree", "Points", "스킬 포인트 {0}");

	// 상세의 조건 줄. {0} = 비용, {1} = 필요 레벨.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText RequirementFormat = NSLOCTEXT("VBSkillTree", "Requirement", "비용 {0} 포인트 · 레벨 {1} 이상");

	// 칸 상태 글(칸 옆 글과 상세에 같이 쓴다). 없으면 판정 이름이 보인다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TMap<EVBSkillUnlockResult, FText> StateTexts;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText UnlockText = NSLOCTEXT("VBSkillTree", "Unlock", "열기");

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UCanvasPanel> NodeCanvas;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtLevel;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtPoints;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtNodeName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtNodeDescription;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtNodeInfo;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnUnlock;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtUnlock;

private:
	void Rebuild();
	void RefreshHeader();
	void RefreshDetail();
	void Select(int32 NodeIndex);
	FText StateText(EVBSkillUnlockResult Result) const;
	void HandleCloseInput();

	UFUNCTION()
	void HandleUnlockClicked();

	TWeakObjectPtr<UVBProgressionComponent> Progression;
	FDelegateHandle ProgressionChangedHandle;

	// 칸 번호(= 트리의 Nodes 순서) -> 칸 위젯. 줄 위젯은 번호만 알린다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBListRowWidget>> NodeWidgets;

	// 화면 상태 - 저장하지 않는다.
	int32 SelectedIndex = INDEX_NONE;
};
