#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "GameModes/VBGameMode.h"
#include "Narrative/Cinematic/VBCinematicStartupPolicy.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicStartupDefaultsTest, "Vowbound.Unit.Cinematic.Startup.DefaultOptOut",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCinematicStartupDefaultsTest::RunTest(const FString& Parameters)
{
	const AVBGameMode* Defaults = GetDefault<AVBGameMode>();
	const FBoolProperty* Enabled = FindFProperty<FBoolProperty>(AVBGameMode::StaticClass(), TEXT("bPlayInitialNewGameCinematic"));
	const FObjectPropertyBase* Definition = FindFProperty<FObjectPropertyBase>(AVBGameMode::StaticClass(), TEXT("InitialNewGameCinematic"));
	if (!TestNotNull(TEXT("Opt-in property"), Enabled) || !TestNotNull(TEXT("Definition property"), Definition)) return false;
	TestFalse(TEXT("Native game mode does not auto-enable a scene"), Enabled->GetPropertyValue_InContainer(Defaults));
	TestNull(TEXT("No native scene asset hardcoded"), Definition->GetObjectPropertyValue_InContainer(Defaults));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicStartupInvalidTest, "Vowbound.Unit.Cinematic.Startup.UninitializedRejected",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCinematicStartupInvalidTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("새 게임 시네마틱 정의 거절"), EAutomationExpectedErrorFlags::Contains, 2);
	UVBCinematicStartupPolicy* Policy = NewObject<UVBCinematicStartupPolicy>();
	TestFalse(TEXT("No world/controller cannot own bootstrap"), Policy->BeginNewGameCinematic(nullptr, nullptr, FGuid::NewGuid()));
	TestFalse(TEXT("Missing attempt is not inferred as new game"), Policy->BeginNewGameCinematic(nullptr, nullptr, {}));
	return true;
}
#endif
