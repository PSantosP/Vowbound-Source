// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBThreatTrackerSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void UVBThreatTrackerSubsystem::Register(AActor* Enemy, const FGameplayTag& Answer, const FLinearColor& Color)
{
	if (!Enemy)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (FVBThreatEntry& Entry : Threats)
	{
		if (Entry.Enemy.Get() == Enemy)
		{
			// 갱신. 같은 적이 예고를 다시 시작한 것이다(직전 Remove 를 놓쳤어도 행이 둘이 되지 않는다).
			Entry.Answer    = Answer;
			Entry.Color     = Color;
			Entry.StartTime = Now;
			return;
		}
	}

	FVBThreatEntry& NewEntry = Threats.AddDefaulted_GetRef();
	NewEntry.Enemy     = Enemy;
	NewEntry.Answer    = Answer;
	NewEntry.Color     = Color;
	NewEntry.StartTime = Now;
}

void UVBThreatTrackerSubsystem::Unregister(const AActor* Enemy)
{
	Threats.RemoveAll([Enemy](const FVBThreatEntry& Entry)
	{
		return !Entry.Enemy.IsValid() || Entry.Enemy.Get() == Enemy;
	});
}

const TArray<FVBThreatEntry>& UVBThreatTrackerSubsystem::GetThreats()
{
	// 죽은 적(약참조 무효)은 여기서 걷어낸다 - 큐 Remove 는 ASC 가 사는 동안만 온다.
	Threats.RemoveAll([](const FVBThreatEntry& Entry) { return !Entry.Enemy.IsValid(); });
	return Threats;
}

bool UVBThreatTrackerSubsystem::IsOnScreen(const APlayerController* PC, const FVector& WorldLocation, FVector2D* OutScreenPos)
{
	if (!PC)
	{
		return false;
	}

	// 엔진 투영은 카메라 뒤에서만 false 를 돌려준다. 앞이지만 화면 사각형 밖인 점은 true 로 오므로 사각형 검사를 더한다.
	FVector2D ScreenPos = FVector2D::ZeroVector;
	if (!PC->ProjectWorldLocationToScreen(WorldLocation, ScreenPos, /*bPlayerViewportRelative=*/false))
	{
		return false;
	}
	if (OutScreenPos)
	{
		*OutScreenPos = ScreenPos;
	}

	int32 SizeX = 0;
	int32 SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	return ScreenPos.X >= 0.0f && ScreenPos.Y >= 0.0f
		&& ScreenPos.X <= static_cast<float>(SizeX) && ScreenPos.Y <= static_cast<float>(SizeY);
}
