// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Vowbound/Vowbound.h" // VB_LOG - BP ActivateAbility 본문이 있으면 소리내어 밝힌다

UVBGameplayAbility::UVBGameplayAbility()
{
	// 네트워크: 서버가 활성화 시작 + 소유자 클라에 GA 인스턴스 복제 (2026-04-20 전환).
	// 이유:
	//   - ServerOnly는 소유자 클라에 인스턴스가 없어 몽타주 로컬 재생 불가 (Bug B).
	//   - ServerInitiated: 클라도 GA 인스턴스로 ExecuteAbility 실행 → PlayMontageAndWait Task가 로컬에서 재생.
	//   - 서버 전용 부작용(GE 적용, Spawn, CMC 변경)은 각 파생 GA에서 `ActorInfo->IsNetAuthority()` 가드로 보호.
	//   - 참조: Engine/Plugins/Runtime/GameplayAbilities/.../AbilitySystemComponent_Abilities.cpp::UAbilitySystemComponent::InternalTryActivateAbility
	//            (bIsLocal=false && !ServerOnly인 경우에만 ClientActivateAbilitySucceed 송출 → 클라 CallActivateAbility)
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	// Instancing: 액터당 1개 인스턴스 (ServerInitiated 복제의 필수 조건)
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

UVBAbilitySystemComponent* UVBGameplayAbility::GetVBAbilitySystemComponent() const
{
	return Cast<UVBAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
}

ACharacter* UVBGameplayAbility::GetAvatarCharacter() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
}

void UVBGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	
	// OnSpawn 정책: 부여 즉시 활성화 (패시브용)
	if (ActivationPolicy == EVBAbilityActivationPolicy::OnSpawn)
	{
		if (ActorInfo && !Spec.IsActive())
		{
			ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle);
		}
	}
}

void UVBGameplayAbility::PlayHitStop(float RealDuration, float TimeDilation)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}
	
	FGameplayCueParameters CueParams;
	CueParams.RawMagnitude = RealDuration;
	CueParams.NormalizedMagnitude = TimeDilation;
	ASC->ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_HitStop, CueParams);
}

void UVBGameplayAbility::PlayHitStop(float RealDuration, float TimeDilation, const TArray<AActor*>& Victims, const UObject* AttackConfig)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}
	FGameplayCueParameters CueParams;
	CueParams.RawMagnitude        = RealDuration;
	CueParams.NormalizedMagnitude = TimeDilation;
	CueParams.SourceObject        = AttackConfig;
	CueParams.EffectContext       = ASC->MakeEffectContext();
	CueParams.EffectContext.AddActors(TArray<TWeakObjectPtr<AActor>>(Victims));
	ASC->ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_HitStop, CueParams);
}

void UVBGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Super 를 부르지 않는다. 이 클래스가 활성화 순서의 주인이다.
	//
	// 왜 (2026-09-23, FIND-087 종결): 엔진의 UGameplayAbility::ActivateAbility 는
	//   네이티브 어빌리티에게 아무 일도 하지 않는다 - else 분기가 비어 있고
	//   「네이티브 자식은 이 함수를 재정의하고 CommitAbility 를 불러라」는 주석만 있다.
	//   그 함수가 실제로 하는 일은 하나뿐이다: bHasBlueprintActivate 면 BP 본문(K2_ActivateAbility)을
	//   돌리는 것. 그런데 이 프로젝트의 규칙은 「GA 는 ExecuteAbility 를 재정의한다,
	//   ActivateAbility 가 아니라」이므로 그 경로는 애초에 금지돼 있다.
	//   즉 Super 를 부르는 것은 «금지된 본문을 자원 소비보다 먼저 실행할 구멍» 만 열어 둔 셈이었다.
	//   가설이 아니다 - BP_GA_HitReact·BP_GA_LightAttack 그래프에 그 이벤트 노드가 이미 놓여 있고
	//   지금은 then 연결이 0이라 비어 있을 뿐이다. 누가 선 하나를 이으면 그날 터진다.
	//
	// 지운 대신 소리를 낸다. BP 본문이 있는데 Super 를 안 부르면 그 본문은 조용히 안 돈다 -
	//   구멍을 막으면서 다른 구멍을 내는 셈이라, 막을 수 없는 것(BP 저작)은 말로 막는다.
	if (bHasBlueprintActivate || bHasBlueprintActivateFromEvent)
	{
		// VB_LOG 는 매크로 안에서 TEXT() 를 씌우므로 여기서는 맨 리터럴을 넘긴다.
		//   여러 줄로 이어 붙이면 TEXT("a" "b") 가 되어 넓은 문자열과 좁은 문자열이 섞여 깨진다 - 한 줄로 둔다.
		VB_LOG(Error, "%s 의 블루프린트에 ActivateAbility 이벤트 본문이 있다. 이 프로젝트는 그 경로를 안 쓰므로 본문이 실행되지 않는다. 로직을 ExecuteAbility(C++) 로 옮기거나 그래프에서 그 이벤트 노드를 지워라.", *GetName());
	}

	// 1단계: CommitAbility (모든 GA에 강제). 자원 판정이 무엇보다 먼저다.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 2단계: 자식의 실제 로직
	ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}






