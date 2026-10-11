#pragma once

#include "AnimGraphNode_Base.h"
#include "Animation/AnimNode_VBMuteEffects.h"
#include "AnimGraphNode_VBMuteEffects.generated.h"

class UAnimBlueprint;

UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UAnimGraphNode_VBMuteEffects : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Settings")
	FAnimNode_VBMuteEffects Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override { return FText::FromString(TEXT("Vowbound Mute Effects")); }
	virtual FText GetTooltipText() const override
	{
		return FText::FromString(TEXT("Pass-through. Sound and camera-shake notifies from assets updated below this node are skipped (hidden branch kept updating)."));
	}
	virtual FString GetNodeCategory() const override { return TEXT("Vowbound"); }

	// 명시적으로 호출할 때만 넣는다. TargetNodeName 의 TargetPinName(포즈 입력, 연결 하나)에 들어오던 선 사이에 끼운다.
	// 저장/컴파일은 호출자가 검증한 뒤 한다. 같은 그래프에 이미 있으면 거부한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool InsertOnPin(UAnimBlueprint* Blueprint, FName TargetNodeName, FName TargetPinName);
};
