// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "VBAN_SendAttackTraceEvent.generated.h"

class UVBSwingImpactProfile;
class UVBSwingCombatProfile;

/**
 * 공격 판정 타이밍 AnimNotify
 * 몽타주 특정 프레임에서 발화 -> GA에 GameplayEvent 전송
 * GA가 WaitGameplayEvent로 수신하여 PerformAttackTrace() 실행
 */
UCLASS(DisplayName = "Send Attack Trace Event")
class VOWBOUND_API UVBAN_SendAttackTraceEvent : public UAnimNotify
{
	GENERATED_BODY()
public:
	UVBAN_SendAttackTraceEvent();

	// 이 스윙의 임팩트 쉐이크 프로필(옵션). 비우면 공격 leaf 의 DefaultSwingImpactProfile 로 폴백한다.
	//
	// 왜 노티에 두는가: 저작 단위가 곧 스윙이다. 한 몽타주 안에 내려베기 -> 올려베기가 이어지면
	//  타격 노티도 둘이고 밀림 방향도 둘이어야 하는데, 공격 config 는 공격당 1개라 그 입도를 낼 수 없다.
	//  노티에 두면 저작 지점과 스윙이 1:1 이 된다(규칙 26 입도).
	// 왜 자산 참조인가: 쉐이크 클래스를 큐로 실어 보내야 하는데 FGameplayCueParameters 는
	//  UObject 참조만 싣는다. 값을 노티에 직접 저작하면 노티 N 개가 같은 값을 복제해 드리프트가 생긴다.
	// EditAnywhere 인 이유: 노티는 몽타주 트랙 위의 인스턴스라 EditDefaultsOnly 로는 저작할 수 없다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBSwingImpactProfile> SwingImpactProfile;

	// 이 스윙의 전투 수치 보정 프로필(옵션). 비우면 보정 없이 무기 leaf 의 값이 그대로 최종값이다.
	//
	// 위의 쉐이크 프로필과 저작 지점이 같고 이유도 같다(스윙 1:1). 자산을 나눈 이유는 읽는 쪽이 달라서다 -
	//  쉐이크는 큐를 타고 클라 연출로 가고, 이쪽은 GA 가 서버 권위 경로에서만 읽는다.
	//  합치면 "궤적은 같은데 무게만 다른 스윙"이 쉐이크 자산을 공유하지 못해 사본이 늘고, 사본의 축이
	//  원본과 갈라지면 밀림 방향이 조용히 바뀐다.
	// 담기는 것은 절대값이 아니라 배율이다 - 비우거나 1.0 을 넣으면 현행과 비트 단위로 같은 값이 나온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBSwingCombatProfile> SwingCombatProfile;

	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	
	// Notify 에디터 표시 이름
	virtual FString GetNotifyName_Implementation() const override;
};
