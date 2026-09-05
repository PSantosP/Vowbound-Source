// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Animation/VBAN_SendAttackTraceEvent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBSwingImpactProfile.h" // OptionalObject(UObject) 로의 업캐스트 — 완전 타입 필요
#include "Data/VBSwingCombatProfile.h" // OptionalObject2 로의 업캐스트 — 같은 이유
#include "Vowbound/Vowbound.h"

UVBAN_SendAttackTraceEvent::UVBAN_SendAttackTraceEvent()
{
	// 정확한 프레임 타이밍 보장하도록
#if WITH_EDITORONLY_DATA
	bShouldFireInEditor = false;
#endif
}

void UVBAN_SendAttackTraceEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	
	if (!MeshComp)
	{
		return;
	}
	
	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor)
	{
		return;
	}
	
	// Owner의 ASC를 통해 GameplayEvent 전송
	UAbilitySystemComponent* ASC = 
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OwnerActor);
	
	if (!ASC)
	{
		return;
	}
	
	// GameplayEvent 데이터 구성
	FGameplayEventData EventData;
	EventData.EventTag = VBGameplayTags::Event_Montage_AttackTrace;
	EventData.Instigator = OwnerActor;
	EventData.Target = OwnerActor; // GA에서 실제 타겟 탐색
	// 이 스윙의 임팩트 쉐이크 프로필을 페이로드에 실어 보낸다. OptionalObject 는 지금까지 미사용이었고,
	//  GA 의 OnAttackTraceEvent 도 페이로드를 버리고 있었다 — 저작 지점이 없던 이유가 정확히 이 두 곳이다.
	// null 이어도 그대로 보낸다. 수신측이 "노티에 없음 -> leaf 폴백"으로 읽는 신호가 곧 이 null 이다.
	// 복제는 불필요하다: 이 이벤트를 기다리는 WaitGameplayEvent 태스크는 서버에서만 만들어지고
	//  서버도 몽타주를 재생하므로 노티는 서버에서 발화한다.
	EventData.OptionalObject = SwingImpactProfile;
	// 전투 수치 보정은 두 번째 슬롯으로 간다. 엔진이 UObject 슬롯을 둘 주므로 두 관심사를 한 자산으로
	//  묶거나 컨테이너를 새로 만들 이유가 없다. 여기도 null 을 그대로 보낸다 - 수신측이 그 null 을
	//  "보정 없음"으로 읽고, 그 상태가 곧 현행 동작이다.
	EventData.OptionalObject2 = SwingCombatProfile;

	// 이벤트 전송 -> WaitGameplayEvent가 수신
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		OwnerActor,
		VBGameplayTags::Event_Montage_AttackTrace,
		EventData);
	
	VB_LOG(Log, "AN_SendAttackTraceEvent: 공격 Trace 이벤트 전송 (%s)", *OwnerActor->GetName());
}

FString UVBAN_SendAttackTraceEvent::GetNotifyName_Implementation() const
{
	return TEXT("Attack Trace");
}


