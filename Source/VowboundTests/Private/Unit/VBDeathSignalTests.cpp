#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CharacterCommon/VBDeathSignalSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

// 사망 신호 서브시스템 자체의 계약. AI 컨트롤러가 이 신호로 BB 를 비우는 것(설계 7절 a~f)은 컨트롤러를 옮길 때
//   그쪽 시험이 본다. 클라이언트 가드는 넷 드라이버 없는 PIE 월드의 초기 넷모드로 본다 - 접속·복제의 증거는 아니다.
// 콜백이 캡처하는 값은 월드보다 먼저 선언한다. 월드 정리 중에 방송이 와도 내려간 스택을 만지지 않게.
namespace
{
	const UWorld::InitializationValues DeathSignalWorldInit()
	{
		return UWorld::InitializationValues().AllowAudioPlayback(false)
			.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	}

	UWorld* CreateDeathSignalTestWorld()
	{
		const UWorld::InitializationValues Initialization = DeathSignalWorldInit();
		return UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDeathSignalWorldScopeTest, "Vowbound.Unit.DeathSignal.WorldScope",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBDeathSignalWorldScopeTest::RunTest(const FString& Parameters)
{
	TArray<AActor*> HeardA, HeardB;
	UWorld* WorldA = CreateDeathSignalTestWorld();
	UWorld* WorldB = CreateDeathSignalTestWorld();
	ON_SCOPE_EXIT
	{
		if (WorldA) WorldA->DestroyWorld(false);
		if (WorldB) WorldB->DestroyWorld(false);
	};
	if (!TestNotNull(TEXT("world A"), WorldA) || !TestNotNull(TEXT("world B"), WorldB)) return false;

	UVBDeathSignalSubsystem* SignalsA = WorldA->GetSubsystem<UVBDeathSignalSubsystem>();
	UVBDeathSignalSubsystem* SignalsB = WorldB->GetSubsystem<UVBDeathSignalSubsystem>();
	if (!TestNotNull(TEXT("subsystem in world A"), SignalsA) || !TestNotNull(TEXT("subsystem in world B"), SignalsB)) return false;

	AActor* OwnerA = WorldA->SpawnActor<AActor>();
	AActor* OwnerB = WorldB->SpawnActor<AActor>();
	AActor* DeadInA = WorldA->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("actors"), OwnerA) || !OwnerB || !DeadInA) return false;

	const FDelegateHandle HandleA = SignalsA->AddListener(OwnerA, [&HeardA](AActor* Dead) { HeardA.Add(Dead); });
	SignalsB->AddListener(OwnerB, [&HeardB](AActor* Dead) { HeardB.Add(Dead); });

	UVBDeathSignalSubsystem::BroadcastActorDied(DeadInA);
	TestEqual(TEXT("same-world listener hears the death once"), HeardA.Num(), 1);
	TestTrue(TEXT("with the dead actor"), HeardA.Num() == 1 && HeardA[0] == DeadInA);
	TestEqual(TEXT("other world hears nothing"), HeardB.Num(), 0);

	UVBDeathSignalSubsystem::BroadcastActorDied(nullptr);
	TestEqual(TEXT("null broadcast is ignored"), HeardA.Num(), 1);

	SignalsA->RemoveListener(HandleA);
	SignalsA->RemoveListener(HandleA);
	UVBDeathSignalSubsystem::BroadcastActorDied(DeadInA);
	TestEqual(TEXT("removed listener (removed twice) hears nothing"), HeardA.Num(), 1);

	TestFalse(TEXT("listener without owner is refused"), SignalsA->AddListener(nullptr, [](AActor*) {}).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDeathSignalListenerLifetimeTest, "Vowbound.Unit.DeathSignal.ListenerLifetime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBDeathSignalListenerLifetimeTest::RunTest(const FString& Parameters)
{
	int32 GoneCalls = 0;
	int32 SelfCalls = 0;
	FDelegateHandle SelfHandle;
	int32 LateCalls = 0;
	bool bAdded = false;
	UWorld* World = CreateDeathSignalTestWorld();
	if (!TestNotNull(TEXT("test world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UVBDeathSignalSubsystem* Signals = World->GetSubsystem<UVBDeathSignalSubsystem>();
	AActor* Dead = World->SpawnActor<AActor>();
	AActor* GoneOwner = World->SpawnActor<AActor>();
	AActor* SelfRemover = World->SpawnActor<AActor>();
	AActor* Adder = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("subsystem"), Signals) || !Dead || !GoneOwner || !SelfRemover || !Adder) return false;

	// 소유 객체가 사라진 구독은 해제 없이도 불리지 않는다(EndPlay 가 안 오는 경로).
	Signals->AddListener(GoneOwner, [&GoneCalls](AActor*) { ++GoneCalls; });
	TestTrue(TEXT("owner actor destroyed"), World->DestroyActor(GoneOwner));

	// 방송 중 자기 해제. 해제 뒤로는 캡처를 더 쓰지 않는다(묶인 콜백이 그 자리에서 해제된다).
	SelfHandle = Signals->AddListener(SelfRemover, [&SelfCalls, &SelfHandle, Signals](AActor*)
	{
		++SelfCalls;
		Signals->RemoveListener(SelfHandle);
	});

	// 방송 중 추가 - 이번 방송에는 안 불리고 다음 방송에 불린다(지원 범위 밖으로 적은 차이를 드러낸다).
	Signals->AddListener(Adder, [&LateCalls, &bAdded, Signals, Adder](AActor*)
	{
		if (!bAdded)
		{
			bAdded = true;
			Signals->AddListener(Adder, [&LateCalls](AActor*) { ++LateCalls; });
		}
	});

	UVBDeathSignalSubsystem::BroadcastActorDied(Dead);
	TestEqual(TEXT("listener whose owner was destroyed is skipped"), GoneCalls, 0);
	TestEqual(TEXT("self-removing listener ran once"), SelfCalls, 1);
	TestEqual(TEXT("listener added during a broadcast is not called by it"), LateCalls, 0);

	UVBDeathSignalSubsystem::BroadcastActorDied(Dead);
	TestEqual(TEXT("self-removed listener stays removed"), SelfCalls, 1);
	TestEqual(TEXT("listener added during the first broadcast hears the second"), LateCalls, 1);
	return true;
}

#if WITH_EDITOR
// 클라이언트에서는 방송하지 않는다. 넷 드라이버가 없는 PIE 월드는 처음 지정한 넷모드를 그대로 돌려준다
//   (엔진 World.cpp::UWorld::InternalGetNetMode). 그래서 초기화 전에 NM_Client 를 지정한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDeathSignalClientSilentTest, "Vowbound.Unit.DeathSignal.ClientSilent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBDeathSignalClientSilentTest::RunTest(const FString& Parameters)
{
	int32 Calls = 0;
	const UWorld::InitializationValues Initialization = DeathSignalWorldInit();
	UWorld* World = UWorld::CreateWorld(EWorldType::PIE, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization, true);
	if (!TestNotNull(TEXT("client test world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	World->SetPlayInEditorInitialNetMode(NM_Client);
	World->InitWorld(Initialization);
	if (!TestEqual(TEXT("world reports client net mode"), static_cast<int32>(World->GetNetMode()), static_cast<int32>(NM_Client))) return false;

	UVBDeathSignalSubsystem* Signals = World->GetSubsystem<UVBDeathSignalSubsystem>();
	AActor* Owner = World->SpawnActor<AActor>();
	AActor* Dead = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("subsystem"), Signals) || !TestNotNull(TEXT("actors"), Owner) || !Dead) return false;

	Signals->AddListener(Owner, [&Calls](AActor*) { ++Calls; });
	UVBDeathSignalSubsystem::BroadcastActorDied(Dead);
	TestEqual(TEXT("client world does not broadcast"), Calls, 0);
	return true;
}
#endif

#endif
