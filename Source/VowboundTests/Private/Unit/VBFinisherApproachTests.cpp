#include "Misc/AutomationTest.h"
#include "Character/VBFinisherApproachPolicy.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherApproachTest, "Vowbound.Unit.Finisher.ApproachSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBFinisherApproachTest::RunTest(const FString& Parameters)
{
	const FVector Origin = FVector::ZeroVector;
	const FVector Forward = FVector::ForwardVector;
	TestTrue(TEXT("Front location admits front clip"), VBFinisherApproachPolicy::Matches(EVBFinisherApproach::Front, FVector(100, 0, 0), Origin, Forward));
	TestFalse(TEXT("Front location rejects back clip"), VBFinisherApproachPolicy::Matches(EVBFinisherApproach::Back, FVector(100, 0, 0), Origin, Forward));
	TestTrue(TEXT("Back location admits back clip"), VBFinisherApproachPolicy::Matches(EVBFinisherApproach::Back, FVector(-100, 0, 0), Origin, Forward));
	TestFalse(TEXT("Back location rejects front clip"), VBFinisherApproachPolicy::Matches(EVBFinisherApproach::Front, FVector(-100, 0, 0), Origin, Forward));
	TestTrue(TEXT("Rotated translated pair uses victim facing"), VBFinisherApproachPolicy::Matches(EVBFinisherApproach::Front, FVector(50, 100, 20), FVector(50, 0, 0), FVector::RightVector));
	TestTrue(TEXT("Side boundary belongs to front"), VBFinisherApproachPolicy::Matches(EVBFinisherApproach::Front, FVector(0, 100, 0), Origin, Forward));
	TestFalse(TEXT("Side boundary cannot admit both"), VBFinisherApproachPolicy::Matches(EVBFinisherApproach::Back, FVector(0, 100, 0), Origin, Forward));
	TestFalse(TEXT("Coincident horizontal roots reject"), VBFinisherApproachPolicy::Matches(EVBFinisherApproach::Front, FVector(0, 0, 100), Origin, Forward));
	TestFalse(TEXT("Missing victim facing rejects"), VBFinisherApproachPolicy::Matches(EVBFinisherApproach::Back, Forward, Origin, Origin));
	TestFalse(TEXT("Nonfinite position rejects"), VBFinisherApproachPolicy::Matches(EVBFinisherApproach::Front, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), Origin, Forward));
	for (const TCHAR* Weapon : {TEXT("Katana"), TEXT("BigSword")})
	{
		for (int32 Index = 1; Index <= 4; ++Index)
		{
			const FString Name = FString::Printf(TEXT("DA_Finisher_%s_%02d"), Weapon, Index);
			const FString Path = FString::Printf(TEXT("/Game/Vowbound/Data/Finisher/%s.%s"), *Name, *Name);
			const UVBFinisherConfig* Config = LoadObject<UVBFinisherConfig>(nullptr, *Path);
			if (!TestNotNull(*Name, Config)) continue;
			TestNotNull(TEXT("Interaction database exists"), Config->InteractionDatabase.Get());
			TestEqual(TEXT("Authored side admitted before pose search"),
				VBFinisherApproachPolicy::Matches(Config->Approach, FVector(100, 0, 0), Origin, Forward), Index >= 3);
		}
	}
	return true;
}
#endif
