// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Narrative/Story/VBStoryState.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBStoryStateComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FVBOnStoryStateChanged);

/**
 * 세계 사실의 주인. AVBGameState 에 붙는다. 설계 .sw/narrative 2.1절·5절(주인 지도).
 *
 * 쓰기는 서버만 한다 - 클라에서 부르면 아무것도 안 바꾸고 경고한다. 조건은 어디서나 읽는다(복제되므로 클라 표시도 맞다).
 * 바꾸는 길은 규칙의 효과(FVBEffect_SetFact·AddCounter)다. 기능 코드가 여기를 직접 쓰기 시작하면 «누가 이 사실을 켰나» 를 찾을 곳이 흩어진다.
 * 왜 GameState 인가: 세계 사실은 플레이어 한 명의 것이 아니다. 개인 사실이 필요해지면 같은 클래스를 PlayerState 에도 붙인다(주인만 둘).
 */
UCLASS(ClassGroup=(Vowbound))
class VOWBOUND_API UVBStoryStateComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBStoryStateComponent();

	// 지금 월드의 GameState 에 붙은 것. 없으면 nullptr(메뉴 월드 등).
	static UVBStoryStateComponent* Get(const UObject* WorldContextObject);

	const FVBStoryState& GetState() const { return State; }
	bool HasFact(FGameplayTag Fact) const { return State.HasFact(Fact); }
	int32 GetCounter(FGameplayTag Counter) const { return State.GetCounter(Counter); }

	// 서버에서만 바꾼다. 바뀌었으면 OnStoryStateChanged.
	void SetFact(FGameplayTag Fact, bool bOn);
	void AddToCounter(FGameplayTag Counter, int32 Delta);

	// 서버에서는 바꾼 직후, 클라에서는 복제가 도착했을 때. 표시(퀘스트 목표·대화 선택지 잠금)가 다시 그릴 때 듣는다.
	FVBOnStoryStateChanged OnStoryStateChanged;

	//~ IVBSaveParticipant - 사실·카운터 구역(FVBProgressSaveData::Story)
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	// 등록은 BeginPlay 가 아니라 여기 - Continue 적용이 월드 BeginPlay 보다 먼저 올 수 있다(IVBSaveParticipant 주석).
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(ReplicatedUsing=OnRep_State)
	FVBStoryState State;

	UFUNCTION()
	void OnRep_State();

	// 소유 액터가 없으면(단위 시험) 쓴다 - 복제할 곳이 없으니 서버와 갈릴 일도 없다.
	bool CanWrite(const TCHAR* What) const;
};
