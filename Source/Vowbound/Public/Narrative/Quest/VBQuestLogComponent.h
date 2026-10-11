// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Narrative/Story/VBGameEventSubsystem.h"   // FVBGameEvent - 처리 중에 온 사건을 값으로 쌓는다
#include "SaveSystem/VBSaveParticipant.h"
#include "SaveSystem/VBSaveGameTypes.h"   // FVBSavedQuest - 못 찾은 저장 줄을 값으로 지킨다
#include "VBQuestLogComponent.generated.h"

class APlayerState;
class UVBQuestDefinition;
class UVBQuestLogComponent;
struct FVBRuleContext;

UENUM(BlueprintType)
enum class EVBQuestStatus : uint8
{
	// 일지에 없다. 조건이 «아직 안 받은 퀘스트» 를 물을 때만 쓰인다 - 일지 줄에는 이 값이 없다.
	NotStarted,
	Active,
	Completed,
};

/** 일지의 한 줄(퀘스트당 한 줄). */
USTRUCT()
struct FVBQuestLogEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UVBQuestDefinition> Definition = nullptr;

	UPROPERTY()
	EVBQuestStatus Status = EVBQuestStatus::Active;

	UPROPERTY()
	int32 StageIndex = 0;

	// 지금 단계의 목표별 진행(목표와 같은 순서). 단계가 넘어가면 0 부터 다시.
	UPROPERTY()
	TArray<int32> Progress;

	// 마지막으로 시작·진행된 순번(서버가 올린다). HUD 가 «지금 따라가는 퀘스트» 를 고를 때 가장 큰 것을 쓴다.
	UPROPERTY()
	int32 RecentOrder = 0;

	void PostReplicatedAdd(const struct FVBQuestLogList& List);
	void PostReplicatedChange(const struct FVBQuestLogList& List);
	void PreReplicatedRemove(const struct FVBQuestLogList& List);
};

USTRUCT()
struct FVBQuestLogList : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVBQuestLogEntry> Entries;

	UPROPERTY(NotReplicated)
	TObjectPtr<UVBQuestLogComponent> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FVBQuestLogEntry, FVBQuestLogList>(Entries, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FVBQuestLogList> : public TStructOpsTypeTraitsBase2<FVBQuestLogList>
{
	enum { WithNetDeltaSerializer = true };
};

UENUM()
enum class EVBQuestNotice : uint8
{
	Started,
	StageAdvanced,
	Completed,
};

DECLARE_MULTICAST_DELEGATE(FVBOnQuestLogChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBOnQuestNotice, const UVBQuestDefinition*, EVBQuestNotice);

/**
 * 퀘스트 진행의 주인. AVBPlayerState 에 붙는다. 설계 .sw/narrative 3.3절·12.2절·5절(주인 지도).
 *
 * 시작은 규칙의 효과(FVBEffect_StartQuest)가 StartQuest 를 부르는 길 하나, 진행은 사건(UVBGameEventSubsystem)을 듣는 길 하나다.
 *   효과가 단계를 직접 밀면 진행의 주인이 둘이 된다(퀘스트 자신과 그 효과를 가진 모든 자산).
 * 쓰기는 서버만. 표시는 OnQuestLogChanged·OnQuestNotice 를 듣는다. 목록은 주인 클라에게만 복제한다.
 * 사건은 일으킨 사람이 이 플레이어이거나 없을 때(세계가 일으킨 일)만 센다 - 다른 플레이어가 잡은 적을 내 퀘스트가 세지 않게.
 * 단계 효과·보상이 다시 사건을 일으킬 수 있다(아이템 주기 -> Event.Item.Acquired). 처리 중에 온 사건은 줄을 세웠다가 끝난 뒤 처리한다 -
 *   한 퀘스트의 단계를 처리하는 도중에 같은 목록을 다시 건드리지 않게.
 */
UCLASS(ClassGroup=(Vowbound))
class VOWBOUND_API UVBQuestLogComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBQuestLogComponent();

	// 폰·컨트롤러·PlayerState 어느 쪽을 줘도 그 플레이어의 일지. 없으면 nullptr.
	static UVBQuestLogComponent* FindFor(const AActor* Actor);

	EVBQuestStatus GetStatus(const UVBQuestDefinition* Quest) const;
	// 진행 중이 아니면 INDEX_NONE.
	int32 GetStageIndex(const UVBQuestDefinition* Quest) const;
	const TArray<FVBQuestLogEntry>& GetEntries() const { return List.Entries; }

	// 진행 중 퀘스트 중 가장 최근에 시작·진행된 것(HUD 목표 줄). 없으면 nullptr.
	const FVBQuestLogEntry* GetTrackedEntry() const;

	// 서버에서만. 이미 일지에 있으면(진행·완료) 다시 시작하지 않고 false.
	bool StartQuest(const UVBQuestDefinition* Quest);

	// 서버: 사건 하나를 센다. 사건 방송을 듣는 자리이고, 시험이 직접 부르기도 한다.
	void HandleGameEvent(const FVBGameEvent& Event);

	FVBOnQuestLogChanged OnQuestLogChanged;
	FVBOnQuestNotice OnQuestNotice;

	//~ IVBSaveParticipant - 퀘스트 구역(UVBSaveGame::Quests). 문맥의 PlayerState 가 주인일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

	void HandleReplicatedChange() { OnQuestLogChanged.Broadcast(); }

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	FVBQuestLogList List;

	UFUNCTION(Client, Unreliable)
	void ClientQuestNotice(const UVBQuestDefinition* Quest, EVBQuestNotice Notice);

	// 한 줄의 지금 단계를 사건 하나로 센다(Event 가 null 이면 상태 목표만 다시 본다). 바뀌었으면 참.
	bool UpdateProgress(FVBQuestLogEntry& Entry, const FVBGameEvent* Event, const FVBRuleContext& Context);

	// 지금 단계가 다 찼으면 효과를 적용하고 넘긴다(새 단계가 이미 차 있으면 이어서). 마지막이면 보상·완료.
	void TryAdvance(FVBQuestLogEntry& Entry, const FVBRuleContext& Context);

	// 지금 단계에 맞게 진행 칸을 0 으로 다시 만든다.
	static void ResetProgress(FVBQuestLogEntry& Entry);

	// 이름이 ProcessEvent 면 UObject::ProcessEvent(RPC 생성 코드가 부른다)를 가린다 - 그래서 GameEvent 를 붙인다.
	void ProcessGameEvent(const FVBGameEvent& Event);
	void HandleStoryChanged();
	// 세이브 적용이 끝났다. 적용 중에 사실이 바뀌었으면 그때 미룬 판단을 한 번 한다.
	void HandleSaveApplied();
	void Announce(const UVBQuestDefinition* Quest, EVBQuestNotice Notice, FGameplayTag EventTag);
	void Touch(FVBQuestLogEntry& Entry);

	APlayerState* GetOwningPlayerState() const;
	bool HasWriteAuthority(const TCHAR* What) const;
	FVBQuestLogEntry* FindEntry(const UVBQuestDefinition* Quest);
	const FVBQuestLogEntry* FindEntry(const UVBQuestDefinition* Quest) const;

	FDelegateHandle GameEventHandle;
	FDelegateHandle StoryChangedHandle;
	FDelegateHandle SaveAppliedHandle;
	int32 NextRecentOrder = 1;

	// 세이브 적용 중에 사실이 바뀌어 판단을 미뤘다(HandleStoryChanged).
	bool bStoryChangedWhileApplyingSave = false;

	// 되찾지 못한 저장 줄. 다음 저장에 그대로 다시 쓴다(ReadSave).
	TArray<FVBSavedQuest> UnresolvedSavedQuests;

	// 처리 중 재진입 막기(클래스 주석). 처리 중에 온 사건은 PendingEvents 에 쌓는다.
	bool bProcessing = false;
	TArray<FVBGameEvent> PendingEvents;
};
