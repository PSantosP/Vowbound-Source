#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Enemy/VBEncounterGateComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Narrative/Story/VBAreaTrigger.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "GameplayTagContainer.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FConsumerTags
	{
		FGameplayTag Once, LegacyOnce, Applied, Event, Required, Woken, Done, DefeatedCount;
	};

	const FConsumerTags& ConsumerTags()
	{
		// 처음 fixture가 실행될 때만 조회한다. 태그 사전/ini를 바꾸지 않으며 제품 World에는 효과를 적용하지 않는다.
		static const FConsumerTags Tags{
			FGameplayTag::RequestGameplayTag(FName(TEXT("Story.Prologue.FirstEncounterEngaged")), false),
			FGameplayTag::RequestGameplayTag(FName(TEXT("Story.Prologue.Woke")), false),
			FGameplayTag::RequestGameplayTag(FName(TEXT("Story.Prologue.NurseStationChecked")), false),
			FGameplayTag::RequestGameplayTag(FName(TEXT("Encounter.Prologue.FirstEnemy")), false),
			FGameplayTag::RequestGameplayTag(FName(TEXT("Story.Test.Never")), false),
			FGameplayTag::RequestGameplayTag(FName(TEXT("Story.Prologue.WeaponFound")), false),
			FGameplayTag::RequestGameplayTag(FName(TEXT("Story.Prologue.FirstEncounterDone")), false),
			FGameplayTag::RequestGameplayTag(FName(TEXT("Story.Test.ErrandDone")), false)
		};
		return Tags;
	}
	constexpr EAutomationTestFlags ConsumerFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

// Set instance data before BeginPlay and call the real consumer. Never inject a Ready result.
class FVBEncounterConsumerTestAccess
{
public:
	static void ConfigureArea(AVBAreaTrigger& Area, bool bRequirePreparation, FGameplayTag Once)
	{
		Area.bRequirePlayerPreparation = bRequirePreparation;
		Area.OnceFact = Once;
		Area.EventTags.AddTag(ConsumerTags().Event);
		FVBEffect_AddCounter Effect;
		Effect.Counter = ConsumerTags().Applied;
		Effect.Delta = 1;
		Area.Effects.Add(FInstancedStruct::Make<FVBEffect_AddCounter>(Effect));
	}
	static void ConfigureGate(UVBEncounterGateComponent& Gate, AVBAreaTrigger* Area, bool bRequirePreparation)
	{
		Gate.bRequirePlayerPreparation = bRequirePreparation;
		Gate.WakeArea = Area;
		if (!bRequirePreparation)
		{
			FVBCondition_Fact Condition;
			Condition.Fact = ConsumerTags().Required;
			Gate.WakeConditions.Add(FInstancedStruct::Make<FVBCondition_Fact>(Condition));
		}
		FVBEffect_SetFact Woken;
		Woken.Fact = ConsumerTags().Woken;
		Gate.WokenEffects.Add(FInstancedStruct::Make<FVBEffect_SetFact>(Woken));
		FVBEffect_SetFact Done;
		Done.Fact = ConsumerTags().Done;
		Gate.DefeatedEffects.Add(FInstancedStruct::Make<FVBEffect_SetFact>(Done));
		FVBEffect_AddCounter Count;
		Count.Counter = ConsumerTags().DefeatedCount;
		Count.Delta = 1;
		Gate.DefeatedEffects.Add(FInstancedStruct::Make<FVBEffect_AddCounter>(Count));
	}
	static void SetWakeArea(UVBEncounterGateComponent& Gate, AVBAreaTrigger* Area) { Gate.WakeArea = Area; }
	static bool ValidWakeArea(const UVBEncounterGateComponent& Gate) { return Gate.HasValidWakeArea(); }
	static bool WarnedWakeArea(const UVBEncounterGateComponent& Gate) { return Gate.bWarnedInvalidWakeArea; }
	static void Evaluate(AVBAreaTrigger& Area) { Area.Evaluate(); }
	static void Evaluate(UVBEncounterGateComponent& Gate) { Gate.EvaluateWake(); }
	static bool Admit(AVBAreaTrigger& Area, APawn& Pawn) { return Area.TryFire(Pawn); }
	static bool WasSatisfied(const AVBAreaTrigger& Area, APawn* Pawn)
	{
		const bool* Value = Area.LastSatisfied.Find(Pawn);
		return Value && *Value;
	}
	static bool Pending(const AVBAreaTrigger& Area) { return Area.bEvaluatePending; }
	static bool Pending(const UVBEncounterGateComponent& Gate) { return Gate.bEvaluatePending; }
	static APawn* BoundPawn(const AVBAreaTrigger& Area) { return Area.PlayerBinding.GetPawn(); }
	static APawn* BoundPawn(const UVBEncounterGateComponent& Gate) { return Gate.PlayerBinding.GetPawn(); }
};

namespace
{
	struct FConsumerEnemy
	{
		AActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		UVBEncounterGateComponent* Gate = nullptr;
	};

	struct FConsumerWorld
	{
		UWorld* World = nullptr;
		UVBStoryStateComponent* Story = nullptr;
		APlayerController* PC = nullptr;
		ACharacter* Pawn = nullptr;
		UVBPlayerReadinessComponent* Preparation = nullptr;
		AVBAreaTrigger* Area = nullptr;
		AVBAreaTrigger* LegacyArea = nullptr;
		FConsumerEnemy Enemy;
		int32 AreaEvents = 0;

		~FConsumerWorld()
		{
			if (!World) return;
			// CreateWorld(nullptr)로 만든 이 fixture의 패키지만 종료 중 보호/정리한다.
			const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
			// BeginPlay를 마친 격리 World는 CleanupWorld 전에 정상 종료한다.
			if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
			OwnedPackage->SetDirtyFlag(false);
		}

		ACharacter* SpawnPawn(UVBPlayerReadinessComponent*& OutPreparation)
		{
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Spawn.bDeferConstruction = true;
			ACharacter* NewPawn = World->SpawnActor<ACharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
			if (!NewPawn) return nullptr;
			// 초기/후속 스폰 모두 자동 Controller를 만들지 않는다. 이 fixture가 명시적으로 Possess한다.
			NewPawn->AutoPossessAI = EAutoPossessAI::Disabled;
			NewPawn->AutoPossessPlayer = EAutoReceiveInput::Disabled;
			NewPawn->FinishSpawning(FTransform::Identity);
			UCapsuleComponent* Capsule = NewPawn->GetCapsuleComponent();
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Capsule->SetCollisionObjectType(ECC_Pawn);
			Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
			Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
			Capsule->SetGenerateOverlapEvents(true);
			NewPawn->GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			NewPawn->GetMesh()->SetGenerateOverlapEvents(false);
			OutPreparation = NewObject<UVBPlayerReadinessComponent>(NewPawn);
			NewPawn->AddInstanceComponent(OutPreparation);
			OutPreparation->RegisterComponent();
			return NewPawn;
		}

		FConsumerEnemy SpawnEnemy(bool bRequirePreparation)
		{
			FConsumerEnemy Result;
			Result.Actor = World->SpawnActor<AActor>();
			if (!Result.Actor) return Result;
			USceneComponent* Root = NewObject<USceneComponent>(Result.Actor);
			Result.Actor->AddInstanceComponent(Root);
			Result.Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Result.ASC = NewObject<UAbilitySystemComponent>(Result.Actor);
			Result.Actor->AddInstanceComponent(Result.ASC);
			Result.ASC->RegisterComponent();
			Result.ASC->InitAbilityActorInfo(Result.Actor, Result.Actor);
			UVBHealthAttributeSet* Health = NewObject<UVBHealthAttributeSet>(Result.Actor);
			Health->InitMaxHealth(100.0f);
			Health->InitHealth(100.0f);
			Result.ASC->AddAttributeSetSubobject(Health);
			Result.Gate = NewObject<UVBEncounterGateComponent>(Result.Actor);
			Result.Actor->AddInstanceComponent(Result.Gate);
			FVBEncounterConsumerTestAccess::ConfigureGate(*Result.Gate, Area, bRequirePreparation);
			Result.Gate->RegisterComponent();
			return Result;
		}

		bool Initialize(FAutomationTestBase& Test, bool bControllerAtStart = true)
		{
			if (!Test.TestNotNull(TEXT("engine"), GEngine)) return false;
			const FConsumerTags& Tags = ConsumerTags();
			bool bTagsValid = Test.TestTrue(TEXT("registered fixture once key"), Tags.Once.IsValid());
			bTagsValid &= Test.TestTrue(TEXT("registered fixture legacy once key"), Tags.LegacyOnce.IsValid());
			bTagsValid &= Test.TestTrue(TEXT("registered fixture applied counter key"), Tags.Applied.IsValid());
			bTagsValid &= Test.TestTrue(TEXT("registered fixture event key"), Tags.Event.IsValid());
			bTagsValid &= Test.TestTrue(TEXT("registered fixture required key"), Tags.Required.IsValid());
			bTagsValid &= Test.TestTrue(TEXT("registered fixture woken key"), Tags.Woken.IsValid());
			bTagsValid &= Test.TestTrue(TEXT("registered fixture done key"), Tags.Done.IsValid());
			bTagsValid &= Test.TestTrue(TEXT("registered fixture defeated counter key"), Tags.DefeatedCount.IsValid());
			if (!bTagsValid) return false;
			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("isolated consumer world"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			AGameStateBase* GS = World->SpawnActor<AGameStateBase>();
			if (!Test.TestNotNull(TEXT("game state"), GS)) return false;
			World->SetGameState(GS);
			Story = NewObject<UVBStoryStateComponent>(GS);
			GS->AddInstanceComponent(Story);
			Story->RegisterComponent();
			Pawn = SpawnPawn(Preparation);
			if (!Test.TestNotNull(TEXT("player pawn"), Pawn)) return false;
			if (!Test.TestTrue(TEXT("fixture disables automatic possession"),
				Pawn->AutoPossessAI == EAutoPossessAI::Disabled && Pawn->AutoPossessPlayer == EAutoReceiveInput::Disabled)) return false;
			if (!Test.TestTrue(TEXT("pawn has no automatic controller before explicit possession"), Pawn->GetController() == nullptr)) return false;
			if (bControllerAtStart)
			{
				PC = World->SpawnActor<APlayerController>();
				if (!Test.TestNotNull(TEXT("player controller"), PC)) return false;
				PC->PlayerState = World->SpawnActor<APlayerState>();
				if (!Test.TestNotNull(TEXT("player state"), PC->PlayerState.Get())) return false;
				PC->Possess(Pawn);
			}
			Area = World->SpawnActor<AVBAreaTrigger>();
			LegacyArea = World->SpawnActor<AVBAreaTrigger>();
			if (!Test.TestNotNull(TEXT("preparation area"), Area) || !Test.TestNotNull(TEXT("legacy area"), LegacyArea)) return false;
			FVBEncounterConsumerTestAccess::ConfigureArea(*Area, true, ConsumerTags().Once);
			FVBEncounterConsumerTestAccess::ConfigureArea(*LegacyArea, false, ConsumerTags().LegacyOnce);
			Enemy = SpawnEnemy(true);
			if (!Test.TestNotNull(TEXT("enemy gate"), Enemy.Gate)) return false;
			UVBGameEventSubsystem* Events = World->GetSubsystem<UVBGameEventSubsystem>();
			if (!Test.TestNotNull(TEXT("event subsystem"), Events)) return false;
			Events->OnGameEvent.AddLambda([this](const FVBGameEvent& Event)
			{
				if (Event.EventTag == VBGameplayTags::Event_Area_Entered && Event.TargetTags.HasTagExact(ConsumerTags().Event)) ++AreaEvents;
			});
			World->InitializeActorsForPlay(FURL());
			GS->HandleBeginPlay();
			Pawn->GetCapsuleComponent()->UpdateOverlaps();
			return Test.TestTrue(TEXT("world began play"), World->HasBegunPlay())
				&& Test.TestTrue(TEXT("expected possession state"), PC
					? PC->GetPawn() == Pawn && Pawn->GetController() == PC : Pawn->GetController() == nullptr)
				&& Test.TestTrue(TEXT("registered provider has real unavailable state"),
					UVBPlayerReadinessComponent::QueryForPawn(Pawn).State == EVBPlayerPreparationState::Unavailable);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConsumerAreaAdmissionTest, "Vowbound.Unit.Narrative.PreparationConsumer.AreaAdmission", ConsumerFlags)
bool FVBConsumerAreaAdmissionTest::RunTest(const FString& Parameters)
{
	FConsumerWorld F;
	if (!F.Initialize(*this)) return false;
	TestTrue(TEXT("physical inside is independent of preparation"), F.Area->IsPawnInside(F.Pawn));
	FVBEncounterConsumerTestAccess::Evaluate(*F.Area);
	TestFalse(TEXT("final admission rejects unavailable pawn"), FVBEncounterConsumerTestAccess::Admit(*F.Area, *F.Pawn));
	TestFalse(TEXT("unavailable did not consume edge"), FVBEncounterConsumerTestAccess::WasSatisfied(*F.Area, F.Pawn));
	TestFalse(TEXT("unavailable did not consume once fact"), F.Story->HasFact(ConsumerTags().Once));
	TestEqual(TEXT("unavailable emitted no event"), F.AreaEvents, 0);
	TestEqual(TEXT("unavailable applied no effect"), F.Story->GetCounter(ConsumerTags().Applied), 0);
	FVBEncounterConsumerTestAccess::Evaluate(*F.LegacyArea);
	TestTrue(TEXT("opt-out still consumes its once fact"), F.Story->HasFact(ConsumerTags().LegacyOnce));
	TestEqual(TEXT("opt-out emitted one event"), F.AreaEvents, 1);
	TestEqual(TEXT("opt-out applied one effect"), F.Story->GetCounter(ConsumerTags().Applied), 1);
	FVBEncounterConsumerTestAccess::Evaluate(*F.LegacyArea);
	TestFalse(TEXT("consumed legacy once cannot admit again"), FVBEncounterConsumerTestAccess::Admit(*F.LegacyArea, *F.Pawn));
	TestEqual(TEXT("legacy once event remains one"), F.AreaEvents, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConsumerPawnBindingsTest, "Vowbound.Unit.Narrative.PreparationConsumer.PawnBindings", ConsumerFlags)
bool FVBConsumerPawnBindingsTest::RunTest(const FString& Parameters)
{
	FConsumerWorld F;
	if (!F.Initialize(*this, false)) return false;
	FVBEncounterConsumerTestAccess::Evaluate(*F.Area);
	FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
	TestFalse(TEXT("no controller keeps gate asleep"), F.Enemy.Gate->IsAwake());
	F.PC = F.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("late player controller"), F.PC)) return false;
	F.PC->PlayerState = F.World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("late player state"), F.PC->PlayerState.Get())) return false;
	TestTrue(TEXT("controller discovery schedules area"), FVBEncounterConsumerTestAccess::Pending(*F.Area));
	TestTrue(TEXT("controller discovery schedules gate"), FVBEncounterConsumerTestAccess::Pending(*F.Enemy.Gate));
	F.PC->Possess(F.Pawn);
	FVBEncounterConsumerTestAccess::Evaluate(*F.Area);
	FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
	TestTrue(TEXT("late controller bound its current pawn"), FVBEncounterConsumerTestAccess::BoundPawn(*F.Area) == F.Pawn);
	UVBPlayerReadinessComponent* NewPreparation = nullptr;
	ACharacter* NewPawn = F.SpawnPawn(NewPreparation);
	if (!TestNotNull(TEXT("replacement pawn"), NewPawn)) return false;
	F.PC->Possess(NewPawn);
	FVBEncounterConsumerTestAccess::Evaluate(*F.Area);
	FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
	TestTrue(TEXT("area rebound to current pawn"), FVBEncounterConsumerTestAccess::BoundPawn(*F.Area) == NewPawn);
	TestTrue(TEXT("gate rebound to current pawn"), FVBEncounterConsumerTestAccess::BoundPawn(*F.Enemy.Gate) == NewPawn);
	TestFalse(TEXT("area unsubscribed live old provider"), F.Preparation->OnPreparationChanged.IsBoundToObject(F.Area));
	TestFalse(TEXT("gate unsubscribed live old provider"), F.Preparation->OnPreparationChanged.IsBoundToObject(F.Enemy.Gate));
	F.Preparation->BeginInitialization(FGuid::NewGuid(), EVBGameStartIntent::NewGame);
	TestFalse(TEXT("old provider cannot schedule area"), FVBEncounterConsumerTestAccess::Pending(*F.Area));
	TestFalse(TEXT("old provider cannot schedule gate"), FVBEncounterConsumerTestAccess::Pending(*F.Enemy.Gate));
	NewPreparation->BeginInitialization(FGuid::NewGuid(), EVBGameStartIntent::NewGame);
	TestTrue(TEXT("current provider schedules area requery"), FVBEncounterConsumerTestAccess::Pending(*F.Area));
	TestTrue(TEXT("current provider schedules gate requery"), FVBEncounterConsumerTestAccess::Pending(*F.Enemy.Gate));
	F.Area->Destroy();
	F.Enemy.Actor->Destroy();
	TestFalse(TEXT("area EndPlay unsubscribed"), NewPreparation->OnPreparationChanged.IsBoundToObject(F.Area));
	TestFalse(TEXT("gate EndPlay unsubscribed"), NewPreparation->OnPreparationChanged.IsBoundToObject(F.Enemy.Gate));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConsumerHealthAndKillZTest, "Vowbound.Unit.Narrative.PreparationConsumer.HealthAndKillZ", ConsumerFlags)
bool FVBConsumerHealthAndKillZTest::RunTest(const FString& Parameters)
{
	FConsumerWorld F;
	if (!F.Initialize(*this)) return false;
	F.Enemy.ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 75.0f);
	TestFalse(TEXT("unavailable nonfatal hit did not wake"), F.Enemy.Gate->IsAwake());
	TestFalse(TEXT("unavailable hit did not apply woken effects"), F.Story->HasFact(ConsumerTags().Woken));
	F.Enemy.ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 0.0f);
	TestTrue(TEXT("fatal hit preserves defeated fact"), F.Story->HasFact(ConsumerTags().Done));
	TestEqual(TEXT("fatal hit applies defeated once"), F.Story->GetCounter(ConsumerTags().DefeatedCount), 1);
	F.Enemy.Gate->SetHeld(true);
	F.Enemy.Gate->SetHeld(false);
	FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
	TestFalse(TEXT("defeated is not woken by hold/requery"), F.Enemy.Gate->IsAwake());
	FConsumerEnemy Legacy = F.SpawnEnemy(false);
	if (!TestNotNull(TEXT("legacy gate"), Legacy.Gate)) return false;
	Legacy.ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 75.0f);
	TestTrue(TEXT("legacy opt-out hit still wakes"), Legacy.Gate->IsAwake());
	FConsumerEnemy Falling = F.SpawnEnemy(true);
	if (!TestNotNull(TEXT("falling gate"), Falling.Gate)) return false;
	F.World->GetWorldSettings()->bEnableWorldBoundsChecks = true;
	Falling.Actor->SetActorLocation(FVector(0.0, 0.0, F.World->GetWorldSettings()->KillZ - 100.0));
	Falling.Actor->Destroy();
	TestEqual(TEXT("destroyed below KillZ preserves defeated effect"), F.Story->GetCounter(ConsumerTags().DefeatedCount), 2);
	Legacy.Actor->Destroy();
	TestEqual(TEXT("management destroy above KillZ is not defeated"), F.Story->GetCounter(ConsumerTags().DefeatedCount), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConsumerControllerReplacementTest, "Vowbound.Unit.Narrative.PreparationConsumer.ControllerReplacement", ConsumerFlags)
bool FVBConsumerControllerReplacementTest::RunTest(const FString& Parameters)
{
 FConsumerWorld F;
 if (!F.Initialize(*this)) return false;
 FVBEncounterConsumerTestAccess::Evaluate(*F.Area);
 FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
 APlayerController* OldPC = F.PC;
 APlayerController* NewPC = F.World->SpawnActor<APlayerController>();
 if (!TestNotNull(TEXT("replacement controller"), NewPC)) return false;
 NewPC->PlayerState = F.World->SpawnActor<APlayerState>();
 if (!TestNotNull(TEXT("replacement player state"), NewPC->PlayerState.Get())) return false;
 TestTrue(TEXT("new controller schedules area while old still exists"), FVBEncounterConsumerTestAccess::Pending(*F.Area));
 TestTrue(TEXT("new controller schedules gate while old still exists"), FVBEncounterConsumerTestAccess::Pending(*F.Enemy.Gate));
 FVBEncounterConsumerTestAccess::Evaluate(*F.Area);
 FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
 TestTrue(TEXT("first controller remains selected before removal"), FVBEncounterConsumerTestAccess::BoundPawn(*F.Area) == F.Pawn);
 OldPC->UnPossess();
 OldPC->Destroy();
 FVBEncounterConsumerTestAccess::Evaluate(*F.Area);
 FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
 TestTrue(TEXT("replacement without pawn binds no area pawn"), FVBEncounterConsumerTestAccess::BoundPawn(*F.Area) == nullptr);
 TestTrue(TEXT("replacement without pawn binds no gate pawn"), FVBEncounterConsumerTestAccess::BoundPawn(*F.Enemy.Gate) == nullptr);
 TestFalse(TEXT("missing pawn keeps gate asleep"), F.Enemy.Gate->IsAwake());
 UVBPlayerReadinessComponent* NewPreparation = nullptr;
 ACharacter* NewPawn = F.SpawnPawn(NewPreparation);
 if (!TestNotNull(TEXT("replacement controlled pawn"), NewPawn)) return false;
 NewPC->Possess(NewPawn);
 FVBEncounterConsumerTestAccess::Evaluate(*F.Area);
 FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
 TestTrue(TEXT("area follows new controller pawn"), FVBEncounterConsumerTestAccess::BoundPawn(*F.Area) == NewPawn);
 TestTrue(TEXT("gate follows new controller pawn"), FVBEncounterConsumerTestAccess::BoundPawn(*F.Enemy.Gate) == NewPawn);
 F.Preparation->BeginInitialization(FGuid::NewGuid(), EVBGameStartIntent::NewGame);
 TestFalse(TEXT("old provider no longer schedules area"), FVBEncounterConsumerTestAccess::Pending(*F.Area));
 TestFalse(TEXT("old provider no longer schedules gate"), FVBEncounterConsumerTestAccess::Pending(*F.Enemy.Gate));
 NewPreparation->BeginInitialization(FGuid::NewGuid(), EVBGameStartIntent::NewGame);
 TestTrue(TEXT("new provider schedules area"), FVBEncounterConsumerTestAccess::Pending(*F.Area));
 TestTrue(TEXT("new provider schedules gate"), FVBEncounterConsumerTestAccess::Pending(*F.Enemy.Gate));
 F.PC = NewPC;
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConsumerWakeAreaValidationTest, "Vowbound.Unit.Narrative.PreparationConsumer.WakeAreaValidation", ConsumerFlags)
bool FVBConsumerWakeAreaValidationTest::RunTest(const FString& Parameters)
{
 FConsumerWorld F;
 FConsumerWorld Other;
 if (!F.Initialize(*this) || !Other.Initialize(*this)) return false;
 TestTrue(TEXT("same world area is valid"), FVBEncounterConsumerTestAccess::ValidWakeArea(*F.Enemy.Gate));
 FVBEncounterConsumerTestAccess::SetWakeArea(*F.Enemy.Gate, nullptr);
 TestFalse(TEXT("null area is invalid"), FVBEncounterConsumerTestAccess::ValidWakeArea(*F.Enemy.Gate));
 AddExpectedError(TEXT("WakeArea가 없거나 다른 월드"), EAutomationExpectedErrorFlags::Contains, 1);
 FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
 TestTrue(TEXT("invalid area warned"), FVBEncounterConsumerTestAccess::WarnedWakeArea(*F.Enemy.Gate));
 TestFalse(TEXT("invalid area cannot wake"), F.Enemy.Gate->IsAwake());
 FVBEncounterConsumerTestAccess::SetWakeArea(*F.Enemy.Gate, Other.Area);
 TestFalse(TEXT("foreign world area is invalid"), FVBEncounterConsumerTestAccess::ValidWakeArea(*F.Enemy.Gate));
 FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
 TestFalse(TEXT("foreign area cannot wake"), F.Enemy.Gate->IsAwake());
 FVBEncounterConsumerTestAccess::SetWakeArea(*F.Enemy.Gate, F.Area);
 TestTrue(TEXT("restored same world area is valid again"), FVBEncounterConsumerTestAccess::ValidWakeArea(*F.Enemy.Gate));
 FVBEncounterConsumerTestAccess::Evaluate(*F.Enemy.Gate);
 TestFalse(TEXT("valid area does not bypass unavailable preparation"), F.Enemy.Gate->IsAwake());
 return true;
}

#endif
