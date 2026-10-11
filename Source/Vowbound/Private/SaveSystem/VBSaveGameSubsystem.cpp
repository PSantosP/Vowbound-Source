// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "SaveSystem/VBSaveGameSubsystem.h"

#include "SaveSystem/VBSaveGame.h"
#include "Data/VBGameFlowSettings.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/ActorComponent.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 글로벌 설정 슬롯 이름. 게임 진행과 무관하게 단일.
	const FString GSettingsSlotName = TEXT("VBSettings");

	// 단일 로컬 플레이어 가정. 분할화면/멀티 로컬 지원 시 확장.
	constexpr int32 GUserIndex = 0;
}

UVBSaveGameSubsystem* UVBSaveGameSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return nullptr;
	}

	if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UVBSaveGameSubsystem>();
		}
	}
	return nullptr;
}

void UVBSaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bStopping = false;

	// 설정은 게임 시작 직후 어디서든 즉시 필요(메인 메뉴 옵션 등)하므로 미리 보증해 둔다.
	LoadOrCreateSettings();
}

void UVBSaveGameSubsystem::Deinitialize()
{
	bStopping = true;
	++SaveGateRevision;
	PendingLoadId.Invalidate();
	SettingsSave = nullptr;
	ActiveGameSave = nullptr;

	Super::Deinitialize();
}

// -------------------------------------------------------------------- Settings

UVBSaveGame* UVBSaveGameSubsystem::LoadOrCreateSettings()
{
	if (SettingsSave)
	{
		return SettingsSave;
	}

	if (UGameplayStatics::DoesSaveGameExist(GSettingsSlotName, GUserIndex))
	{
		SettingsSave = Cast<UVBSaveGame>(UGameplayStatics::LoadGameFromSlot(GSettingsSlotName, GUserIndex));
	}

	// 디스크에 없거나(첫 실행) 캐스팅 실패(포맷 깨짐) 시 기본값으로 새로 생성.
	if (!SettingsSave)
	{
		SettingsSave = Cast<UVBSaveGame>(UGameplayStatics::CreateSaveGameObject(UVBSaveGame::StaticClass()));
	}

	return SettingsSave;
}

const FVBSettingsSaveData& UVBSaveGameSubsystem::GetSettingsData(const UObject* WorldContextObject)
{
	// 서브시스템도 세이브도 없는 경로(에디터 위젯 프리뷰, GameInstance 초기화 전)를 위한 기본값.
	// 정적 상수라 수명이 참조보다 길다.
	static const FVBSettingsSaveData DefaultSettings;

	if (UVBSaveGameSubsystem* Subsystem = Get(WorldContextObject))
	{
		if (const UVBSaveGame* Save = Subsystem->LoadOrCreateSettings())
		{
			// SettingsSave 는 Initialize 에서 보증되고 Deinitialize 전까지 교체되지 않는다 - 참조 안정.
			return Save->Settings;
		}
	}
	return DefaultSettings;
}

void UVBSaveGameSubsystem::UpdateSettings(const FVBSettingsSaveData& NewSettings)
{
	if (UVBSaveGame* Save = LoadOrCreateSettings())
	{
		Save->Settings = NewSettings;
		SaveSettings();
	}
}

void UVBSaveGameSubsystem::SaveSettings()
{
	UVBSaveGame* Save = LoadOrCreateSettings();
	Save->SaveVersion = UVBSaveGame::CurrentSaveVersion;
	Save->SaveTimestamp = FDateTime::UtcNow();

	// 설정은 작지만 저장 시점이 게임플레이 중일 수 있어 비동기로 히치를 피한다.
	UGameplayStatics::AsyncSaveGameToSlot(Save, GSettingsSlotName, GUserIndex, FAsyncSaveGameToSlotDelegate());
}

// -------------------------------------------------------------------- Game Slot

bool UVBSaveGameSubsystem::DoesGameSaveExist(int32 SlotIndex) const
{
	return UGameplayStatics::DoesSaveGameExist(MakeGameSlotName(SlotIndex), GUserIndex);
}

void UVBSaveGameSubsystem::SaveGameSlot(int32 SlotIndex)
{
	if (!CanWriteSlot(SlotIndex)) return;
	TStrongObjectPtr<ThisClass> KeepAlive(this);
	TGuardValue<bool> Saving(bSavingSnapshot, true);
	FlushSaveToSlot(GetOrCreateActiveGameSave(), SlotIndex);
}

FVBSaveHoldToken UVBSaveGameSubsystem::BeginSaveHold(FName Reason)
{
	// 이미 제출한 기록은 보류로 취소할 수 없다. 장면은 실제 기록 종료 뒤 시작한다.
	if (bStopping || bCompletingSave || !PendingWrites.IsEmpty()) return {};
	const FVBSaveHoldToken Token = SaveHolds.Acquire(Reason);
	if (Token.IsValid()) NotifySaveHoldsChanged();
	return IsSaveHoldActive(Token) ? Token : FVBSaveHoldToken{};
}

bool UVBSaveGameSubsystem::ConsumeInitialNewGameStart(FGuid AttemptId)
{
	if (bStopping || !AttemptId.IsValid() || GameStartContext.AttemptId != AttemptId
		|| GameStartContext.Intent != EVBGameStartIntent::NewGame || ConsumedNewGameAttempt == AttemptId) return false;
	ConsumedNewGameAttempt = AttemptId;
	return true;
}

bool UVBSaveGameSubsystem::EndSaveHold(const FVBSaveHoldToken& Token)
{
	if (bStopping || !SaveHolds.Release(Token)) return false;
	NotifySaveHoldsChanged();
	return true;
}

void UVBSaveGameSubsystem::NotifySaveHoldsChanged()
{
	++SaveGateRevision;
	OnSaveHoldsChanged.Broadcast();
}

bool UVBSaveGameSubsystem::CanWriteSlot(int32 SlotIndex)
{
	FName Reason;
	if (bStopping) Reason = TEXT("Unavailable");
	else if (SlotIndex < 0 || SlotIndex >= MaxSlotCount) Reason = TEXT("InvalidSlot");
	else if (SaveHolds.IsHeld()) Reason = TEXT("Held");
	else if (bSavingSnapshot || bCompletingSave || bChangingStartContext || PendingWrites.Contains(SlotIndex)) Reason = TEXT("Busy");
	if (Reason.IsNone()) return true;
	VB_LOG(Warning, "게임 저장 거절: 슬롯 %d, 이유 %s", SlotIndex, *Reason.ToString());
	// 거절 통지 안의 재요청은 방송을 반복하지 않는다.
	if (!bCompletingSave)
	{
		TGuardValue<bool> Completing(bCompletingSave, true);
		OnGameSaveRefused.Broadcast(SlotIndex, Reason);
	}
	return false;
}

void UVBSaveGameSubsystem::FlushSaveToSlot(UVBSaveGame* Snapshot, int32 SlotIndex)
{
	if (!Snapshot || bStopping) return;
	Snapshot->SaveVersion = UVBSaveGame::CurrentSaveVersion;
	Snapshot->SaveTimestamp = FDateTime::UtcNow();
	const FGuid RequestId = FGuid::NewGuid();
	PendingWrites.Add(SlotIndex, RequestId);

	const FString SlotName = MakeGameSlotName(SlotIndex);

	// SlotIndex 를 캡처해 완료 콜백에서 그대로 브로드캐스트. WeakObjectPtr 로 수명 안전.
	FAsyncSaveGameToSlotDelegate Delegate;
	Delegate.BindLambda(
		[WeakThis = TWeakObjectPtr<UVBSaveGameSubsystem>(this), SlotIndex, RequestId]
		(const FString& /*Slot*/, const int32 /*UserIdx*/, bool bSuccess)
		{
			if (UVBSaveGameSubsystem* StrongThis = WeakThis.Get(); StrongThis && !StrongThis->bStopping)
			{
				TStrongObjectPtr<ThisClass> KeepAlive(StrongThis);
				const FGuid* Pending = StrongThis->PendingWrites.Find(SlotIndex);
				if (!Pending || *Pending != RequestId) return;
				StrongThis->PendingWrites.Remove(SlotIndex);
				TGuardValue<bool> Completing(StrongThis->bCompletingSave, true);
				StrongThis->OnGameSaveCompleted.Broadcast(SlotIndex, bSuccess);
			}
		});

#if WITH_DEV_AUTOMATION_TESTS
	if (TestSaveDispatch)
	{
		TestSaveDispatch(Snapshot, SlotName, GUserIndex, Delegate);
		return;
	}
#endif
	UGameplayStatics::AsyncSaveGameToSlot(Snapshot, SlotName, GUserIndex, Delegate);
}

void UVBSaveGameSubsystem::LoadGameSlot(int32 SlotIndex)
{
	if (bStopping || bChangingStartContext) return;
	const FGuid LoadId = FGuid::NewGuid();
	const FGuid SourceAttempt = GameStartContext.AttemptId;
	PendingLoadId = LoadId;
	const FString SlotName = MakeGameSlotName(SlotIndex);

	// 존재하지 않는 슬롯은 비동기 로드를 걸지 않고 즉시 실패 통지.
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, GUserIndex))
	{
		OnGameLoadCompleted.Broadcast(SlotIndex, false);
		return;
	}

	FAsyncLoadGameFromSlotDelegate Delegate;
	Delegate.BindLambda(
		[WeakThis = TWeakObjectPtr<UVBSaveGameSubsystem>(this), SlotIndex, LoadId, SourceAttempt]
		(const FString& /*Slot*/, const int32 /*UserIdx*/, USaveGame* LoadedData)
		{
			UVBSaveGameSubsystem* StrongThis = WeakThis.Get();
			if (!StrongThis || StrongThis->bStopping)
			{
				return;
			}

			UVBSaveGame* VBSave = Cast<UVBSaveGame>(LoadedData);
			TStrongObjectPtr<ThisClass> KeepAlive(StrongThis);
			if (StrongThis->PendingLoadId != LoadId || StrongThis->GameStartContext.AttemptId != SourceAttempt)
			{
				StrongThis->OnGameLoadCompleted.Broadcast(SlotIndex, false);
				return;
			}
			StrongThis->PendingLoadId.Invalidate();
			if (VBSave)
			{
				TGuardValue<bool> Changing(StrongThis->bChangingStartContext, true);
				VBSave = StrongThis->MigrateIfNeeded(VBSave);
				StrongThis->ActiveGameSave = VBSave;
				// 로드한 슬롯이 곧 활성 슬롯 + Continue 적용 대기. LoadGameSlot(async)의 유일 호출자는 Continue 흐름이다
				// (슬롯 목록 메타는 동기 LoadGameFromSlot 을 쓰므로 이 경로를 타지 않는다). New Game 은 StartNewGame 경유라 여기 안 옴.
				StrongThis->ActiveSlotIndex = SlotIndex;
				StrongThis->bPendingContinueApply = true;
				StrongThis->GameStartContext = {FGuid::NewGuid(), EVBGameStartIntent::Continue};
				StrongThis->SaveHolds.AdvanceGeneration(StrongThis->GameStartContext.AttemptId);
				StrongThis->NotifySaveHoldsChanged();
				StrongThis->OnGameStartContextChanged.Broadcast();
			}
			StrongThis->OnGameLoadCompleted.Broadcast(SlotIndex, VBSave != nullptr);
		});

	UGameplayStatics::AsyncLoadGameFromSlot(SlotName, GUserIndex, Delegate);
}

void UVBSaveGameSubsystem::DeleteGameSave(int32 SlotIndex)
{
	FName Reason;
	if (bStopping) Reason = TEXT("Unavailable");
	else if (SlotIndex < 0 || SlotIndex >= MaxSlotCount) Reason = TEXT("InvalidSlot");
	else if (bSavingSnapshot || bCompletingSave || bChangingStartContext || PendingWrites.Contains(SlotIndex))
		Reason = TEXT("Busy");
	else if (SaveHolds.IsHeld() && ActiveSlotIndex == SlotIndex) Reason = TEXT("Held");
	if (!Reason.IsNone())
	{
		VB_LOG(Warning, "게임 저장 삭제 거절: 슬롯 %d, 이유 %s", SlotIndex, *Reason.ToString());
		return;
	}
#if WITH_DEV_AUTOMATION_TESTS
	if (TestDeleteDispatch)
	{
		TestDeleteDispatch(MakeGameSlotName(SlotIndex), GUserIndex);
		return;
	}
#endif
	UGameplayStatics::DeleteGameInSlot(MakeGameSlotName(SlotIndex), GUserIndex);
}

void UVBSaveGameSubsystem::StartNewGame()
{
	BeginNewGame(INDEX_NONE);
}

FGuid UVBSaveGameSubsystem::BeginNewGame(int32 SlotIndex)
{
	if (bStopping || bChangingStartContext) return {};
	TGuardValue<bool> Changing(bChangingStartContext, true);
	// 기존 활성 세이브를 버리고 깨끗한 새 세이브로 교체. 기본값은 struct 멤버 초기화로 보장.
	ActiveGameSave = Cast<UVBSaveGame>(UGameplayStatics::CreateSaveGameObject(UVBSaveGame::StaticClass()));

	// 슬롯 미확정. 첫 저장 때 정해진다 - 시작만으로 슬롯을 먹지 않는다.
	ActiveSlotIndex = SlotIndex;

	// '적용 대기'는 세우지 않는다 - 새 레벨에서 기본 상태를 유지해야 하므로.
	bPendingContinueApply = false;
	GameStartContext = {FGuid::NewGuid(), EVBGameStartIntent::NewGame};
	PendingLoadId.Invalidate();
	SaveHolds.AdvanceGeneration(GameStartContext.AttemptId);
	NotifySaveHoldsChanged();
	OnGameStartContextChanged.Broadcast();

	// 디스크에 쓰지 않는다. 새 게임을 시작한 것과 그 진행을 남기겠다는 것은 다른 결정이다.
	return GameStartContext.AttemptId;
}

void UVBSaveGameSubsystem::StartNewGameInSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= MaxSlotCount) return;
	const FGuid Started = BeginNewGame(SlotIndex);
	if (!Started.IsValid() || bStopping) return;

	// 사용자가 목록에서 직접 고른 슬롯이므로 즉시 점유가 옳다 - 그래야 "Continue" 대상이 바로 생기고
	// 덮어쓰기 의사도 그 자리에서 확정된다.
	SaveGameSlot(SlotIndex);
}

void UVBSaveGameSubsystem::TravelToGameplayLevel()
{
	const UVBGameFlowSettings* Settings = GetDefault<UVBGameFlowSettings>();
	if (!Settings || Settings->GameplayLevel.IsNull())
	{
		// 조용히 넘기지 않는다 - 증상이 "새 게임 버튼을 눌렀는데 아무 일도 안 남"으로만 나타나기 때문이다.
		// 종전 UVBMainMenuWidget 이 정확히 그 무음 실패였다.
		VB_LOG(Warning, "GameplayLevel 미지정 - 레벨 전환 불가. Project Settings > Game > Vowbound Game Flow 확인");
		return;
	}

	// WorldContext 로 GameInstance 를 넘긴다. 이 서브시스템은 Within=GameInstance 라 소유자가 곧 월드의 주인이고,
	// UGameInstance::GetWorld() 가 정본이다. this 로 넘기면 UObject::GetWorld() 의 Outer 사슬에 의존하게 된다.
	UGameplayStatics::OpenLevelBySoftObjectPtr(GetGameInstance(), Settings->GameplayLevel);
}

bool UVBSaveGameSubsystem::ConsumePendingContinueApply()
{
	const bool bWasPending = bPendingContinueApply;
	bPendingContinueApply = false; // 1회성: 소비 즉시 내려 중복 적용 방지.
	return bWasPending;
}

void UVBSaveGameSubsystem::SaveCurrentSlot()
{
	// 서브시스템은 폰 참조가 없으므로 GI 월드에서 로컬 폰을 해석해 단일 저장경로(SaveCurrentGame)로 위임.
	// → 스냅샷 없이 flush 만 하던 갭 제거. 폰 null 이면 SaveCurrentGame 내부에서 경고 후 취소.
	APawn* Pawn = nullptr;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UWorld* World = GI->GetWorld())
		{
			if (const APlayerController* PC = World->GetFirstPlayerController())
			{
				Pawn = PC->GetPawn();
			}
		}
	}
	SaveCurrentGame(Pawn);
}

void UVBSaveGameSubsystem::SaveCurrentGame(APawn* Pawn)
{
	// 슬롯을 고를 화면이 없는 경로. 해석은 ResolveSaveSlot 이 소유하고, 저장 자체는 아래 한 곳뿐이다.
	SaveCurrentGameToSlot(Pawn, ResolveSaveSlot());
}

int32 UVBSaveGameSubsystem::ResolveSaveSlot() const
{
	if (ActiveSlotIndex != INDEX_NONE)
	{
		return ActiveSlotIndex;
	}

	// 빈 슬롯을 먼저 고른다. 어느 것을 덮을지는 사용자가 골라야 하는 문제라
	// 만석이면 0 으로 가되 반드시 경고를 남긴다 - 사용자가 고르는 경로는 슬롯 목록 저장 모드다.
	for (const FVBSaveSlotMeta& Meta : GetAllSlotMetadata())
	{
		if (!Meta.bExists)
		{
			VB_LOG(Log, "ResolveSaveSlot: 활성 슬롯 미확정 -> 빈 슬롯 %d 사용", Meta.SlotIndex);
			return Meta.SlotIndex;
		}
	}

	VB_LOG(Warning, "ResolveSaveSlot: 빈 슬롯이 없어 슬롯 0 을 덮는다 - 슬롯을 고르는 저장 화면을 거쳐야 한다");
	return 0;
}

namespace
{
	// 폰 하나의 저장 문맥. 일반 저장과 체크포인트가 같은 것을 만든다.
	FVBSaveContext MakePawnSaveContext(APawn& Pawn)
	{
		FVBSaveContext Context;
		Context.World = Pawn.GetWorld();
		Context.Pawn = &Pawn;
		Context.PlayerController = Pawn.GetController<APlayerController>();
		Context.PlayerState = Pawn.GetPlayerState();
		return Context;
	}
}

bool UVBSaveGameSubsystem::SaveCurrentGameToSlot(APawn* Pawn, int32 SlotIndex)
{
	if (!Pawn)
	{
		VB_LOG(Warning, "SaveCurrentGameToSlot: Pawn null — 저장 취소");
		return false;
	}
	return WriteContextToSlot(MakePawnSaveContext(*Pawn), SlotIndex);
}

bool UVBSaveGameSubsystem::SaveCheckpoint(APawn* Pawn, const FTransform& RestoreAt)
{
	if (!Pawn)
	{
		VB_LOG(Warning, "SaveCheckpoint: Pawn null — 저장 취소");
		return false;
	}
	FVBSaveContext Context = MakePawnSaveContext(*Pawn);
	Context.RestoreTransformOverride = RestoreAt;
	const int32 Slot = VBSaveSlotPolicy::ResolveCheckpointSlot(ActiveSlotIndex, GetAllSlotMetadata());
	if (Slot == INDEX_NONE)
	{
		// 막을 수 있는 덮어쓰기라 막는다. 슬롯 목록 화면에서 저장해 활성 슬롯이 정해지면 그 뒤 체크포인트는 그 슬롯에 쓴다.
		VB_LOG(Warning, "SaveCheckpoint: 활성 슬롯이 없고 빈 슬롯도 없다 - 기존 세이브를 덮지 않고 체크포인트를 건너뛴다");
		return false;
	}
	VB_LOG(Log, "SaveCheckpoint: 슬롯 %d, 돌아올 자리 %s", Slot, *RestoreAt.GetLocation().ToString());
	return WriteContextToSlot(Context, Slot);
}

int32 VBSaveSlotPolicy::ResolveCheckpointSlot(int32 ActiveSlot, const TArray<FVBSaveSlotMeta>& Slots)
{
	if (ActiveSlot != INDEX_NONE)
	{
		return ActiveSlot;
	}
	for (const FVBSaveSlotMeta& Meta : Slots)
	{
		if (!Meta.bExists)
		{
			return Meta.SlotIndex;
		}
	}
	return INDEX_NONE;
}

bool UVBSaveGameSubsystem::WriteContextToSlot(const FVBSaveContext& Context, int32 SlotIndex)
{
	if (!CanWriteSlot(SlotIndex)) return false;
	TStrongObjectPtr<ThisClass> KeepAlive(this);
	TGuardValue<bool> Saving(bSavingSnapshot, true);
	const uint64 Revision = SaveGateRevision;
	const FGuid Attempt = GameStartContext.AttemptId;
	TStrongObjectPtr<UWorld> World(Context.World);
	TStrongObjectPtr<APawn> Pawn(Context.Pawn);
	TStrongObjectPtr<APlayerController> Controller(Context.PlayerController);
	TStrongObjectPtr<APlayerState> PlayerState(Context.PlayerState);
	const auto IsCurrent = [&]
	{
		FName Reason;
		if (bStopping) Reason = TEXT("Stopping");
		else if (Revision != SaveGateRevision) Reason = TEXT("GateRevisionChanged");
		else if (Attempt != GameStartContext.AttemptId) Reason = TEXT("StartAttemptChanged");
		else if (SaveHolds.IsHeld()) Reason = TEXT("Held");
		else if (!IsValid(World.Get()) || World->bIsTearingDown) Reason = TEXT("WorldUnavailable");
		else if (World->GetGameInstance() != GetGameInstance() || World->GetNetMode() == NM_Client) Reason = TEXT("WorldAuthority");
		else if (!IsValid(Pawn.Get()) || Pawn->IsActorBeingDestroyed() || Pawn->GetWorld() != World.Get()) Reason = TEXT("PawnUnavailable");
		else if (!IsValid(Controller.Get()) || Controller->IsActorBeingDestroyed() || Controller->GetWorld() != World.Get()) Reason = TEXT("ControllerUnavailable");
		else if (Controller->GetPawn() != Pawn.Get() || Pawn->GetController() != Controller.Get()) Reason = TEXT("PossessionChanged");
		else if (!IsValid(PlayerState.Get()) || Pawn->GetPlayerState() != PlayerState.Get() || Controller->PlayerState != PlayerState.Get()) Reason = TEXT("PlayerStateChanged");
		if (!Reason.IsNone()) VB_LOG(Warning, "저장 문맥 거절: 슬롯 %d, 이유 %s", SlotIndex, *Reason.ToString());
		return Reason.IsNone();
	};
	if (!IsCurrent()) return false;
	// 참여자가 각자 자기 구역을 쓴다(빌드 = AVBPlayerState, 위치 = UVBPlayerRestoreComponent, 문·스위치 = UVBInteractableSubsystem,
	//   사실 = UVBStoryStateComponent). 하나도 없으면 멈춘다 - 빈 세이브를 «저장됨» 으로 알리면 불러올 때에야 비었다는 것을 안다.
	UVBSaveParticipantSubsystem* Registry = Context.World ? Context.World->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr;
	const TArray<IVBSaveParticipant*> Participants = Registry ? Registry->GetParticipants() : TArray<IVBSaveParticipant*>();
	if (Participants.Num() == 0)
	{
		VB_LOG(Error, "WriteContextToSlot: 이 월드에 저장 참여자가 없다 - 저장 취소(빈 세이브를 쓰지 않는다)");
		return false;
	}
	TArray<TStrongObjectPtr<UObject>> Writers;
	for (IVBSaveParticipant* Participant : Participants) Writers.Emplace(Cast<UObject>(Participant));
	TStrongObjectPtr<UVBSaveGame> Active(GetOrCreateActiveGameSave());
	TStrongObjectPtr<UVBSaveGame> Game(DuplicateObject(Active.Get(), GetTransientPackage()));
	if (!Game.IsValid() || !IsCurrent()) return false;
	for (int32 Index = 0; Index < Participants.Num(); ++Index)
	{
		const UObject* Object = Writers[Index].Get();
		const UActorComponent* Component = Cast<UActorComponent>(Object);
		const AActor* Actor = Cast<AActor>(Object);
		if (!IsCurrent() || !IsValid(Object) || Object->GetWorld() != World.Get()
			|| (Component && !Component->IsRegistered()) || (Actor && Actor->IsActorBeingDestroyed())) return false;
		Participants[Index]->WriteSave(*Game.Get(), Context);
		if (!IsCurrent()) return false;
	}
	if (Registry->GetParticipants() != Participants) return false;
	VB_LOG(Log, "WriteContextToSlot: 참여자 %d 가 슬롯 %d 에 썼다", Participants.Num(), SlotIndex);

	// 쓴 자리가 곧 활성 슬롯이 된다 - 이후 Continue/SaveCurrentSlot 이 같은 자리를 가리켜야 한다.
	ActiveSlotIndex = SlotIndex;
	Game->SaveVersion = UVBSaveGame::CurrentSaveVersion;
	Game->SaveTimestamp = FDateTime::UtcNow();
	// 공개 API가 돌려준 객체의 정체성은 유지하고, 검증한 저장 프로퍼티만 한 번 게시한다.
	for (TFieldIterator<FProperty> Property(Game->GetClass()); Property; ++Property)
	{
		if (Property->HasAnyPropertyFlags(CPF_SaveGame)) Property->CopyCompleteValue_InContainer(Active.Get(), Game.Get());
	}

	FlushSaveToSlot(Game.Get(), SlotIndex); // async → OnGameSaveCompleted
	return true;
}

int32 UVBSaveGameSubsystem::ApplyActiveSave(const FVBSaveContext& Context)
{
	if (!ActiveGameSave)
	{
		VB_LOG(Error, "ApplyActiveSave: 적용할 활성 세이브가 없다");
		return 0;
	}
	UVBSaveParticipantSubsystem* Registry = Context.World ? Context.World->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr;
	const int32 Applied = Registry ? Registry->ReadAll(*ActiveGameSave, Context) : 0;
	if (Applied == 0)
	{
		VB_LOG(Error, "ApplyActiveSave: 이 월드에 저장 참여자가 없다 - 세이브가 적용되지 않았다");
	}
	return Applied;
}

UVBSaveGame* UVBSaveGameSubsystem::GetOrCreateActiveGameSave()
{
	if (!ActiveGameSave)
	{
		ActiveGameSave = Cast<UVBSaveGame>(UGameplayStatics::CreateSaveGameObject(UVBSaveGame::StaticClass()));
	}
	return ActiveGameSave;
}

FString UVBSaveGameSubsystem::MakeGameSlotName(int32 SlotIndex)
{
	return FString::Printf(TEXT("VBGameSave_%d"), SlotIndex);
}

// -------------------------------------------------------------------- Slot Metadata

bool UVBSaveGameSubsystem::GetSlotMetadata(int32 SlotIndex, FVBSaveSlotMeta& OutMeta) const
{
	// 빈 결과로 초기화하되 슬롯 번호는 항상 채워 UI 가 빈 슬롯도 그릴 수 있게 한다.
	OutMeta = FVBSaveSlotMeta();
	OutMeta.SlotIndex = SlotIndex;

	const FString SlotName = MakeGameSlotName(SlotIndex);
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, GUserIndex))
	{
		return false;
	}

	// 목록 표시는 빈도가 낮고 슬롯 수가 적어 동기 로드 허용(메뉴 진입 1회).
	const UVBSaveGame* Save = Cast<UVBSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, GUserIndex));
	if (!Save)
	{
		// 파일은 있으나 캐스팅 실패(포맷 손상) — 존재하지 않는 것으로 처리.
		return false;
	}

	OutMeta.bExists = true;
	OutMeta.DisplayName = Save->SlotDisplayName;
	OutMeta.Timestamp = Save->SaveTimestamp;
	OutMeta.PlayTimeSeconds = Save->Progress.PlayTimeSeconds;
	OutMeta.LastLevelName = Save->Progress.LastLevelName;
	return true;
}

TArray<FVBSaveSlotMeta> UVBSaveGameSubsystem::GetAllSlotMetadata() const
{
	TArray<FVBSaveSlotMeta> Result;
	Result.Reserve(MaxSlotCount);

	for (int32 SlotIndex = 0; SlotIndex < MaxSlotCount; ++SlotIndex)
	{
		FVBSaveSlotMeta Meta;
		GetSlotMetadata(SlotIndex, Meta); // 빈 슬롯도 SlotIndex 채워 포함
		Result.Add(Meta);
	}

	return Result;
}

int32 UVBSaveGameSubsystem::GetMostRecentSlotIndex() const
{
	int32 BestSlot = INDEX_NONE;
	FDateTime BestTime = FDateTime::MinValue();

	// 존재하는 슬롯 중 가장 최근 저장 시각을 가진 슬롯을 고른다.
	// (UVBMainMenuWidget 에 있던 정책을 그대로 이관 — 메인메뉴/게임오버가 같은 세이브를 가리키게 하는 단일 소스.)
	for (const FVBSaveSlotMeta& Meta : GetAllSlotMetadata())
	{
		if (Meta.bExists && Meta.Timestamp > BestTime)
		{
			BestTime = Meta.Timestamp;
			BestSlot = Meta.SlotIndex;
		}
	}

	return BestSlot;
}

bool UVBSaveGameSubsystem::HasAnySaveGame() const
{
	return GetMostRecentSlotIndex() >= 0;
}

UVBSaveGame* UVBSaveGameSubsystem::MigrateIfNeeded(UVBSaveGame* InSave) const
{
	if (!InSave)
	{
		return nullptr;
	}

	// 확장 지점: 버전이 올라가면 여기서 단계별 변환을 수행한다.
	// 예) if (InSave->SaveVersion < 2) { /* v1 → v2 필드 보정 */ InSave->SaveVersion = 2; }
	if (InSave->SaveVersion < UVBSaveGame::CurrentSaveVersion)
	{
		// 아직 v1 단일 버전 — 보정할 항목 없음. 버전만 현재로 끌어올린다.
		InSave->SaveVersion = UVBSaveGame::CurrentSaveVersion;
	}

	return InSave;
}
