// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Dialogue/VBDialogueComponent.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Narrative/Dialogue/VBDialogueAsset.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Vowbound/Vowbound.h"

const FVBDialogueNode* FVBDialogueView::GetNode() const
{
	return Dialogue ? Dialogue->FindNode(NodeId) : nullptr;
}

UVBDialogueComponent::UVBDialogueComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// 서버 -> 주인 클라 RPC 를 쓰려면 컴포넌트가 복제돼야 한다. 복제하는 속성은 없다.
	SetIsReplicatedByDefault(true);
}

UVBDialogueComponent* UVBDialogueComponent::FindFor(const AActor* Actor)
{
	const AController* Controller = Cast<AController>(Actor);
	if (!Controller)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			Controller = Pawn->GetController();
		}
	}
	return Controller ? Controller->FindComponentByClass<UVBDialogueComponent>() : nullptr;
}

bool UVBDialogueComponent::StartDialogue(const UVBDialogueAsset* Dialogue, AActor* Speaker)
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		VB_LOG(Warning, "대화 시작을 권한 없는 쪽에서 불렀다 - 무시한다(진행은 서버만)");
		return false;
	}
	if (!Dialogue)
	{
		VB_LOG(Warning, "대화 시작: 대화 자산이 비었다(%s)", *GetNameSafe(Speaker));
		return false;
	}
	if (IsInDialogue())
	{
		VB_LOG(Log, "대화 시작 거절: 이미 %s 대화 중", *GetNameSafe(ServerDialogue));
		return false;
	}
	const FName StartNodeId = Dialogue->ChooseEntry(MakeRuleContext());
	if (StartNodeId.IsNone())
	{
		VB_LOG(Log, "대화 시작 거절: %s 의 진입점 조건이 모두 거짓", *GetNameSafe(Dialogue));
		return false;
	}
	ServerDialogue = Dialogue;
	ServerSpeaker = Speaker;
	VB_LOG(Log, "대화 시작: %s (%s) -> %s", *GetNameSafe(Dialogue), *GetNameSafe(Speaker), *StartNodeId.ToString());
	EnterNode(StartNodeId);
	return IsInDialogue();
}

void UVBDialogueComponent::EndDialogue()
{
	if (!IsInDialogue())
	{
		return;
	}
	VB_LOG(Log, "대화 끝: %s", *GetNameSafe(ServerDialogue));
	ServerDialogue = nullptr;
	ServerNodeId = NAME_None;
	ServerSpeaker.Reset();
	ClientEndDialogue();
}

void UVBDialogueComponent::RequestChoose(int32 ChoiceIndex)
{
	if (View.IsActive())
	{
		ServerChoose(View.NodeId, ChoiceIndex);
	}
}

void UVBDialogueComponent::RequestContinue()
{
	if (View.IsActive())
	{
		ServerContinue(View.NodeId);
	}
}

void UVBDialogueComponent::RequestLeave()
{
	if (View.IsActive())
	{
		ServerLeave();
	}
}

void UVBDialogueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 레벨을 떠나거나 PC 가 사라진다. 주인 클라에 보낼 곳이 없으니 서버 상태만 비운다 - 화면은 레벨과 함께 내려간다.
	ServerDialogue = nullptr;
	ServerSpeaker.Reset();
	View = FVBDialogueView();
	Super::EndPlay(EndPlayReason);
}

void UVBDialogueComponent::ServerChoose_Implementation(FName NodeId, int32 ChoiceIndex)
{
	const FVBDialogueNode* Node = ValidateRequest(NodeId, TEXT("선택"));
	if (!Node)
	{
		return;
	}
	if (!Node->Choices.IsValidIndex(ChoiceIndex))
	{
		VB_LOG(Warning, "대화 선택 거절: 노드 %s 에 선택지 %d 번이 없다", *NodeId.ToString(), ChoiceIndex);
		return;
	}
	const FVBDialogueChoice& Choice = Node->Choices[ChoiceIndex];
	const FVBRuleContext Context = MakeRuleContext();
	// 표시가 켠 버튼을 믿지 않는다 - 조건을 여기서 다시 본다(그 사이 사실이 바뀌었을 수 있다).
	if (!VBRules::EvaluateAll(Choice.Conditions, Context))
	{
		VB_LOG(Log, "대화 선택 거절: 노드 %s 선택지 %d 의 조건이 거짓", *NodeId.ToString(), ChoiceIndex);
		return;
	}
	const FName NextNodeId = Choice.NextNodeId;
	VBRules::ApplyAll(Choice.Effects, Context);
	EnterNode(NextNodeId);
}

void UVBDialogueComponent::ServerContinue_Implementation(FName NodeId)
{
	const FVBDialogueNode* Node = ValidateRequest(NodeId, TEXT("계속"));
	if (!Node)
	{
		return;
	}
	// 선택지가 있는 노드는 고르는 것으로만 넘어간다. 다만 하나도 고를 수 없으면 계속 = 끝이다 - 막힌 채 남지 않게(화면도 같은 규칙으로 버튼을 띄운다).
	const FVBRuleContext Context = MakeRuleContext();
	for (const FVBDialogueChoice& Choice : Node->Choices)
	{
		if (VBRules::EvaluateAll(Choice.Conditions, Context))
		{
			VB_LOG(Warning, "대화 계속 거절: 노드 %s 에 고를 수 있는 선택지가 있다", *NodeId.ToString());
			return;
		}
	}
	EnterNode(Node->Choices.IsEmpty() ? Node->NextNodeId : NAME_None);
}

void UVBDialogueComponent::ServerLeave_Implementation()
{
	EndDialogue();
}

void UVBDialogueComponent::ClientShowNode_Implementation(const UVBDialogueAsset* Dialogue, FName NodeId, const TArray<bool>& ChoiceAvailable)
{
	View.Dialogue = Dialogue;
	View.NodeId = NodeId;
	View.ChoiceAvailable = ChoiceAvailable;
	if (!View.GetNode())
	{
		VB_LOG(Warning, "대화 표시: %s 에서 노드 %s 를 찾지 못했다 - 빈 화면이 된다", *GetNameSafe(Dialogue), *NodeId.ToString());
	}
	// 화면을 여는 것은 이 컴포넌트가 아니라 PC 다(AVBPlayerController 가 이 알림을 듣는다) - 모델이 뷰를 모르게(설계 13.1절 1번).
	OnViewChanged.Broadcast();
}

void UVBDialogueComponent::ClientEndDialogue_Implementation()
{
	View = FVBDialogueView();
	OnViewChanged.Broadcast();
}

void UVBDialogueComponent::EnterNode(FName NodeId)
{
	const FVBDialogueNode* Node = ServerDialogue ? ServerDialogue->FindNode(NodeId) : nullptr;
	if (!Node)
	{
		// 빈 이름 = 정상적인 끝. 이름이 있는데 없는 노드면 저작 실수다(IsDataValid 가 먼저 잡는다).
		if (!NodeId.IsNone())
		{
			VB_LOG(Warning, "대화 %s: 없는 노드 %s 로 가려 했다 - 대화를 끝낸다", *GetNameSafe(ServerDialogue), *NodeId.ToString());
		}
		EndDialogue();
		return;
	}
	ServerNodeId = NodeId;
	const FVBRuleContext Context = MakeRuleContext();
	VBRules::ApplyAll(Node->OnEnterEffects, Context);

	if (Node->ReachedTag.IsValid())
	{
		if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
		{
			FVBGameEvent Event;
			Event.EventTag = VBGameplayTags::Event_Dialogue_NodeReached;
			Event.TargetTags.AddTag(Node->ReachedTag);
			Event.Instigator = Context.Instigator;
			Events->Broadcast(Event);
		}
	}

	// 선택지 판정은 들어선 «뒤에» 한다 - 들어서며 켠 사실이 이 노드의 선택지를 열 수 있다.
	TArray<bool> ChoiceAvailable;
	ChoiceAvailable.Reserve(Node->Choices.Num());
	for (const FVBDialogueChoice& Choice : Node->Choices)
	{
		ChoiceAvailable.Add(VBRules::EvaluateAll(Choice.Conditions, Context));
	}
	ClientShowNode(ServerDialogue, NodeId, ChoiceAvailable);
}

const FVBDialogueNode* UVBDialogueComponent::ValidateRequest(FName NodeId, const TCHAR* What)
{
	if (!IsInDialogue())
	{
		// 끝난 직후에 도착한 요청(끝 버튼을 두 번 누름). 정상이라 조용히 버린다.
		VB_LOG(Verbose, "대화 %s 요청: 대화 중이 아니다 - 버린다", What);
		return nullptr;
	}
	if (ServerSpeaker.IsStale())
	{
		VB_LOG(Log, "대화 %s 요청: 말을 건 대상이 사라졌다 - 대화를 끝낸다", What);
		EndDialogue();
		return nullptr;
	}
	if (NodeId != ServerNodeId)
	{
		VB_LOG(Log, "대화 %s 요청: 지난 노드 %s 에 대한 요청(지금 %s) - 버린다", What, *NodeId.ToString(), *ServerNodeId.ToString());
		return nullptr;
	}
	return ServerDialogue->FindNode(ServerNodeId);
}

FVBRuleContext UVBDialogueComponent::MakeRuleContext() const
{
	const AController* Controller = Cast<AController>(GetOwner());
	FVBRuleContext Context = FVBRuleContext::Make(this, Controller ? Controller->PlayerState.Get() : nullptr);
	// 말을 건 NPC. 상점 열기 효과가 이것을 상인으로 쓴다.
	Context.Counterpart = ServerSpeaker.Get();
	return Context;
}
