// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBDialogueComponent.generated.h"

class UVBDialogueAsset;
struct FVBDialogueNode;
struct FVBRuleContext;

/** 주인 클라가 그릴 지금의 대사. 서버가 보내 준 것 그대로다 - 클라가 계산하지 않는다. */
USTRUCT()
struct VOWBOUND_API FVBDialogueView
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UVBDialogueAsset> Dialogue = nullptr;

	UPROPERTY()
	FName NodeId;

	// 선택지마다 고를 수 있나(노드의 Choices 와 같은 순서). 조건은 서버가 봤다.
	UPROPERTY()
	TArray<bool> ChoiceAvailable;

	bool IsActive() const { return Dialogue != nullptr; }
	const FVBDialogueNode* GetNode() const;
};

DECLARE_MULTICAST_DELEGATE(FVBOnDialogueViewChanged);

/**
 * 대화 진행의 주인. AVBPlayerController 에 붙는다. 설계 .sw/narrative 3.2절·11.2절.
 *
 * 서버가 진행을 든다: 어느 대화의 어느 노드인가, 선택지가 고를 수 있나, 무엇을 적용하나.
 *   주인 클라는 ClientShowNode 로 받은 것을 그리고, 버튼은 요청(RequestChoose·RequestContinue·RequestLeave)만 보낸다.
 * 요청에 노드 이름을 함께 보내는 이유: 버튼을 두 번 누르면 두 번째 요청이 이미 넘어간 다음 노드에 적용돼 한 마디를 건너뛴다.
 *   서버는 지금 노드와 다르면 버린다.
 * 화면: 이 컴포넌트는 화면을 모른다. 주인 클라에서 보기가 바뀌면 OnViewChanged 만 알리고, AVBPlayerController 가 그것을 듣고
 *   UI.Screen.Dialogue 를 열고 내린다(설계 13.1절 1번 - 종전에는 여기서 UI 관리자를 불렀다). 화면도 OnViewChanged 만 듣는다.
 * 왜 PC 인가: 대화는 한 플레이어의 입력과 화면에 딸린 일이다. 폰이 죽어도 PC 는 남아 끝 처리를 한다.
 * 게임을 멈추지 않는다(user 결정 2026-09-30). 화면의 입력이 Menu 라 그동안 조종만 멈춘다.
 * [빈칸] 전투 중에 열리지 않게 하는 조건은 아직 없다 - State.Combat.InCombat 을 붙이는 주인이 없다(설계 11.3절).
 */
UCLASS(ClassGroup=(Vowbound))
class VOWBOUND_API UVBDialogueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBDialogueComponent();

	// 폰·컨트롤러 어느 쪽을 줘도 그 플레이어의 대화 컴포넌트. 없으면 nullptr.
	static UVBDialogueComponent* FindFor(const AActor* Actor);

	// 서버에서만. 진입점이 하나도 참이 아니거나 이미 대화 중이면 false. Speaker 는 말을 건 대상(사라지면 대화가 끝난다).
	bool StartDialogue(const UVBDialogueAsset* Dialogue, AActor* Speaker);

	// 서버에서만. 대화 중이 아니면 아무것도 안 한다.
	void EndDialogue();

	bool IsInDialogue() const { return ServerDialogue != nullptr; }

	// 표시가 부른다. 판단은 서버가 다시 한다.
	void RequestChoose(int32 ChoiceIndex);
	void RequestContinue();
	void RequestLeave();

	// 주인 클라에서 그릴 것. 대화 중이 아니면 IsActive() 가 거짓이다.
	const FVBDialogueView& GetView() const { return View; }

	// 주인 클라에서 View 가 바뀌었을 때(새 노드·끝). PC 는 이것으로 화면을 여닫고, 화면은 이것으로 다시 그린다.
	FVBOnDialogueViewChanged OnViewChanged;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION(Server, Reliable)
	void ServerChoose(FName NodeId, int32 ChoiceIndex);

	UFUNCTION(Server, Reliable)
	void ServerContinue(FName NodeId);

	UFUNCTION(Server, Reliable)
	void ServerLeave();

	UFUNCTION(Client, Reliable)
	void ClientShowNode(const UVBDialogueAsset* Dialogue, FName NodeId, const TArray<bool>& ChoiceAvailable);

	UFUNCTION(Client, Reliable)
	void ClientEndDialogue();

	// 서버: 노드에 들어선다 - 효과 적용, 사건 방송, 선택지 판정, 주인 클라에 보내기. 없는 노드면 끝낸다.
	void EnterNode(FName NodeId);

	// 서버: 요청이 지금 노드에 대한 것이고 말 건 대상이 아직 있나. 아니면 버린다(대상이 사라졌으면 끝낸다).
	const FVBDialogueNode* ValidateRequest(FName NodeId, const TCHAR* What);

	FVBRuleContext MakeRuleContext() const;

	UPROPERTY()
	TObjectPtr<const UVBDialogueAsset> ServerDialogue;

	FName ServerNodeId;
	TWeakObjectPtr<AActor> ServerSpeaker;

	// 주인 클라의 그릴 것. 리슨 서버 호스트에서는 서버 상태와 한 객체에 같이 산다 - 그래서 둘을 따로 둔다.
	UPROPERTY()
	FVBDialogueView View;
};
