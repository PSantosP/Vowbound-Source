// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBANS_WeaponTrail.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBSwingTrailComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBSwingTrailProfile.h"
#include "Engine/World.h"

UVBANS_WeaponTrail::UVBANS_WeaponTrail()
{
	Shape = VBGameplayTags::Melee_Shape_Weapon;
#if WITH_EDITORONLY_DATA
	// 애니 편집기 미리보기 액터에는 판정 컴포넌트가 없어 날을 못 푼다.
	bShouldFireInEditor = false;
#endif
}

void UVBANS_WeaponTrail::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
                                     const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	const UWorld* World = MeshComp ? MeshComp->GetWorld() : nullptr;
	if (!Owner || !World || !World->IsGameWorld())
	{
		return;
	}
	// 열쇠는 이 노티 객체다 - 시작과 끝이 같은 객체에서 온다. 이벤트 참조(EventReference.GetNotify())는 열쇠로 못 쓴다:
	//   엔진이 켜진 노티를 매 프레임 새 배열에 복사하고 참조를 그 복사본으로 다시 맞춰서(UAnimInstance::TriggerAnimNotifies 의 SetNotify)
	//   끝에서 받는 주소가 시작 때와 다르다. 그 주소로 짝을 찾던 첫 판은 궤적이 한 번도 안 닫혔다(2026-09-30 user PIE 「계속 붙어있네」).
	if (UVBSwingTrailComponent* Trail = UVBSwingTrailComponent::FindOrAdd(Owner))
	{
		Trail->BeginTrail(this, Shape, Profile, TotalDuration);
	}
}

void UVBANS_WeaponTrail::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                   const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (UVBSwingTrailComponent* Trail = Owner ? Owner->FindComponentByClass<UVBSwingTrailComponent>() : nullptr)
	{
		Trail->EndTrail(this);
	}
}

FString UVBANS_WeaponTrail::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Weapon Trail (%s)"), *Shape.ToString());
}
