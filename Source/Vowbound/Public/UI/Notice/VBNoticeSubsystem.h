// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UI/Notice/VBNoticeFeed.h"
#include "VBNoticeSubsystem.generated.h"

/**
 * 화면 알림 줄의 주인. 로컬 플레이어마다 하나(설계 .sw/remaining-features/a2-notices.md).
 * 알림을 띄우려는 쪽(HUD 의 구독들, 앞으로 도감·제작·튜토리얼)은 Push 만 부른다. 그리는 것은 UVBNoticeFeedWidget 이다.
 * 왜 HUD 가 아니라 여기인가: HUD 는 Game 층이라 메뉴가 뜨면 그 아래에 깔리고, 생산자가 늘면 HUD 가 모든 생산자를 알아야 한다.
 * 시각은 실시간(FPlatformTime)이다 - 일시정지 메뉴에서 저장해도 «저장됨» 이 사라진다.
 */
UCLASS()
class VOWBOUND_API UVBNoticeSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	// 위젯·PC·폰·로컬 플레이어 무엇이든. 로컬 플레이어 찾기는 UVBUIManagerSubsystem::Get 과 같은 길이다.
	static UVBNoticeSubsystem* Get(const UObject* WorldContextObject);

	void Push(FVBNoticeRequest Request);
	void PushText(EVBNoticeCategory Category, const FText& Text, FName MergeKey = NAME_None);

	// 시간이 다 된 줄을 걷어 낸 뒤의 목록. 화면이 매 틱 부른다 - 걷힌 줄이 있으면 OnNoticesChanged 가 먼저 울린다.
	const TArray<FVBNoticeEntry>& GetEntries();

	double GetNow() const;

	// 줄이 생기거나 합쳐지거나 걷혔다.
	FSimpleMulticastDelegate OnNoticesChanged;

private:
	FVBNoticeFeed Feed;
};
