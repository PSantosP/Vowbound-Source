#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Narrative/Story/VBAreaTrigger.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagContainer.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// 실제 상자/캡슐 겹침을 관찰한다. Editor World와 제품 맵은 사용하지 않는다.
	struct FAreaOccupancyWorld
	{
		UWorld* World = nullptr;
		AVBAreaTrigger* Area = nullptr;
		ACharacter* Pawn = nullptr;
		UVBStoryStateComponent* Story = nullptr;

		~FAreaOccupancyWorld()
		{
			if (World)
			{
				// 이 fixture가 직접 만든 패키지만 종료 중 보호하고 dirty 표시를 정리한다.
				const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
				// EndPlay가 액터/컴포넌트 종료와 World의 시작 상태 해제를 담당한다.
				if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				OwnedPackage->SetDirtyFlag(false);
			}
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("engine for isolated world"), GEngine)) return false;
			const UWorld::InitializationValues Initialization = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::Num, &Initialization);
			if (!Test.TestNotNull(TEXT("isolated collision world"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);

			AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
			if (!Test.TestNotNull(TEXT("isolated game state"), GameState)) return false;
			World->SetGameState(GameState);
			Story = NewObject<UVBStoryStateComponent>(GameState);
			GameState->AddInstanceComponent(Story);
			Story->RegisterComponent();

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Area = World->SpawnActor<AVBAreaTrigger>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
			Pawn = World->SpawnActor<ACharacter>(FVector(1000.0, 0.0, 0.0), FRotator::ZeroRotator, SpawnParameters);
			if (!Test.TestNotNull(TEXT("area"), Area) || !Test.TestNotNull(TEXT("pawn"), Pawn)) return false;
			if (!Test.TestNotNull(TEXT("area collision box"), Area->FindComponentByClass<UBoxComponent>())) return false;

			UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Capsule->SetCollisionObjectType(ECC_Pawn);
			Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
			Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
			Capsule->SetGenerateOverlapEvents(true);
			Pawn->GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Pawn->GetMesh()->SetGenerateOverlapEvents(false);

			// 알림에는 actor 초기화뿐 아니라 World의 BeginPlay도 필요하다.
			World->InitializeActorsForPlay(FURL());
			GameState->HandleBeginPlay();
			Capsule->UpdateOverlaps();
			return Test.TestTrue(TEXT("fixture world has begun play"), World->HasBegunPlay())
				&& Test.TestTrue(TEXT("area has begun play"), Area->HasActorBegunPlay())
				&& Test.TestTrue(TEXT("pawn has begun play"), Pawn->HasActorBegunPlay());
		}

		void MovePawn(const FVector& Location) const
		{
			Pawn->SetActorLocation(Location);
			Pawn->GetCapsuleComponent()->UpdateOverlaps();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAreaOccupancyQueryTest, "Vowbound.Unit.Narrative.AreaOccupancy.PhysicalQuery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAreaOccupancyQueryTest::RunTest(const FString& Parameters)
{
	FAreaOccupancyWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	TestFalse(TEXT("null pawn is outside"), Fixture.Area->IsPawnInside(nullptr));
	TestFalse(TEXT("actual capsule starts outside"), Fixture.Area->IsPawnInside(Fixture.Pawn));

	// 같은 좌표의 다른 World 폰은 현재 영역의 점유가 아니다.
	const UWorld::InitializationValues OtherInitialization = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* OtherWorld = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &OtherInitialization);
	if (!TestNotNull(TEXT("other world"), OtherWorld)) return false;
	ON_SCOPE_EXIT
	{
		const TStrongObjectPtr<UPackage> OtherPackage(OtherWorld->GetPackage());
		OtherWorld->DestroyWorld(false);
		OtherPackage->SetDirtyFlag(false);
	};
	APawn* OtherPawn = OtherWorld->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("other-world pawn"), OtherPawn)) return false;
	TestFalse(TEXT("same-coordinate pawn from another world is outside"), Fixture.Area->IsPawnInside(OtherPawn));

	Fixture.MovePawn(FVector::ZeroVector);
	TestTrue(TEXT("actual capsule enters the box"), Fixture.Area->IsPawnInside(Fixture.Pawn));
	Fixture.MovePawn(FVector(1000.0, 0.0, 0.0));
	TestFalse(TEXT("actual capsule leaves the box"), Fixture.Area->IsPawnInside(Fixture.Pawn));
	Fixture.MovePawn(FVector::ZeroVector);
	TestTrue(TEXT("capsule reenters before destruction"), Fixture.Area->IsPawnInside(Fixture.Pawn));
	TestTrue(TEXT("pawn destruction succeeds"), Fixture.Pawn->Destroy());
	TestFalse(TEXT("destroyed pawn is outside"), Fixture.Area->IsPawnInside(Fixture.Pawn));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAreaOccupancySignalsTest, "Vowbound.Unit.Narrative.AreaOccupancy.SignalsAfterOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAreaOccupancySignalsTest::RunTest(const FString& Parameters)
{
	// UncookedOnly 시험 모듈은 native 태그를 등록하지 않는다. 기존 태그는 이 격리 World의 열쇠로만 사용한다.
	const FGameplayTag ConsumedFact = FGameplayTag::RequestGameplayTag(FName(TEXT("Story.Test.Talked")), false);
	if (!TestTrue(TEXT("registered fixture once key Story.Test.Talked"), ConsumedFact.IsValid())) return false;
	// 정리 중 알림이 와도 캡처가 살아 있어야 한다. handle은 World 정리 전에 제거한다.
	TArray<bool> ObservedInside;
	FAreaOccupancyWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	const FStructProperty* OnceProperty = FindFProperty<FStructProperty>(AVBAreaTrigger::StaticClass(), TEXT("OnceFact"));
	if (!TestNotNull(TEXT("authored OnceFact field"), OnceProperty)) return false;
	*OnceProperty->ContainerPtrToValuePtr<FGameplayTag>(Fixture.Area) = ConsumedFact;
	Fixture.Story->SetFact(ConsumedFact, true);
	if (!TestTrue(TEXT("once fact is already recorded"), Fixture.Story->HasFact(ConsumedFact))) return false;

	const FDelegateHandle Handle = Fixture.Area->OnOccupancyChanged.AddLambda([&Fixture, &ObservedInside]()
	{
		ObservedInside.Add(Fixture.Area->IsPawnInside(Fixture.Pawn));
	});
	ON_SCOPE_EXIT { Fixture.Area->OnOccupancyChanged.Remove(Handle); };
	TestFalse(TEXT("initial query after subscription is outside"), Fixture.Area->IsPawnInside(Fixture.Pawn));

	Fixture.MovePawn(FVector::ZeroVector);
	if (!TestTrue(TEXT("physical entry still signals after OnceFact"), !ObservedInside.IsEmpty())) return false;
	TestTrue(TEXT("entry callback reads current inside"), ObservedInside.Last());
	TestTrue(TEXT("recorded OnceFact does not suppress query"), Fixture.Area->IsPawnInside(Fixture.Pawn));
	const int32 EntrySignalCount = ObservedInside.Num();
	Fixture.MovePawn(FVector(1000.0, 0.0, 0.0));
	if (!TestTrue(TEXT("physical exit still signals after OnceFact"), ObservedInside.Num() > EntrySignalCount)) return false;
	TestFalse(TEXT("exit callback reads current outside"), ObservedInside.Last());
	TestTrue(TEXT("occupancy observations preserve the consumed fact"), Fixture.Story->HasFact(ConsumedFact));

	const int32 SignalCountBeforeStoryChange = ObservedInside.Num();
	Fixture.Story->SetFact(ConsumedFact, false);
	Fixture.Story->SetFact(ConsumedFact, true);
	TestEqual(TEXT("story-only changes do not report physical occupancy changes"), ObservedInside.Num(), SignalCountBeforeStoryChange);
	return true;
}

#endif
