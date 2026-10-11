// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBGameEventSubsystem.generated.h"

class APlayerState;

/** «무슨 일이 있었다» 하나. 설계 .sw/narrative 2.3절. */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBGameEvent
{
	GENERATED_BODY()

	// 무슨 일(예: Event.Enemy.Killed, Event.Interaction.Completed, Event.Dialogue.NodeReached).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Story")
	FGameplayTag EventTag;

	// 무엇에 대해(예: 죽은 적의 종류 태그, 연 문의 태그). 퀘스트 목표는 이 태그들에 질의를 건다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Story")
	FGameplayTagContainer TargetTags;

	// 일으킨 플레이어. 없으면 세계가 일으킨 일(시간 경과 등).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Story")
	TObjectPtr<APlayerState> Instigator = nullptr;

	// 수량(주운 개수 등). 기본 1.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Story")
	int32 Count = 1;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnGameEvent, const FVBGameEvent&);

/**
 * 게임 사건 방송. 설계 .sw/narrative 2.3절.
 *
 * 왜 있나: 퀘스트가 「적을 죽였다」「문을 열었다」를 알려고 전투·상호작용 코드 안에 퀘스트 호출을 박으면, 새 목표 종류마다 남의 코드를 고친다.
 *   생산자는 여기 한 줄 방송하고, 듣는 쪽(퀘스트 일지 등)은 누가 보냈는지 모른 채 태그로 거른다.
 * 서버에서만 방송한다 - 듣는 쪽이 전부 서버 권위 상태(퀘스트 진행)를 바꾸고, 그 결과는 복제로 클라에 간다.
 *   클라에서 부르면 버리고 경고한다(같은 사건이 서버·클라에서 두 번 세지는 것을 막는다).
 */
UCLASS()
class VOWBOUND_API UVBGameEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVBGameEventSubsystem* Get(const UObject* WorldContextObject);

	void Broadcast(const FVBGameEvent& Event);

	FVBOnGameEvent OnGameEvent;
};
