// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/Notice/VBNoticeSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "HAL/PlatformTime.h"
#include "UI/VBUIManagerSubsystem.h"
#include "UI/VBUISettings.h"

UVBNoticeSubsystem* UVBNoticeSubsystem::Get(const UObject* WorldContextObject)
{
	// 로컬 플레이어 찾기를 다시 짜지 않는다 - UI 관리자는 로컬 플레이어마다 늘 있다.
	const UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(WorldContextObject);
	const ULocalPlayer* LocalPlayer = UI ? UI->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UVBNoticeSubsystem>() : nullptr;
}

void UVBNoticeSubsystem::Push(FVBNoticeRequest Request)
{
	const UVBUISettings* Settings = GetDefault<UVBUISettings>();
	if (Feed.Push(MoveTemp(Request), GetNow(), Settings->NoticeSeconds, Settings->MaxNotices))
	{
		OnNoticesChanged.Broadcast();
	}
}

void UVBNoticeSubsystem::PushText(EVBNoticeCategory Category, const FText& Text, FName MergeKey)
{
	Push(FVBNoticeRequest::FromText(Category, Text, MergeKey));
}

const TArray<FVBNoticeEntry>& UVBNoticeSubsystem::GetEntries()
{
	if (Feed.Prune(GetNow()))
	{
		OnNoticesChanged.Broadcast();
	}
	return Feed.GetEntries();
}

double UVBNoticeSubsystem::GetNow() const
{
	return FPlatformTime::Seconds();
}
