#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Narrative/Dialogue/VBSpokenLineComponent.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Enemy/VBEncounterGateComponent.h"
#include "Enemy/VBEncounterRules.h"
#include "Interaction/VBInteractableComponent.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "GameFramework/GameStateBase.h"

// 프롤로그 첫 구간 장치 둘의 단위 시험. 설계 .sw/prologue-opening 3.4·3.6a.
// 줄 보이는 시간(지정값 우선, 아니면 최소+글자당)과, 체크포인트가 «상태는 지금, 자리는 지정한 곳» 으로 쓰이는가를 잰다.
// 클라로 가는 RPC·자막 표시·실제 슬롯 저장은 PIE 가 본다.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSpokenLineSecondsTest, "Vowbound.Unit.Narrative.SpokenLineSeconds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSpokenLineSecondsTest::RunTest(const FString& Parameters)
{
	const UVBSpokenLineComponent* Lines = GetDefault<UVBSpokenLineComponent>();
	FVBSpokenLine Line;
	Line.Text = FText::FromString(TEXT("저기요."));
	TestEqual(TEXT("short line uses the minimum"), Lines->ResolveSeconds(Line), Lines->MinLineSeconds);

	Line.Text = FText::FromString(FString::ChrN(100, TEXT('가')));
	TestEqual(TEXT("long line scales with characters"), Lines->ResolveSeconds(Line), 100 * Lines->SecondsPerCharacter);

	Line.Duration = 0.5f;
	TestEqual(TEXT("authored duration wins"), Lines->ResolveSeconds(Line), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCheckpointRestoreAtTest, "Vowbound.Unit.Save.CheckpointRestoreAt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCheckpointRestoreAtTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACharacter* Pawn = World->SpawnActor<ACharacter>(FVector(100.0f, 200.0f, 300.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}
	UVBPlayerRestoreComponent* Restore = NewObject<UVBPlayerRestoreComponent>(Pawn);
	Pawn->AddInstanceComponent(Restore);
	Restore->RegisterComponent();

	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	FVBSaveContext Context;
	Context.World = World;
	Context.Pawn = Pawn;

	// 보통 저장: 지금 자리.
	Restore->WriteSave(*Save, Context);
	TestTrue(TEXT("plain save writes a transform"), Save->Progress.bHasSavedTransform);
	TestTrue(TEXT("plain save uses where the pawn stands"),
		Save->Progress.SavedPlayerTransform.GetLocation().Equals(Pawn->GetActorLocation(), 0.01));

	// 체크포인트: 지정한 자리.
	const FTransform Marker(FRotator(0.0f, 90.0f, 0.0f), FVector(1850.0f, 1300.0f, 90.0f));
	Context.RestoreTransformOverride = Marker;
	Restore->WriteSave(*Save, Context);
	TestTrue(TEXT("checkpoint uses the marker location"),
		Save->Progress.SavedPlayerTransform.GetLocation().Equals(Marker.GetLocation(), 0.01));
	TestTrue(TEXT("checkpoint uses the marker yaw"),
		FMath::IsNearlyEqual(Save->Progress.SavedPlayerTransform.Rotator().Yaw, 90.0, 0.01));
	return true;
}

// 체크포인트 슬롯(f3 검증 10-07): 활성 슬롯 -> 빈 슬롯 -> 둘 다 없으면 거절. 만석일 때 0번을 덮지 않는 것이 요점이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCheckpointSlotTest, "Vowbound.Unit.Save.CheckpointSlot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCheckpointSlotTest::RunTest(const FString& Parameters)
{
	auto Slot = [](int32 Index, bool bExists)
	{
		FVBSaveSlotMeta Meta;
		Meta.SlotIndex = Index;
		Meta.bExists = bExists;
		return Meta;
	};
	const TArray<FVBSaveSlotMeta> Full = { Slot(0, true), Slot(1, true), Slot(2, true) };
	const TArray<FVBSaveSlotMeta> OneFree = { Slot(0, true), Slot(1, false), Slot(2, true) };
	TestEqual(TEXT("active slot wins even when full"), VBSaveSlotPolicy::ResolveCheckpointSlot(2, Full), 2);
	TestEqual(TEXT("no active slot -> first empty slot"), VBSaveSlotPolicy::ResolveCheckpointSlot(INDEX_NONE, OneFree), 1);
	TestEqual(TEXT("no active slot and full -> refuse, not slot 0"), VBSaveSlotPolicy::ResolveCheckpointSlot(INDEX_NONE, Full), (int32)INDEX_NONE);
	return true;
}

// 조우 보류(.sw/cinematic-playback 10절 4): 꼬리표가 맞는 조우 문만 보류되고, 같은 값은 다시 알리지 않는다.
// 행동 트리 멈춤(AI 컨트롤러)과 공격 GA 막기(로스태그)는 ASC·컨트롤러가 있는 PIE 에서 본다 - 여기 액터는 ASC 가 없어 경고를 낸다.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEncounterHoldTest, "Vowbound.Unit.Encounter.Hold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEncounterHoldTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// 꼬리표 칸은 배치에서 적는 protected 칸이라 리플렉션으로 채운다. 태그 이름은 연산과 무관해 네이티브 태그를 빌린다.
	auto MakeGate = [World](FGameplayTag Tag) -> UVBEncounterGateComponent*
	{
		ACharacter* Pawn = World->SpawnActor<ACharacter>();
		UVBEncounterGateComponent* Gate = NewObject<UVBEncounterGateComponent>(Pawn);
		Pawn->AddInstanceComponent(Gate);
		Gate->RegisterComponent();
		const FStructProperty* Prop = FindFProperty<FStructProperty>(UVBEncounterGateComponent::StaticClass(), TEXT("EncounterTags"));
		Prop->ContainerPtrToValuePtr<FGameplayTagContainer>(Gate)->AddTag(Tag);
		return Gate;
	};
	UVBEncounterGateComponent* First = MakeGate(VBGameplayTags::State_Encounter_Held);
	UVBEncounterGateComponent* Other = MakeGate(VBGameplayTags::State_Dead);
	if (!TestNotNull(TEXT("gates"), First) || !TestNotNull(TEXT("gates"), Other))
	{
		return false;
	}

	int32 Broadcasts = 0;
	First->OnHeldChanged.AddLambda([&Broadcasts](bool) { ++Broadcasts; });

	FVBRuleContext Context;
	Context.World = World;
	FVBEffect_HoldEncounter Hold;
	Hold.EncounterTags.AddTag(VBGameplayTags::State_Encounter_Held);
	Hold.bHold = true;
	Hold.Apply(Context);
	TestTrue(TEXT("matching gate is held"), First->IsHeld());
	TestFalse(TEXT("other tag is untouched"), Other->IsHeld());
	TestEqual(TEXT("one broadcast"), Broadcasts, 1);

	Hold.Apply(Context);
	TestEqual(TEXT("same value does not broadcast again"), Broadcasts, 1);

	Hold.bHold = false;
	Hold.Apply(Context);
	TestFalse(TEXT("release"), First->IsHeld());
	TestEqual(TEXT("release broadcasts"), Broadcasts, 2);
	return true;
}

// 쓰러진 조우의 보류(Codex 03 지적 10-04): 새 보류는 안 걸고, 쓰러지기 전 걸린 보류는 걷되 방송하지 않는다(죽은 적의 행동 트리를 안 잇는다).
// 진행 중 공격 취소(CancelAbilities)는 ASC 가 있는 적이 필요해 PIE 확인 목록으로 간다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEncounterHoldDefeatedTest, "Vowbound.Unit.Encounter.HoldDefeated",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEncounterHoldDefeatedTest::RunTest(const FString& Parameters)
{
	using VBEncounterHold::Decide;
	TestTrue(TEXT("alive: hold applies and notifies"), Decide(false, true, false) == EVBEncounterHoldAction::ApplyAndNotify);
	TestTrue(TEXT("alive: release applies and notifies"), Decide(true, false, false) == EVBEncounterHoldAction::ApplyAndNotify);
	TestTrue(TEXT("same value is ignored"), Decide(true, true, false) == EVBEncounterHoldAction::Ignore);
	TestTrue(TEXT("defeated: new hold is ignored"), Decide(false, true, true) == EVBEncounterHoldAction::Ignore);
	TestTrue(TEXT("defeated: release is silent"), Decide(true, false, true) == EVBEncounterHoldAction::ApplySilently);
	TestTrue(TEXT("defeated: same value is ignored"), Decide(false, false, true) == EVBEncounterHoldAction::Ignore);
	return true;
}

// 상호작용 조건 칸(.sw/prologue-opening 14): 비면 쓸 수 있고, 거짓이면 완료(NotifyInteracted)도 안 난다.
// 실제 사실 저장소(게임 상태 컴포넌트)를 세워 참·거짓·시작 뒤 거짓 전환을 재고, 거절된 완료가 완료 효과를 안 돌리는지를
// 완료 효과가 켜는 표지 사실로 잰다(Codex 03 검토 10-04 - 저장소 없는 거짓만으로는 14.6 을 검증했다고 못 한다).
// GA 의 시작 거절·클라 알림·HUD 한 줄은 PIE 가 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractUseConditionsTest, "Vowbound.Unit.Interaction.UseConditions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractUseConditionsTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACharacter* Owner = World->SpawnActor<ACharacter>();
	UVBInteractableComponent* Target = NewObject<UVBInteractableComponent>(Owner);
	Owner->AddInstanceComponent(Target);
	Target->RegisterComponent();

	TestTrue(TEXT("no conditions: usable"), Target->PassesUseConditions(nullptr));
	TestTrue(TEXT("no conditions: completes"), Target->NotifyInteracted(nullptr));

	// 조건·완료 효과 칸은 배치에서 적는 protected 칸이라 리플렉션으로 채운다. 사실 태그는 연산과 무관해 네이티브 태그를 빌린다.
	auto ArrayOf = [Target](const TCHAR* Name) -> TArray<FInstancedStruct>*
	{
		const FArrayProperty* Prop = FindFProperty<FArrayProperty>(UVBInteractableComponent::StaticClass(), Name);
		return Prop ? Prop->ContainerPtrToValuePtr<TArray<FInstancedStruct>>(Target) : nullptr;
	};
	TArray<FInstancedStruct>* Conditions = ArrayOf(TEXT("UseConditions"));
	TArray<FInstancedStruct>* Effects = ArrayOf(TEXT("CompletionEffects"));
	if (!TestNotNull(TEXT("UseConditions property"), Conditions) || !TestNotNull(TEXT("CompletionEffects property"), Effects))
	{
		return false;
	}
	const FGameplayTag GateFact = VBGameplayTags::State_Encounter_Held;  // 조건이 읽는 사실
	const FGameplayTag DoneMarker = VBGameplayTags::State_Dead;          // 완료 효과가 켜는 표지 - 켜졌으면 효과가 돈 것이다
	FVBCondition_Fact Fact;
	Fact.Fact = GateFact;
	Conditions->Add(FInstancedStruct::Make(Fact));
	FVBEffect_SetFact Marker;
	Marker.Fact = DoneMarker;
	Effects->Add(FInstancedStruct::Make(Marker));

	// 저장소가 없으면 거짓(알 수 없는 것을 참으로 흘리지 않는다). 경고 둘.
	AddExpectedMessagePlain(TEXT("사실 저장소가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("no story: not usable"), Target->PassesUseConditions(nullptr));
	TestFalse(TEXT("no story: completion refused"), Target->NotifyInteracted(nullptr));

	// 실제 사실 저장소 - 게임 상태 컴포넌트(UVBStoryStateComponent::Get 이 찾는 자리).
	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
	UVBStoryStateComponent* Story = NewObject<UVBStoryStateComponent>(GameState);
	GameState->AddInstanceComponent(Story);
	Story->RegisterComponent();
	World->SetGameState(GameState);
	if (!TestTrue(TEXT("story is found through the world"), UVBStoryStateComponent::Get(Target) == Story))
	{
		return false;
	}

	TestFalse(TEXT("fact off: not usable"), Target->PassesUseConditions(nullptr));
	TestFalse(TEXT("fact off: completion refused"), Target->NotifyInteracted(nullptr));
	TestFalse(TEXT("fact off: completion effects did not run"), Story->HasFact(DoneMarker));

	Story->SetFact(GateFact, true);
	TestTrue(TEXT("fact on: usable"), Target->PassesUseConditions(nullptr));
	TestTrue(TEXT("fact on: completes"), Target->NotifyInteracted(nullptr));
	TestTrue(TEXT("fact on: completion effects ran"), Story->HasFact(DoneMarker));

	// 시작 때 참이었다가 완료 전에 거짓이 된 경우(동작 도중) - 완료 직전 재검사가 막는다.
	Story->SetFact(DoneMarker, false);
	TestTrue(TEXT("flip: usable at start"), Target->PassesUseConditions(nullptr));
	Story->SetFact(GateFact, false);
	TestFalse(TEXT("flip: completion refused"), Target->NotifyInteracted(nullptr));
	TestFalse(TEXT("flip: completion effects did not run"), Story->HasFact(DoneMarker));
	return true;
}
#endif
