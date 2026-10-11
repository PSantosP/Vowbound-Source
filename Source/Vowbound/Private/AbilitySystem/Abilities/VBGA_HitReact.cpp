// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_HitReact.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBHitImpact.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBHitReactConfig.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h" // ProcessRootMotionPostConvertToWorld - 밀림 방향 보정
#include "Vowbound/Vowbound.h"

namespace
{
	// 피격자의 현재 스탠스. 히트리액트는 피격자 소유 데이터이므로 "맞은 쪽"의 무기 타입을 읽는다.
	// 적(AVBEnemyBase)은 UVBWeaponStateComponent 를 갖지 않는다(플레이어 전용) → None → DefaultSet 폴백.
	EVBWeaponType ResolveVictimStance(const AActor* Victim)
	{
		if (const AVBCharacter* VBChar = Cast<AVBCharacter>(Victim))
		{
			if (const UVBWeaponStateComponent* WSC = VBChar->GetWeaponStateComponent())
			{
				return WSC->GetCurrentWeaponType();
			}
		}
		return EVBWeaponType::None;
	}
}

UVBGA_HitReact::UVBGA_HitReact()
{
	// 입력 GA 가 아니다 → InputTag 없음. ActivationPolicy 는 베이스 기본값 OnInputTriggered 를 그대로 둔다.
	// (OnSpawn 으로 두면 VBGameplayAbility::OnGiveAbility 가 부여 즉시 활성화해버린다 — 트리거 기반과 충돌)
	// NetExecutionPolicy(ServerInitiated) / InstancingPolicy(InstancedPerActor) 는 베이스 생성자 상속.

	// 프로젝트 최초의 AbilityTriggers 사용.
	// 배선은 ASC 가 자동 수행한다: UAbilitySystemComponent::OnGiveAbility 가 AbilityTriggers 를 순회해
	// GameplayEventTriggeredAbilities 맵에 등록 → GiveAbility 만 하면 끝. 별도 구독 코드 불필요.
	// TriggerSource 는 FAbilityTriggerData 기본 생성자가 이미 GameplayEvent 로 설정하므로 명시 대입 생략.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = VBGameplayTags::Event_Combat_Hit;   // 선언만 있고 사용처가 없던 예약 태그를 여기서 살린다
	AbilityTriggers.Add(Trigger);

	// 슈퍼아머/사망 게이트 = GAS 네이티브("이 어빌리티는 활성화 액터가 이 태그 중 하나라도 가지면 차단됨").
	// 왜 로스태그 비복제(VBGA_MeleeAttackBase.cpp AddLooseGameplayTag)가 문제되지 않는가:
	//   트리거 활성화는 서버 권위에서만 시도된다(HasNetworkAuthorityToActivateTriggeredAbility →
	//   ServerInitiated 는 bIsAuthority 반환) → 판정이 읽는 ASC 는 항상 서버 ASC 이고, 서버엔 슈퍼아머 태그가 확실히 있다.
	//   → 큐 시절 sim proxy 에서 게이트가 반대로 평가되던 발산이 자동 소멸한다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_SuperArmor);

	// 처형을 «거는 중» 이면 반응하지 않는다. 이 GA 는 Ability.* 자산 태그가 없어 상대의
	//   BlockAbilitiesWithTag 로는 못 걸린다 - 그래서 반응 쪽이 자기를 막는다(바로 위 줄과 같은 수단).
	// 왜: 반응 몽타주가 처형 몽타주와 같은 슬롯 그룹이라 끼어들면 처형 애니가 그 자리에서 끊기고,
	//   플러그인은 그것을 「장면 이탈」로 읽어 장면을 통째로 끝낸다
	//   (2026-09-13 실측: 제자리 회전 애니가 같은 방식으로 3.07초 장면을 0.012초에 끝냈다).
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Finishing);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);

	// 엎드린(그로기) 동안은 서 있는 플린치를 틀지 않는다 - 그 구간의 피격 반응은 UVBGA_Knockdown 이 같은 Event.Combat.Hit 을 받아
	//   엎드린 자세의 방향별 클립으로 낸다(design .sw/boss/design.md §14.4). 둘이 같이 깨어나면 서 있는 플린치가 엎드린 몸을 덮고
	//   HitReacting 상승 에지가 반격을 끊는다. 지금 보스는 이 GA 를 안 가져 무해하지만, 둘을 함께 받는 아키타입이 생기는 날 이 줄이 막는다(2026-09-28 /verify).
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Downed);

	// 경직(기절 게이지 소진) 중에도 플린치를 틀지 않는다 (DEC-025 design §6 - 반응 몽타주의 주인 지도).
	//   종전에는 경직 중 다음 한 대의 플린치가 bStopAllMontages 로 경직 몽타주를 덮어 UVBGA_Stagger 가 Interrupted 로 끝났다 -
	//   병사의 3.7초 기절이 다음 한 대에 끊기는 경로다. 우선순위(넘어짐 > 경직 > 무거움 > 가벼움)에서 아래가 위를 끊지 못하게 한다.
	//   한 타 안의 순서(피해 먼저 -> 기절 게이지 나중, GE_PhysicalDamage 모디파이어 순서)는 그대로라 «플린치 -> 경직이 덮음» 은 변함없다.
	//   UVBGA_Knockdown 도 Staggered 를 들므로(경직 파생) 넘어져 있는 동안의 플린치도 이 줄이 함께 막는다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Staggered);

	// 히트리액트는 일반 유효 피격 연출만 담당하며 State.Combat.Staggered 를 소유하지 않는다.
	// 해당 태그의 단독 생산자는 UVBGA_Stagger 이며, Poise 파탄만 뜻한다.
	// 대신 «맞는 중» 은 자기 태그로 드러낸다(2026-09-06). 적 BT 가 이 태그로 이동 분기를 막는다 -
	//   종전에는 플린치가 이동을 안 멈춰, 스윙이 끊긴 적이 «둘러싸기» 로 바뀌며 맞으면서 링까지 걸어 나갔다
	//   (PIE 로그: 94cm 에서 역할 전환 뒤 3초 동안 피격 3회, user 「맞으면서 도망간다」).
	//   ActivationOwnedTags 라 몽타주 4종 종료 경로 어디로 끝나도 엔진이 함께 뗀다. 플레이어에게도 붙지만 읽는 곳이 없다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_HitReacting);

	// 맞으면 상호작용(레버 홀드 등)을 끊는다. 맞으면서 레버가 끝까지 당겨지면 안 된다.
	CancelAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Interact);

	// 연타 FEEL 보존을 위해 반드시 true. 기본값 false 면 InstancedPerActor 가 활성 중일 때 재활성화가 조용히 거부된다
	// → 연타로 맞아도 히트리액트가 첫 발에만 나온다(구 동작은 bStopAllMontages 로 매번 재생 = 회귀).
	// 엔진 주석: "if true, and trying to activate an already active instanced ability, end it and re-trigger it."
	bRetriggerInstancedAbility = true;
}

void UVBGA_HitReact::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                    const FGameplayAbilityActorInfo*     ActorInfo,
                                    const FGameplayAbilityActivationInfo ActivationInfo,
                                    const FGameplayEventData*            TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 데이터 홈 미할당은 설정 오류다 — 조용히 넘기면 히트리액트가 흔적 없이 사라진다(BP 클래스 디폴트 누락).
	if (!HitReactConfig)
	{
		VB_LOG(Warning, "%s: HitReactConfig 미할당 -> 히트리액트 스킵. BP_GA_HitReact 에 DA_HitReact_Default 를 할당하라", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 피격자 = 이 GA 의 아바타(이벤트가 피격자 ASC 로 송신되므로). 공격자 기준 역추출이 필요 없다 —
	// 큐 시절 EffectContext 의 HitResult 에서 피격자를 되짚던 역전 구조가 여기서 사라진다.
	// 방향(2026-09-06, design .sw/enemy-soldier/design.md §4): 이벤트의 Instigator(공격자) 위치를 피격자 로컬로 옮겨 4분면.
	//   넉백 요(SetByCaller)를 쓰지 않는 이유: 그것은 «미는 방향» 이라 조준 보정이 섞인다. 몸짓은 «어디서 맞았나» 다.
	//   공격자를 모르면 Front - 무방향 자산과 같은 결과다.
	AActor* Avatar = GetAvatarActorFromActorInfo();
	// 실효 등급(DEC-025). 허브가 저항까지 반영해 태그 하나로 실었다 - 여기서 다시 판정하지 않는다.
	//   넘어뜨림·띄움 태그가 여기까지 왔으면 넘어짐 GA 가 받지 못한 것인데, 허브가 그때 무거움으로 내려 보내므로
	//   정상 경로에서는 안 온다. 와도 «무거움 이상» 으로 읽어 무거운 피격을 튼다(약한 쪽으로 떨어지지 않게).
	const EVBHitImpact Impact = TriggerEventData ? VBHitImpactFromTags(TriggerEventData->InstigatorTags) : EVBHitImpact::Light;

	// 피격자의 몸은 어느 등급에서도 돌리지 않는다 (2026-09-29 다섯 번째 PIE, user 「왜 날 보고 처형 준비 자세를 해 - 뒷자세로 있으면 그 상태로」).
	//   두 번째 PIE 뒤에는 무거운 피격을 공격 정면으로 돌려 세운 뒤 앞 클립을 틀었는데, 뒤에서 치면 180도 돌아 플레이어를 마주 봤고
	//   그 방향 그대로 처형 대기 자세에 들어갔다(로그 14:04:09 돌려 세움 172). 그때 돌린 이유(옆에서 맞았는데 앞 클립)는 대부분
	//   공격자 위치를 원점으로 읽던 버그였다(1-B 두 번째 PIE 5번).
	// 그래서 무거운 피격도 가벼운 피격처럼 4분면으로 고른다. 무거운 세트에 좌·우 클립이 없으면(적 골격에 없다 - MM_HitReact 는 앞·뒤뿐, 실측)
	//   밀린 방향이 몸 앞쪽이면 «뒤에서 맞음», 뒤쪽이면 «앞에서 맞음» 클립으로 떨어진다 - 무방향 한 클립(앞)으로 떨어지면 뒤에서 맞은 몸이 뒤로 젖혀진다.
	// 이동은 등급마다 한 주인이다. 무거운 클립은 제자리라 허브의 물리 넉백이 밀린 방향 그대로 내고, 루트모션 클립(가벼움)은
	//   그 이동만 밀린 방향으로 돌린다(아래 BeginRootMotionRedirect).
	float PushYaw = 0.0f;
	const bool bHasPushYaw = TriggerEventData && VBGetPushYaw(*TriggerEventData, PushYaw);
	EVBHitDirection Direction = VBComputeHitDirection(Avatar, TriggerEventData ? TriggerEventData->Instigator.Get() : nullptr);
	if (Impact >= EVBHitImpact::Heavy && bHasPushYaw && Avatar &&
		((Direction == EVBHitDirection::Left && !HitReactConfig->HeavySet.MontageLeft) ||
		 (Direction == EVBHitDirection::Right && !HitReactConfig->HeavySet.MontageRight)))
	{
		const bool bPushedForward = FVector::DotProduct(FRotator(0.0f, PushYaw, 0.0f).Vector(), Avatar->GetActorForwardVector()) > 0.0;
		Direction = bPushedForward ? EVBHitDirection::Back : EVBHitDirection::Front;
	}

	UAnimMontage* HitReactMontage = nullptr;
	PlayingImpact = EVBHitImpact::Light;
	if (Impact >= EVBHitImpact::Heavy)
	{
		HitReactMontage = HitReactConfig->ResolveHeavyMontage(Direction);
		if (HitReactMontage)
		{
			PlayingImpact = EVBHitImpact::Heavy;
		}
		if (!HitReactMontage && !bWarnedMissingHeavy)
		{
			// 가벼운 반응으로 떨어진다 - 무반응보다 낫다. 그러나 «강타인데 플린치» 는 user 가 신고한 바로 그 증상이라 소리낸다.
			bWarnedMissingHeavy = true;
			VB_LOG(Warning, "%s: %s 의 HeavySet 이 비어 무거운 피격을 가벼운 플린치로 튼다(이 액터에서 첫 1회만 알린다)",
			       *GetNameSafe(Avatar), *GetNameSafe(HitReactConfig));
		}
	}
	if (!HitReactMontage)
	{
		HitReactMontage = HitReactConfig->ResolveMontage(ResolveVictimStance(Avatar), Direction);
	}
	if (!HitReactMontage)
	{
		VB_LOG(Warning, "%s: 히트리액트 몽타주 해석 실패 -> 즉시 종료", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ServerInitiated → 서버 + 피격자 소유클라 양쪽에서 실행되어 각자 로컬 재생한다(공격 GA 와 동일 패턴).
	// sim proxy 는 서버 ASC->PlayMontage 가 갱신하는 RepAnimMontageInfo 로 수신 — 히트리액트는 섹션 점프가 없는
	// 단발 Play 라, 3+ 연속 jump 에서 불안정하던 기지 사례(VBGA_MeleeAttackBase 콤보 점프)와는 다른 경로다.
	// 몽타주 재생에는 서버 전용 부작용이 없으므로 IsNetAuthority 분기 불필요(밀림 방향 보정은 아래에서 따로 가른다).
	// TriggerEventData 에서 읽는 것은 둘이다 - Instigator(방향 4분면)와 InstigatorTags(실효 등급, DEC-025).
	// bAllowInterruptAfterBlendOut(마지막 인자)는 엔진 기본값 false 를 유지한다.
	// true 로 두면: 정상 블렌드아웃 시 Task 가 ClearAnimatingAbility 를 스킵 → OnBlendOut → EndAbility →
	//   bStopWhenAbilityEnds 의 StopPlayingMontage 가드가 여전히 통과 → 이미 블렌드아웃 중인 몽타주에 재차 stop
	//   = 자연 블렌드아웃이 잘린다. 게다가 그 플래그가 켜주려던 "블렌드아웃 후 OnInterrupted"는 영원히 오지 않는다
	//   (OnBlendOut 에서 이미 어빌리티가 끝나 ShouldBroadcastAbilityTaskDelegates()가 false).
	// 재생 사실을 한 줄 남긴다. 2026-09-06 «피격 애니메이션이 없다» 신고에서 활성화 흔적이 0줄이라
	//   «GA 가 안 켜졌다» 와 «켜졌는데 안 보인다» 를 가를 수 없었다.
	// 몽타주 재생 자체는 서버 전용 부작용이 없으나, 밀림 방향 보정(헤더 BeginRootMotionRedirect)은 서버에서만 건다 - 적 이동의 주인이 서버다.
	//   등급을 가리지 않는다 - 루트모션이 없는 클립(지금의 무거운 클립)은 그 함수가 알아서 건너뛴다.
	const bool bRedirected = bHasPushYaw && ActorInfo && ActorInfo->IsNetAuthority() && BeginRootMotionRedirect(HitReactMontage, PushYaw);

	VB_LOG(Log, "%s: 히트리액트 %s 방향=%d 등급=%s%s", *GetNameSafe(Avatar), *GetNameSafe(HitReactMontage),
	       static_cast<int32>(Direction), VBHitImpactName(Impact),
	       bRedirected ? *FString::Printf(TEXT(" (밀림 방향 보정 %.0f도)"), RootMotionRedirectYaw) : TEXT(""));

	UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			                                                               this,
			                                                               NAME_None,
			                                                               HitReactMontage,
			                                                               HitReactConfig->MontagePlayRate,
			                                                               NAME_None,
			                                                               true,
			                                                               1.0f,
			                                                               0.0f,
			                                                               false);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->ReadyForActivation();
}

bool UVBGA_HitReact::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
                                                 const FGameplayEventData*        Payload) const
{
	if (!Super::ShouldAbilityRespondToEvent(ActorInfo, Payload))
	{
		return false;
	}
	// 등급 태그가 없는 이벤트(패링 되받기 등 옛 경로)는 Light 로 읽혀 종전처럼 켜진다(VBHitImpactFromTags).
	const EVBHitImpact Incoming = Payload ? VBHitImpactFromTags(Payload->InstigatorTags) : EVBHitImpact::Light;
	if (Incoming == EVBHitImpact::None)
	{
		return false;
	}
	// 지금 도는 반응보다 약한 타격은 그 반응을 끊지 않는다(헤더 PlayingImpact 주석). 같거나 센 타격은 종전처럼 재트리거한다.
	//   거절해도 피해·발광·넉백은 허브에서 그대로 든다 - 막는 것은 «반응 클립 교체» 하나다.
	return !(IsActive() && Incoming < PlayingImpact);
}

void UVBGA_HitReact::OnHitReactEnded()
{
	// 몽타주 4개 델리게이트(Completed/BlendOut/Interrupted/Cancelled) 공통 종료. bWasCancelled=false 로 통일.
	//   정리할 것은 밀림 방향 보정 하나이고 그것은 EndAbility 가 푼다.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UVBGA_HitReact::EndAbility(const FGameplayAbilitySpecHandle     Handle,
                                const FGameplayAbilityActorInfo*     ActorInfo,
                                const FGameplayAbilityActivationInfo ActivationInfo,
                                bool                                 bReplicateEndAbility,
                                bool                                 bWasCancelled)
{
	EndRootMotionRedirect();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UVBGA_HitReact::BeginRootMotionRedirect(UAnimMontage* Montage, float PushYaw)
{
	ACharacter*                  Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* Movement  = Character ? Character->GetCharacterMovement() : nullptr;
	const USkeletalMeshComponent* Mesh     = Character ? Character->GetMesh() : nullptr;
	if (!Montage || !Movement || !Mesh || !Montage->HasRootMotion())
	{
		return false;
	}
	// 플레이어는 걸지 않는다. 이 GA 는 서버에서 돌고 플레이어 이동은 소유 클라가 예측하므로, 서버에서만 돌리면 매 타 위치 보정이 난다.
	//   플레이어 피격 클립은 지금 제자리라 잃는 것이 없다. 플레이어가 루트모션 반응을 갖는 날(단계 2) 소유 클라 쪽과 함께 본다.
	if (Character->IsPlayerControlled())
	{
		return false;
	}

	// 이 클립이 이 몸에서 미는 방향. 이름이나 4분면으로 가정하지 않고 전 구간 루트모션을 재서 월드로 옮긴다(규칙 26 «안 잰 원료»).
	//   bExtractRootMotion 은 기본값이 false 라 명시한다(VBGA_MeleeAttackBase::MeasureMontageLungeCm 과 같은 함정).
	FVector ClipLocal;
	if (const FVector* Cached = ClipTranslationCache.Find(Montage))
	{
		ClipLocal = *Cached;
	}
	else
	{
		const FAnimExtractContext Context(0.0, /*bExtractRootMotion*/ true);
		ClipLocal = Montage->ExtractRootMotionFromTrackRange(0.0f, Montage->GetPlayLength(), Context).GetTranslation();
		ClipTranslationCache.Add(Montage, ClipLocal);
	}
	const FVector ClipWorld = Mesh->GetComponentTransform().TransformVectorNoScale(ClipLocal);
	if (FVector2D(ClipWorld.X, ClipWorld.Y).Size() < 1.0f)
	{
		return false; // 수평 이동이 없는 클립 - 돌릴 것이 없다.
	}
	const float Correction = static_cast<float>(
		FRotator::NormalizeAxis(PushYaw - FMath::RadiansToDegrees(FMath::Atan2(ClipWorld.Y, ClipWorld.X))));
	// 밀린 방향과 클립이 크게 어긋나면(등지고 친 경우 등) 클립 그대로가 덜 이상하다 - 180 가까이 돌리면 몸짓과 이동이 정반대가 된다.
	constexpr float MaxRedirectDegrees = 90.0f;
	if (FMath::Abs(Correction) > MaxRedirectDegrees)
	{
		VB_LOG(Log, "%s: 밀림 방향 보정 안 함 - 클립과 밀린 방향이 %.0f도 어긋난다(상한 %.0f)",
		       *GetNameSafe(Character), Correction, MaxRedirectDegrees);
		return false;
	}
	// 이 자리의 주인은 이 GA 하나다(엔진 MotionWarping 은 Pre 쪽을 쓴다). 다른 주인이 생긴 날 조용히 빼앗지 않게 덮지 않고 알린다.
	if (Movement->ProcessRootMotionPostConvertToWorld.IsBound() &&
		!Movement->ProcessRootMotionPostConvertToWorld.IsBoundToObject(this))
	{
		VB_LOG(Warning, "%s: CMC ProcessRootMotionPostConvertToWorld 에 다른 주인이 있다 - 밀림 방향 보정을 건너뛴다", *GetNameSafe(Character));
		return false;
	}
	RootMotionRedirectYaw = Correction;
	RedirectedMontage     = Montage;
	RedirectedMovement    = Movement;
	Movement->ProcessRootMotionPostConvertToWorld.BindUObject(this, &ThisClass::RedirectRootMotion);
	return true;
}

void UVBGA_HitReact::EndRootMotionRedirect()
{
	if (UCharacterMovementComponent* Movement = RedirectedMovement.Get())
	{
		if (Movement->ProcessRootMotionPostConvertToWorld.IsBoundToObject(this))
		{
			Movement->ProcessRootMotionPostConvertToWorld.Unbind();
		}
	}
	RedirectedMovement.Reset();
	RedirectedMontage.Reset();
	RootMotionRedirectYaw = 0.0f;
}

FTransform UVBGA_HitReact::RedirectRootMotion(const FTransform& WorldRootMotion, UCharacterMovementComponent* Movement, float /*DeltaSeconds*/)
{
	// 이 반응 몽타주가 루트모션을 내는 동안만. 공격·경직 몽타주가 이어받았으면 손대지 않는다.
	const ACharacter*            Character = Movement ? Movement->GetCharacterOwner() : nullptr;
	const FAnimMontageInstance*  Instance  = Character ? Character->GetRootMotionAnimMontageInstance() : nullptr;
	if (!Instance || !RedirectedMontage.IsValid() || Instance->Montage != RedirectedMontage.Get())
	{
		return WorldRootMotion;
	}
	// 수평만 돌린다. 수직과 회전은 클립 그대로다.
	const FVector Translation = WorldRootMotion.GetTranslation();
	const FVector Horizontal  = FRotator(0.0f, RootMotionRedirectYaw, 0.0f).RotateVector(FVector(Translation.X, Translation.Y, 0.0f));
	FTransform    Redirected  = WorldRootMotion;
	Redirected.SetTranslation(FVector(Horizontal.X, Horizontal.Y, Translation.Z));
	return Redirected;
}
