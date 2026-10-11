#pragma once

#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/AnimNode_VBLadderIK.h"
#include "AnimGraphNode_VBLadderIK.generated.h"

class UAnimBlueprint;

UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UAnimGraphNode_VBLadderIK : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Settings")
	FAnimNode_VBLadderIK Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override { return FText::FromString(TEXT("Vowbound Ladder IK")); }
	virtual FText GetTooltipText() const override { return FText::FromString(TEXT("Lock ladder contacts using the incoming, uncorrected pose.")); }

	// 명시적으로 호출할 때만 노드를 삽입한다. 저장/컴파일은 호출자가 검증한 뒤 수행한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool InsertBefore(UAnimBlueprint* Blueprint, FName TargetNodeName);

protected:
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
	virtual FText GetControllerDescription() const override { return GetNodeTitle(ENodeTitleType::FullTitle); }
};
