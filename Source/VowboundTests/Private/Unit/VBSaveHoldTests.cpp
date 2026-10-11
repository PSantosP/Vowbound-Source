#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SaveSystem/VBSaveHold.h"

namespace
{
	// 이름은 파일마다 달라야 한다 - 익명 네임스페이스도 unity 묶음에서는 한 번역 단위라 같은 이름이 부딪힌다.
	constexpr EAutomationTestFlags SaveHoldTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveHoldAdmissionTest, "Vowbound.Unit.Save.Holds.Admission", SaveHoldTestFlags)
bool FVBSaveHoldAdmissionTest::RunTest(const FString& Parameters)
{
	FVBSaveHoldSet Holds;
	TestFalse(TEXT("uninitialized generation is invalid"), Holds.GetGeneration().IsValid());
	TestFalse(TEXT("uninitialized set rejects acquire"), Holds.Acquire(TEXT("C02")).IsValid());
	TestEqual(TEXT("rejected acquire leaves no hold"), Holds.Num(), 0);
	TestFalse(TEXT("invalid generation is not admitted"), Holds.AdvanceGeneration({}));
	const FGuid Generation(1, 0, 0, 1);
	TestTrue(TEXT("first valid generation is admitted"), Holds.AdvanceGeneration(Generation));
	TestFalse(TEXT("empty reason is rejected"), Holds.Acquire(NAME_None).IsValid());
	const FVBSaveHoldToken Token = Holds.Acquire(TEXT("C02"));
	TestTrue(TEXT("accepted token is valid"), Token.IsValid());
	TestTrue(TEXT("token carries the current generation"), Token.Generation == Generation);
	TestTrue(TEXT("accepted token belongs to the set"), Holds.Contains(Token));
	TestEqual(TEXT("one accepted hold"), Holds.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveHoldNestedTest, "Vowbound.Unit.Save.Holds.NestedRelease", SaveHoldTestFlags)
bool FVBSaveHoldNestedTest::RunTest(const FString& Parameters)
{
	FVBSaveHoldSet Holds;
	Holds.AdvanceGeneration(FGuid(2, 0, 0, 1));
	const FVBSaveHoldToken A = Holds.Acquire(TEXT("C02"));
	const FVBSaveHoldToken B = Holds.Acquire(TEXT("OtherTransition"));
	if (!TestTrue(TEXT("both nested holds are admitted"), A.IsValid() && B.IsValid())) return false;
	TestTrue(TEXT("nested holds have distinct IDs"), A.HoldId != B.HoldId);
	TestTrue(TEXT("releasing A succeeds"), Holds.Release(A));
	TestTrue(TEXT("B still blocks saving"), Holds.IsHeld() && Holds.Contains(B));
	TestEqual(TEXT("releasing A does not release B"), Holds.Num(), 1);
	TestFalse(TEXT("duplicate A release is idempotent"), Holds.Release(A));
	TestFalse(TEXT("invalid token cannot release B"), Holds.Release({}));
	TestTrue(TEXT("B release succeeds"), Holds.Release(B));
	TestFalse(TEXT("last explicit release opens the set"), Holds.IsHeld());
	TestFalse(TEXT("duplicate final release has no effect"), Holds.Release(B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveHoldGenerationTest, "Vowbound.Unit.Save.Holds.Generation", SaveHoldTestFlags)
bool FVBSaveHoldGenerationTest::RunTest(const FString& Parameters)
{
	FVBSaveHoldSet Holds;
	const FGuid First(3, 0, 0, 1);
	const FGuid Second(3, 0, 0, 2);
	Holds.AdvanceGeneration(First);
	const FVBSaveHoldToken Old = Holds.Acquire(TEXT("PendingOutcome"));
	if (!TestTrue(TEXT("old hold is admitted"), Old.IsValid())) return false;
	TestFalse(TEXT("duplicate initialization is not a generation change"), Holds.AdvanceGeneration(First));
	TestTrue(TEXT("duplicate initialization preserves the hold"), Holds.Contains(Old));
	TestFalse(TEXT("invalid generation cannot discard a live hold"), Holds.AdvanceGeneration({}));
	TestTrue(TEXT("invalid generation preserves the old identity"), Holds.GetGeneration() == First && Holds.Contains(Old));
	TestTrue(TEXT("new explicit generation replaces the old game"), Holds.AdvanceGeneration(Second));
	TestFalse(TEXT("new generation discards old holds"), Holds.IsHeld());
	const FVBSaveHoldToken Current = Holds.Acquire(TEXT("CurrentTransition"));
	if (!TestTrue(TEXT("current hold is admitted"), Current.IsValid())) return false;
	TestFalse(TEXT("old token cannot release the current hold"), Holds.Release(Old));
	TestTrue(TEXT("current hold survives a stale release"), Holds.Contains(Current));
	FVBSaveHoldToken StaleGeneration = Current;
	StaleGeneration.Generation = First;
	TestFalse(TEXT("matching ID with the wrong generation is rejected"), Holds.Release(StaleGeneration));
	TestEqual(TEXT("stale releases preserve the current count"), Holds.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveHoldUnknownTokenTest, "Vowbound.Unit.Save.Holds.UnknownToken", SaveHoldTestFlags)
bool FVBSaveHoldUnknownTokenTest::RunTest(const FString& Parameters)
{
	FVBSaveHoldSet Holds;
	FVBSaveHoldSet Other;
	const FGuid Generation(4, 0, 0, 1);
	Holds.AdvanceGeneration(Generation);
	Other.AdvanceGeneration(Generation);
	const FVBSaveHoldToken Current = Holds.Acquire(TEXT("Current"));
	const FVBSaveHoldToken Foreign = Other.Acquire(TEXT("Foreign"));
	if (!TestTrue(TEXT("both sets issue valid tokens"), Current.IsValid() && Foreign.IsValid())) return false;
	TestFalse(TEXT("foreign token is not in the current set"), Holds.Contains(Foreign));
	TestFalse(TEXT("foreign token cannot release the current hold"), Holds.Release(Foreign));
	TestTrue(TEXT("current token remains held"), Holds.Contains(Current));
	TestTrue(TEXT("foreign token still belongs to its issuing set"), Other.Contains(Foreign));
	TestEqual(TEXT("failed release preserves the current count"), Holds.Num(), 1);
	return true;
}

#endif
