// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchLibrary.h"   // FPoseSearchBlueprintResult
#include "UObject/Interface.h"
#include "VBInteractionAnimInterface.generated.h"

UINTERFACE(MinimalAPI, NotBlueprintable)
class UVBInteractionAnimInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 「이 AnimInstance 는 상호작용(두 몸 맞물림) 결과를 들 수 있다」.
 *
 * 왜 인터페이스인가: 상호작용에는 AnimInstance 가 둘 들어가는데 우리 둘은 형제 클래스다 -
 *   `UVBAnimInstance`(플레이어)와 `UVBEnemyAnimInstance`(적)가 각각 `UAnimInstance` 를 직접 상속한다.
 *   공통 부모를 새로 끼우는 것은 두 클래스의 기존 배선을 전부 건드리는 일이라 값이 안 맞고,
 *   필요한 접점은 «결과를 넣고 뺀다» 둘뿐이라 인터페이스가 정확한 크기다.
 *
 * 누가 채우나: 짝을 지은 쪽(`UVBFinisherComponent`)이 `MotionMatchMulti` 결과를 참가자마다 밀어 넣는다.
 *   각자가 스스로 만들지 않는다 - 검색은 한 번만 돌아야 하고 그 주인은 시작한 쪽이다.
 * 누가 읽나: `UVBAnimNotifyState_InteractionWarp` 가 워프 목표를 낼 때. 그것이 유일한 소비자다.
 * 언제 비나: 공격자 몽타주가 끝날 때 같은 컴포넌트가 빈 구조체를 밀어 넣는다.
 *   안 비우면 다음 상호작용이 낡은 결과로 워프 목표를 낸다.
 *
 * 구현 클래스는 저장 필드에 UPROPERTY 를 반드시 달아야 한다. `FPoseSearchBlueprintResult` 안에
 *   `TObjectPtr<UObject> SelectedAnim` 이 있어서 리플렉션에 안 올리면 GC 가 그 자산을 가져간다.
 *   증상이 「가끔 처형 정렬이 안 된다」로만 보일 자리라 여기 한 번 적어 둔다.
 */
class VOWBOUND_API IVBInteractionAnimInterface
{
	GENERATED_BODY()

public:
	/** 지금 배정된 상호작용. 없으면 기본 구조체(`bIsInteraction=false`)다. */
	virtual const FPoseSearchBlueprintResult& GetInteractionResult() const = 0;

	/** 결과를 채우거나(시작) 비운다(종료). 빈 구조체를 주면 「상호작용 없음」이다. */
	virtual void SetInteractionResult(const FPoseSearchBlueprintResult& InResult) = 0;
};
