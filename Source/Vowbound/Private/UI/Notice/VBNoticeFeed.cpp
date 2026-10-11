// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/Notice/VBNoticeFeed.h"

FVBNoticeRequest FVBNoticeRequest::FromText(EVBNoticeCategory InCategory, const FText& Text, FName InMergeKey)
{
	FVBNoticeRequest Request;
	Request.Category = InCategory;
	Request.MergeKey = InMergeKey;
	Request.MakeText = [Text](int32) { return Text; };
	return Request;
}

bool FVBNoticeFeed::Push(FVBNoticeRequest Request, double Now, double Lifetime, int32 MaxEntries)
{
	if (!Request.MakeText)
	{
		return false;
	}
	const int32 Added = FMath::Max(1, Request.Count);

	if (!Request.MergeKey.IsNone())
	{
		for (FVBNoticeEntry& Entry : Entries)
		{
			// 만료 시각이 지난 줄은 아직 안 걷혔어도 합치지 않는다 - 사라지는 줄에 수가 붙으면 안 보인 채 묻힌다.
			if (Entry.MergeKey == Request.MergeKey && Entry.ExpiresAt > Now)
			{
				const FText Merged = Request.MakeText(Entry.Count + Added);
				if (Merged.IsEmpty())
				{
					return false;
				}
				Entry.Count += Added;
				Entry.Text = Merged;
				Entry.Category = Request.Category;
				Entry.MakeText = MoveTemp(Request.MakeText);
				Entry.ExpiresAt = Now + Lifetime;
				return true;
			}
		}
	}

	FText Text = Request.MakeText(Added);
	if (Text.IsEmpty())
	{
		return false;
	}
	FVBNoticeEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.Category = Request.Category;
	Entry.MergeKey = Request.MergeKey;
	Entry.Count = Added;
	Entry.Text = MoveTemp(Text);
	Entry.ExpiresAt = Now + Lifetime;
	Entry.MakeText = MoveTemp(Request.MakeText);

	const int32 Cap = FMath::Max(1, MaxEntries);
	if (Entries.Num() > Cap)
	{
		Entries.RemoveAt(0, Entries.Num() - Cap);
	}
	return true;
}

bool FVBNoticeFeed::Prune(double Now)
{
	return Entries.RemoveAll([Now](const FVBNoticeEntry& Entry) { return Entry.ExpiresAt <= Now; }) > 0;
}
