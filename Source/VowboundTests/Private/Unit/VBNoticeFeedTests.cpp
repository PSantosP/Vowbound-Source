#include "Misc/AutomationTest.h"
#include "UI/Notice/VBNoticeFeed.h"

// 알림 줄 목록의 단위 시험. 설계 .sw/remaining-features/a2-notices.md 4절.
// 시각은 인자로 굴린다 - 실시간을 기다리지 않는다.

#if WITH_AUTOMATION_TESTS
namespace
{
	FVBNoticeRequest ItemRequest(const FName Key, int32 Count)
	{
		FVBNoticeRequest Request;
		Request.Category = EVBNoticeCategory::Item;
		Request.MergeKey = Key;
		Request.Count = Count;
		Request.MakeText = [Key](int32 Total) { return FText::FromString(FString::Printf(TEXT("%s x%d"), *Key.ToString(), Total)); };
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBNoticeFeedMergeTest, "Vowbound.Unit.Notice.Feed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBNoticeFeedMergeTest::RunTest(const FString& Parameters)
{
	constexpr double Life = 3.0;
	constexpr int32 Max = 3;
	FVBNoticeFeed Feed;

	TestTrue(TEXT("first notice is added"), Feed.Push(ItemRequest(TEXT("Bandage"), 1), 0.0, Life, Max));
	TestTrue(TEXT("same key while visible merges"), Feed.Push(ItemRequest(TEXT("Bandage"), 2), 1.0, Life, Max));
	if (!TestEqual(TEXT("merge keeps one line"), Feed.GetEntries().Num(), 1))
	{
		return false;
	}
	const FVBNoticeEntry& Merged = Feed.GetEntries()[0];
	TestEqual(TEXT("merged count is summed"), Merged.Count, 3);
	TestEqual(TEXT("merged text is rebuilt from the total"), Merged.Text.ToString(), FString(TEXT("Bandage x3")));
	TestEqual(TEXT("merge restarts the timer"), Merged.ExpiresAt, 1.0 + Life);

	TestTrue(TEXT("different key is its own line"), Feed.Push(ItemRequest(TEXT("Gauze"), 1), 1.5, Life, Max));
	TestEqual(TEXT("two lines"), Feed.GetEntries().Num(), 2);

	TestFalse(TEXT("nothing expires early"), Feed.Prune(3.9));
	TestTrue(TEXT("lines expire at their own time"), Feed.Prune(4.0));
	TestEqual(TEXT("bandage (expires 4.0) is gone, gauze (4.5) stays"), Feed.GetEntries().Num(), 1);
	TestEqual(TEXT("remaining line is gauze"), Feed.GetEntries()[0].MergeKey, FName(TEXT("Gauze")));

	// 만료 시각이 지났지만 아직 안 걷힌 줄에는 합치지 않는다 - 사라지는 줄에 수가 붙어 안 보인 채 묻히면 안 된다.
	TestTrue(TEXT("expired-but-unpruned line is not merged into"), Feed.Push(ItemRequest(TEXT("Gauze"), 1), 4.6, Life, Max));
	TestEqual(TEXT("so a new gauze line appears"), Feed.GetEntries().Num(), 2);
	TestEqual(TEXT("new line starts its own count"), Feed.GetEntries().Last().Count, 1);

	// 상한: 넘으면 가장 오래된 줄이 빠진다.
	Feed.Push(FVBNoticeRequest::FromText(EVBNoticeCategory::Quest, FText::FromString(TEXT("A"))), 5.0, Life, Max);
	Feed.Push(FVBNoticeRequest::FromText(EVBNoticeCategory::Quest, FText::FromString(TEXT("B"))), 5.1, Life, Max);
	TestEqual(TEXT("cap holds the line count"), Feed.GetEntries().Num(), Max);
	TestEqual(TEXT("newest line is last"), Feed.GetEntries().Last().Text.ToString(), FString(TEXT("B")));
	TestEqual(TEXT("oldest line (the expired gauze, 4.5) was dropped - the new gauze is first"), Feed.GetEntries()[0].ExpiresAt, 4.6 + Life);

	// 키 없는 같은 글은 합치지 않는다(퀘스트 시작 두 개가 한 줄이 되면 안 된다).
	Feed.Push(FVBNoticeRequest::FromText(EVBNoticeCategory::Quest, FText::FromString(TEXT("B"))), 5.2, Life, Max);
	TestEqual(TEXT("no key means no merge"), Feed.GetEntries().Last().Count, 1);

	// 빈 글과 글 함수 없는 요청은 줄을 만들지 않는다.
	const int32 Before = Feed.GetEntries().Num();
	TestFalse(TEXT("empty text adds nothing"), Feed.Push(FVBNoticeRequest::FromText(EVBNoticeCategory::System, FText::GetEmpty()), 5.3, Life, Max));
	TestFalse(TEXT("missing text builder adds nothing"), Feed.Push(FVBNoticeRequest(), 5.3, Life, Max));
	TestEqual(TEXT("line count unchanged"), Feed.GetEntries().Num(), Before);
	return true;
}
#endif
