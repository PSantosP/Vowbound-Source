// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Story/VBStoryStateComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Vowbound/Vowbound.h"

UVBStoryStateComponent::UVBStoryStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

UVBStoryStateComponent* UVBStoryStateComponent::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->FindComponentByClass<UVBStoryStateComponent>() : nullptr;
}

void UVBStoryStateComponent::SetFact(FGameplayTag Fact, bool bOn)
{
	if (CanWrite(TEXT("SetFact")) && State.SetFact(Fact, bOn))
	{
		VB_LOG(Log, "사실 %s -> %s", *Fact.ToString(), bOn ? TEXT("켬") : TEXT("끔"));
		OnStoryStateChanged.Broadcast();
	}
}

void UVBStoryStateComponent::AddToCounter(FGameplayTag Counter, int32 Delta)
{
	if (!CanWrite(TEXT("AddToCounter")) || !Counter.IsValid() || Delta == 0)
	{
		return;
	}
	const int32 NewValue = State.AddToCounter(Counter, Delta);
	VB_LOG(Log, "카운터 %s %+d -> %d", *Counter.ToString(), Delta, NewValue);
	OnStoryStateChanged.Broadcast();
}

void UVBStoryStateComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	Save.Progress.Story = State;
}

void UVBStoryStateComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (!CanWrite(TEXT("ReadSave")))
	{
		return;
	}
	State = Save.Progress.Story;
	VB_LOG(Log, "사실 로드: 사실 %d, 카운터 %d", State.Facts.Num(), State.Counters.Num());
	OnStoryStateChanged.Broadcast();
}

void UVBStoryStateComponent::InitializeComponent()
{
	Super::InitializeComponent();
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBStoryStateComponent::UninitializeComponent()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBStoryStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBStoryStateComponent, State);
}

void UVBStoryStateComponent::OnRep_State()
{
	OnStoryStateChanged.Broadcast();
}

bool UVBStoryStateComponent::CanWrite(const TCHAR* What) const
{
	const AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		VB_LOG(Warning, "사실 저장소 %s 을 권한 없는 쪽에서 불렀다 - 무시한다(쓰기는 서버만, 클라는 복제로 받는다)", What);
		return false;
	}
	return true;
}
