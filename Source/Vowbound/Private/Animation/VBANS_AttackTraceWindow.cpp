// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBANS_AttackTraceWindow.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBSwingImpactProfile.h" // OptionalObject(UObject) 로의 업캐스트 - 완전 타입 필요
#include "Data/VBSwingCombatProfile.h"

UVBANS_AttackTraceWindow::UVBANS_AttackTraceWindow()
{
	Shape = VBGameplayTags::Melee_Shape_Weapon;
#if WITH_EDITORONLY_DATA
	// 에디터 미리보기에서 GA 이벤트를 쏘지 않는다(옛 노티와 같다).
	bShouldFireInEditor = false;
#endif
}

void UVBANS_AttackTraceWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
                                           const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}

	FGameplayEventData EventData;
	EventData.EventTag = VBGameplayTags::Event_Montage_AttackTrace;
	EventData.Instigator = Owner;
	EventData.Target = Owner;
	// 창 표지 + 어느 날인지. 옛 한 프레임 노티는 이 태그가 없다 - 받는 GA 가 둘을 가르는 유일한 신호다.
	EventData.InstigatorTags.AddTag(VBGameplayTags::Melee_Window);
	if (Shape.IsValid())
	{
		EventData.InstigatorTags.AddTag(Shape);
	}
	EventData.EventMagnitude = RehitInterval;
	// null 도 그대로 보낸다 - 받는 쪽이 «노티에 없음 -> leaf 폴백» 으로 읽는다(옛 노티와 같은 규칙).
	EventData.OptionalObject = SwingImpactProfile;
	EventData.OptionalObject2 = SwingCombatProfile;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, VBGameplayTags::Event_Montage_AttackTrace, EventData);
}

void UVBANS_AttackTraceWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                         const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}
	// 몽타주가 끊겨도(블렌드아웃·중단) 엔진이 NotifyEnd 를 부른다 - 창이 열린 채 남지 않는다. GA 의 EndAbility 도 창을 닫는다(이중화).
	FGameplayEventData EventData;
	EventData.EventTag = VBGameplayTags::Event_Montage_AttackTraceEnd;
	EventData.Instigator = Owner;
	EventData.Target = Owner;
	// 몽타주 중단도 NotifyEnd 를 낸다. 자연 종료일 때만 마지막 구간을 검사하고, 회피·패링으로 끊기면 바로 닫는다.
	EventData.EventMagnitude = UAnimNotifyLibrary::NotifyStateReachedEnd(EventReference) ? 1.0f : 0.0f;
	// 어느 창을 닫을지 - 손·발 창이 겹칠 수 있다. 섹션 점프에서도 엔진이 옛 창의 끝을 새 창의 시작보다 먼저 낸다
	//   (UAnimInstance::TriggerAnimNotifies 가 끝 알림을 시작 알림 앞에서 돈다) - 같은 모양이어도 새 창을 닫지 않는다.
	if (Shape.IsValid())
	{
		EventData.InstigatorTags.AddTag(Shape);
	}
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, VBGameplayTags::Event_Montage_AttackTraceEnd, EventData);
}

FString UVBANS_AttackTraceWindow::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Attack Window (%s)"), *Shape.ToString());
}
