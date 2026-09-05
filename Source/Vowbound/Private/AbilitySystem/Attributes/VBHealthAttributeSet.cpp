// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "GameFramework/Character.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h" // 패링 경직: 공격자 ASC 해석
#include "Character/VBGuardable.h"          // 가드/패링 상태 조회(인터페이스 — 구체 클래스 의존 제거, FIND-057a)
#include "Data/VBParryConfig.h"             // 패링 윈도우/블록 감쇠
#include "Vowbound/Vowbound.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"

UVBHealthAttributeSet::UVBHealthAttributeSet()
{
	// 기본값은 Init...()로 설정, GE_InitStats에서 외부 설정
	// 여기서는 안전한 기본값만 지정
	InitHealth(0.0f);
	InitMaxHealth(0.0f);
	InitShield(0.0f);
	InitMaxShield(0.0f);
}

void UVBHealthAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	
	// 주의 : PreAttributeChange는 CurrentValue에만 영향
	// BaseValue 클램핑이 필요하면 PostGameplayEffectExecute에서 처리
	
	if (Attribute == GetHealthAttribute())
	{
		// Health는 0 ~ MaxHealth 범위
		ClampAttributeOnChange(Attribute, NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		// MaxHealth는 1 이상 (0이면 나누기 에러 가능)
		CachedMaxHealth = GetMaxHealth();
		NewValue = FMath::Max(1.0f, NewValue);
	}
	else if (Attribute == GetShieldAttribute())
	{
		ClampAttributeOnChange(Attribute, NewValue, 0.0f, GetMaxShield());
	}
	else if (Attribute == GetMaxShieldAttribute())
	{
		// MaxShield는 1 이상 (0이면 나누기 에러 가능)
		CachedMaxShield = GetMaxShield();
		NewValue = FMath::Max(1.0f, NewValue);
	}
	
}

void UVBHealthAttributeSet::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	
	// IncomingDamage 처리 (Meta Attribute)
	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		float DamageValue = GetIncomingDamage();
		// Meta Attribute 초기화 (다음 계산을 위해)
		SetIncomingDamage(0.0f);
		
		// OwnerASC 를 블록 밖으로 호이스트 — 아래 히트리액트 이벤트 송신에서 재사용한다(값은 동일, 동작 변화 없음).
		UAbilitySystemComponent* OwnerASC = GetOwningAbilitySystemComponent();

		if (DamageValue > 0.0f)
		{
			if (OwnerASC && OwnerASC->HasMatchingGameplayTag(
				VBGameplayTags::State_Combat_Dodging))
			{
				// 회피 중 무적
				VB_LOG(Log, "Dodge i-frame : Damage nullified");
				DamageValue = 0.0f;
			}
		}

		// 가드/패링 판정 (DEC-009 카타나 파일럿). dodge i-frame 다음, shield 차감 전. 서버 권위 단일 진리점.
		//  IsGuarding 상태는 VBGA_Guard 가 서버에서 BeginGuard/EndGuard 로 관리(GuardStartServerTime=서버 진실). IsGuarding true 면 그 무기의 가드 세트가 등록돼 있다는 뜻(UVBParryConfig::WeaponGuardSets — 2026-07-26 카타나 하드코딩 게이트 소멸).
		//  bGuardedThisHit: 막았으면(블록/패링) 아래 히트리액트(플린치)·넉백을 억제한다 — 가드는 '받아넘김(Accept)'이지 '피격(Hit)'이 아니다(user 06-26).
		bool bGuardedThisHit = false;
		if (DamageValue > 0.0f)
		{
			// 피격자를 인터페이스로 본다(FIND-057a). 구체 클래스 의존 제거 — 적/보스도 IVBGuardable 만
			//  구현하면 이 hub 를 그대로 탄다(기존엔 Cast<AVBCharacter> 라 적은 영원히 가드 불가였다).
			//  액터 포인터를 따로 드는 이유: 아래 패링 경직 이벤트의 Instigator 가 AActor* 를 요구한다.
			AActor* VictimActor = OwnerASC ? OwnerASC->GetAvatarActor() : nullptr;
			if (IVBGuardable* Guardable = Cast<IVBGuardable>(VictimActor))
			{
				if (Guardable->IsGuarding())
				{
					const UVBParryConfig* PCfg = Guardable->GetParryConfig();
					// 삼항 null 폴백 제거 (FIND-056①): GetParryConfig 는 미할당 시 CDO 를 반환하므로 절대 null 이
					//  아니다(VBCharacter.cpp:459 GetParryConfig = IVBGuardable 계약 1). 옛 폴백(0.15 / 0.0 / 0.30)은 도달 불가
					//  죽은 코드였고, 그중 StaggerHitReactMagnitude 폴백 0.0 은 config 디폴트 2.0 과 값까지 갈라져
					//  있었다 — 한쪽만 고치면 조용히 어긋나는 이중 정의라 제거 자체가 근본 수정이다.
					//  폴백 대신 ensure 로 계약을 코드에 못박는다(미래의 다른 구현체 방어). 계약이 깨진 프레임엔
					//  가드 판정 전체를 스킵한다 — 튜닝값 없이 패링/블록을 흉내내면 값이 또 갈린다.
					if (ensureMsgf(PCfg, TEXT("IVBGuardable::GetParryConfig() returned null - CDO fallback contract violated")))
					{
						// 블록·패링 공통: 받아넘김(Accept) 연출 재생 + 플린치 억제(bGuardedThisHit). 차이는 데미지량과 공격자 경직뿐.
						bGuardedThisHit = true;
						Guardable->PlayGuardAccept();
						if (Guardable->GetGuardElapsed() <= PCfg->ParryWindow)
						{
							// ── 패링 성공: 데미지 무효 + 공격자 경직 ──
							VB_LOG(Log, "Parry : Damage nullified + attacker stagger");
							DamageValue = 0.0f;

							// 패링 보상 - 스태미나 회복 + 반격 창 개방(DEC-009 phase-2).
							// 창 수명은 캐릭터가 관리한다. 여기는 프레임 단위 계산기라 타이머의 주인이 아니다.
							Guardable->ApplyGuardStaminaDelta(+PCfg->StaminaGainOnParry);

							// 공격자 해석을 경직 처리보다 위로 올렸다 - 반격 창 개방이 자동 락온 대상으로 함께 쓴다.
							AActor* ParriedAttacker = Data.EffectSpec.GetContext().GetInstigator();
							Guardable->OpenRiposteWindow(PCfg->RiposteWindow, ParriedAttacker);
							// 경직에 GE_Stagger 사용 금지(no-op 확정 FIND-032/033). 공격자 히트리액트(Event.Combat.Hit)를 재사용한다.
							if (UAbilitySystemComponent* AttackerASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(ParriedAttacker))
							{
								FGameplayEventData StaggerEvent;
								StaggerEvent.EventTag       = VBGameplayTags::Event_Combat_Hit;
								StaggerEvent.Instigator     = VictimActor;   // 되받아치는 주체 = 패링한 쪽
								StaggerEvent.Target         = ParriedAttacker;
								StaggerEvent.EventMagnitude = PCfg->StaggerHitReactMagnitude;
								AttackerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_Hit, &StaggerEvent);
							}
						}
						else
						{
							// ── 블록: 데미지 감쇠(윈도우 밖 가드 유지 중). 공격자 경직 없음(패링만 경직) ──
							const float Mult = PCfg->BlockDamageMultiplier;
							VB_LOG(Log, "Block : Damage x%.2f (Accept, no flinch)", Mult);
							DamageValue *= Mult;

							// 블록 비용 - 막을수록 스태미나가 준다. 패링에는 비용이 없어
							// "정확히 받아치면 손해가 없다"는 대비가 손맛을 만든다(GDD 11 §4.1).
							Guardable->ApplyGuardStaminaDelta(-PCfg->StaminaCostOnBlock);
						}
					}
				}
			}
		}

		if (DamageValue > 0.0f)
		{
			// 받는 쪽 1줄. 보내는 쪽(VBGA_MeleeAttackBase)의 곱셈 사슬 로그와 짝이다 -
			//  둘을 대조하면 "스펙과 실제가 다른가"가 로그만으로 판정된다. 2026-08-10 에 그 대조가
			//  불가능해서 자산을 하나씩 까야 했다. 게이트/실드 처리 직후 값이라 감쇠까지 반영된다.
			// 대상과 출처 GE 를 함께 남긴다. 값만 있으면 "누가 때렸는지"를 못 좁힌다 -
			//  2026-08-10 에 정체불명 40 의 출처를 자산 전수조사로 쫓아야 했던 이유가 이것이다.
			const AActor* DamagedActor = GetAvatarActorFromOwner();
			const UGameplayEffect* SourceDef = Data.EffectSpec.Def;
			VB_LOG(Log, "피격[%s]: 데미지 %.1f (출처 %s) / Shield %.1f / Health %.1f→%.1f (Max %.1f)",
			       DamagedActor ? *DamagedActor->GetName() : TEXT("Unknown"),
			       DamageValue, SourceDef ? *SourceDef->GetName() : TEXT("Unknown"),
			       GetShield(), GetHealth(),
			       FMath::Max(0.0f, GetHealth() - FMath::Max(0.0f, DamageValue - GetShield())),
			       GetMaxHealth());

			float RemainingDamage = DamageValue;
			
			// 1단계 : Shield먼저 차감
			if (GetShield() > 0.0f)
			{
				const float ShieldAbsorb = FMath::Min(GetShield(), RemainingDamage);
				SetShield(GetShield() - ShieldAbsorb);
				RemainingDamage -= ShieldAbsorb;
			}
			
			// 2단계 : 남은 데미지를 Health에서 차감
			if (RemainingDamage > 0.0f)
			{
				const float NewHealth = FMath::Max(0.0f, GetHealth() - RemainingDamage);

				// 3단계 : 사망 컨텍스트 저장 (AVBEnemyBase::HandleDeath 가 읽는다)
				//
				// 반드시 SetHealth 보다 먼저여야 한다 (2026-08-19 정정).
				// SetHealth 는 체력 변경 델리게이트를 그 자리에서 발화시키고, 그 끝이
				//   AVBEnemyBase::OnHealthChanged -> HandleDeath -> ApplyDefaultDeathReputation 이다.
				//   즉 사망 처리 전체가 이 함수의 다음 줄보다 먼저 끝난다.
				// 종전에는 이 저장이 SetHealth 뒤에 있어 사망 처리가 항상 빈 기록을 읽었고,
				//   그래서 명성이 한 번도 적용된 적이 없다. 조용한 실패라 UI 가 생기기 전까지 아무도 몰랐다
				//   (2026-08-19 로그 실증: "누가 죽였는지 기록이 없어 명성을 못 준다" 가 사망과 같은 프레임).
				// 조건이 GetHealth() 가 아니라 NewHealth 인 것도 같은 이유다 - 아직 쓰기 전이라
				//   현재 값을 물으면 죽기 직전 체력이 나온다.
				if (NewHealth <= 0.0f)
				{
					LastDamageInstigator = Data.EffectSpec.GetContext().GetInstigator();

					// 의료 행위(정화/처치) 여부. GA_Purify/Execute 가 DynamicAssetTags 에 태그를 설정한다.
					FGameplayTagContainer AssetTags;
					Data.EffectSpec.GetAllAssetTags(AssetTags);
					bLastDamageWasMedical =
						AssetTags.HasTagExact(VBGameplayTags::Effect_Purify) ||
						AssetTags.HasTagExact(VBGameplayTags::Effect_Execute);
				}

				SetHealth(NewHealth);
			}

			// 4단계 : 히트리액트 트리거 (DEBT-009 2단계) — 피격자 UVBGA_HitReact 를 활성화한다.
			//
			// 왜 여기(공격자 쪽이 아니라)인가:
			//  - 도조 i-frame 판정이 이 함수 안에 있다(위 DamageValue=0). 공격자는 그 결과를 모르므로,
			//    공격자가 이벤트를 쏘면 무적 회피 중에도 히트리액트가 재생된다. 여기서 쏘면 DamageValue==0 일 때
			//    이 지점에 도달조차 못 해 그 버그가 구조적으로 소멸한다.
			//  - "데미지가 실제로 들어갔다"는 단일 진리 지점이라, 미래의 무적/흡수/면역이 추가돼도 자동 대응된다.
			//  - 이 콜백은 대상 GE 적용 1회당 1번 실행 → 광역 N명이 각자 이벤트를 받는다(per-target 수동 배분 불필요).
			//
			// 왜 서버 안전한가: PostGameplayEffectExecute 는 권위 머신 전용이고, ServerInitiated 트리거 활성화는
			//  서버 권위에서만 시도된다(HasNetworkAuthorityToActivateTriggeredAbility) → 이중 활성화 불가.
			//  IsOwnerActorAuthoritative 가드는 그 계약을 코드에 명시해 두는 의미.
			//
			// 부수효과: GE 적용 스택 안에서 피격자 GA 가 활성화된다(재진입). HandleGameplayEvent 는 자체
			//  ABILITYLIST_SCOPE_LOCK 을 걸며, 이는 Epic 이 쓰는 표준 패턴이다.
			//
			// 치명타(사망) 시에도 이벤트는 송신하지만, 히트리액트는 재생되지 않는다:
			//  적은 위 SetHealth() 가 동기적으로 AVBEnemyBase::OnHealthChanged → HandleDeath → AddLooseGameplayTag(State.Dead)
			//  를 이 송신보다 '먼저' 일으키고, State.Dead 는 UVBGA_HitReact.ActivationBlockedTags 에 있으므로 활성화가 차단된다.
			//  → 사망 연출 위에 히트리액트가 덮이지 않는다. (구 큐 시절 VBGC_DamagePhysicalBase 도 State.Dead 를 명시 게이트했다.)
			//  가드 억제: 막았으면(bGuardedThisHit) 플린치·넉백을 쏘지 않는다. 이미 Accept(받아넘김)를 재생했다.
			//  블록은 데미지만 감쇠하고 반응은 가드다(user 06-26).
			if (OwnerASC && OwnerASC->IsOwnerActorAuthoritative() && !bGuardedThisHit)
			{
				FGameplayEventData EventData;
				EventData.EventTag       = VBGameplayTags::Event_Combat_Hit;
				EventData.Instigator     = Data.EffectSpec.GetContext().GetInstigator();
				EventData.Target         = OwnerASC->GetAvatarActor();
				EventData.EventMagnitude = DamageValue;
				// HitResult 를 품은 컨텍스트 동봉 — 방향별 히트리액트 확장 시 payload 로 쓴다(현재 미사용).
				EventData.ContextHandle   = Data.EffectSpec.GetContext();

				OwnerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_Hit, &EventData);

				// 5단계 : (A) 넉백 (FIND-035) — 데미지가 실제로 들어간 이 지점이 유일 진리.
				//  넉백=간격 회복, 플린치=반응 → 다른 책임이라 여기(HitReact GA 밖)에 둔다. 뚝심(SuperArmor)이 플린치는
				//  막아도 넉백은 유지되어야 한다(뚝심 적을 못 밀면 '비비는' 결함이 무거운 적에 재발하므로).
				//  방향 = 공격자→피격자 수평(히트 노멀은 다중히트/캡슐에서 noisy). 세기 = 공격자 스탠스값(SetByCaller).
				//  사망 게이트: 킬링블로우면 위 SetHealth→HandleDeath 가 State.Dead 를 이 지점 '전에' 부여 → 스킵(래그돌 충돌 방지).
				const float KnockStrength = Data.EffectSpec.GetSetByCallerMagnitude(
					VBGameplayTags::Data_KnockbackStrength, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f);
				if (KnockStrength > 0.0f && !UVBAbilitySystemStatics::IsDead(OwnerASC))
				{
					AActor* KnockInstigator = Data.EffectSpec.GetContext().GetInstigator();
					ACharacter* VictimChar = Cast<ACharacter>(OwnerASC->GetAvatarActor());
					if (VictimChar && KnockInstigator)
					{
						// 방향은 공격자가 스펙에 실어 보낸 스윙 방향이다. 2026-08-06 전환.
						// 왜 여기서 직접 만들지 않나: 이 지점은 공격자의 회전만 볼 수 있고 조준 보정
						//   (AimAssistMaxAngle, 최대 35도)과 저작된 스윙 축은 볼 수 없다. 공격자 정면으로
						//   밀면 판정이 꺾여 나간 만큼 넉백이 어긋난다 - 공격 경로에 방향이 셋(판정/넉백/쉐이크)
						//   있었고 서로 달랐던 것이 원인이었다(user 실측 신고).
						// 이력: 위치선(방사형, 스윙 그림이 깨짐) -> 공격자 정면(조준 보정과 어긋남) -> 스윙 방향.
						// 히트 노멀을 안 쓰는 이유는 종전과 같다: 다중히트/캡슐에서 noisy 하다.
						// yaw 하나로 충분한 이유: 상방 성분은 KnockbackZ 가 따로 담당해 수평만 결정하면 된다.
						const float KnockYaw = Data.EffectSpec.GetSetByCallerMagnitude(
							VBGameplayTags::Data_KnockbackYaw, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/TNumericLimits<float>::Lowest());
						FVector AwayDir = FVector::ZeroVector;
						if (KnockYaw > TNumericLimits<float>::Lowest())
						{
							AwayDir = FRotator(0.0f, KnockYaw, 0.0f).Vector();
						}
						if (AwayDir.IsNearlyZero())
						{
							// 스펙에 방향이 없는 경로(구 스펙/비근접 데미지)만 공격자 정면으로 폴백한다.
							AwayDir = KnockInstigator->GetActorForwardVector();
							AwayDir.Z = 0.0f;
						}
						if (AwayDir.IsNearlyZero())
						{
							// 정면이 퇴화한 경우(액터 회전이 수직 등)에만 위치선으로 폴백한다.
							AwayDir = VictimChar->GetActorLocation() - KnockInstigator->GetActorLocation();
							AwayDir.Z = 0.0f;
						}
						if (!AwayDir.IsNearlyZero())
						{
							AwayDir.Normalize();
							// 상방 성분을 실어 실제로 띄운다. Z=0 이면 다음 프레임에 바닥을 찾아 Walking 으로
							// 복귀하고 지상 마찰이 수평 속도를 즉시 지운다(KnockbackZ 주석 참조).
							const float KnockZ = Data.EffectSpec.GetSetByCallerMagnitude(
								VBGameplayTags::Data_KnockbackZ, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f);
							// 서버 전용: 적은 PC 없는 서버권위 AI → LaunchCharacter 가 CMC velocity 를 sim proxy 로 복제(Dodge 동형).
							// 이 블록 자체가 IsOwnerActorAuthoritative 가드 안이라 서버에서만 실행됨.
							VictimChar->LaunchCharacter(AwayDir * KnockStrength + FVector(0.0f, 0.0f, KnockZ),
							                            /*bXYOverride=*/true, /*bZOverride=*/false);
						}
					}
				}

				// 5단계 : (B) 피격 플래시 큐 — 피격자 메시를 순간 발광시키는 순수 코스메틱.
				//
				// 왜 공격자 쪽(무기 트레이스)이 아니라 여기인가:
				//  - 이 지점은 이미 "i-frame 통과 + 가드 아님 + DamageValue>0" 안쪽이다. 회피/패링 시 무플래시가
				//    별도 게이트 없이 자동 보장된다. 공격자는 피격자의 판정 결과를 모른 채 발화하므로 그 보장이 없다.
				//  - 이 콜백은 대상 GE 적용 1회당 1번 실행 → 광역 N명이 각자 번쩍인다(per-target 수동 배분 불필요).
				//
				// 왜 피격자 ASC 로 쏘는가: 큐가 도착한 쪽의 MyTarget 이 곧 피격자(AvatarActor)다.
				//  공격자 ASC 로 쏘면 큐 안에서 "누가 맞았는지"를 컨텍스트에서 역추출해야 한다.
				//
				// 킬링블로우도 반짝인다(의도). 위 SetHealth() 가 동기적으로 State.Dead 를 붙인 뒤 이 지점에 오지만
				//  이 블록에는 Dead 게이트가 없다. 플린치는 ActivationBlockedTags 로 차단되고, 넉백은 래그돌 충돌
				//  때문에 자기 손으로 Dead 를 검사한다. 세 가지의 차이는 전적으로 "어디에 게이트를 두었는가"가 만든다.
				//
				// 데디케이티드 서버에서 시각 효과가 안 나는 것은 정상이다 — 순수 코스메틱이라 게임플레이 영향이 없다.
				// 범위 제한: 무기 메시는 별도 컴포넌트라 플래시 대상이 아니다. 적은 WeaponStateComponent 를
				//  갖지 않으므로 현 범위에서는 무해하다.
				FGameplayCueParameters FlashCueParams;
				FlashCueParams.RawMagnitude  = DamageValue;   // 컴포넌트가 데미지→강도 커브 입력으로 쓴다
				FlashCueParams.EffectContext = Data.EffectSpec.GetContext();
				OwnerASC->ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_HitFlash, FlashCueParams);
			}
		}
	}

	// IncomingHealing 처리
	if (Data.EvaluatedData.Attribute == GetIncomingHealingAttribute())
	{
		const float HealValue = GetIncomingHealing();
		SetIncomingHealing(0.0f);
		
		if (HealValue > 0.0f)
		{
			const float NewHealth = FMath::Min(GetMaxHealth(), GetHealth() + HealValue);
			SetHealth(NewHealth);
		}
	}

	
	if (Data.EvaluatedData.Attribute == GetMaxHealthAttribute())
	{
		if (CachedMaxHealth > 0.0f)
		{
			const float Ratio = GetHealth() / CachedMaxHealth;
			SetHealth(FMath::Clamp(Ratio * GetMaxHealth(), 0.0f, GetMaxHealth()));
		}
	}
	
	if (Data.EvaluatedData.Attribute == GetMaxShieldAttribute())
	{
		if (CachedMaxShield > 0.0f)
		{
			const float Ratio = GetShield() / CachedMaxShield;
			SetShield(FMath::Clamp(Ratio * GetMaxShield(), 0.0f, GetMaxShield()));
		}
	}
}

void UVBHealthAttributeSet::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	// REPNOTIFY_Always : 같은 값이 다시 세팅되어도 OnRep 호출
	// UI 동기화에 필요
	DOREPLIFETIME_CONDITION_NOTIFY(UVBHealthAttributeSet, Health,
		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBHealthAttributeSet, MaxHealth,
		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBHealthAttributeSet, Shield,
		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBHealthAttributeSet, MaxShield,
		COND_None, REPNOTIFY_Always);
}

void UVBHealthAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBHealthAttributeSet, Health, OldHealth);
}

void UVBHealthAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBHealthAttributeSet, MaxHealth, OldMaxHealth);
}

void UVBHealthAttributeSet::OnRep_Shield(const FGameplayAttributeData& OldShield)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBHealthAttributeSet, Shield, OldShield);
}

void UVBHealthAttributeSet::OnRep_MaxShield(const FGameplayAttributeData& OldMaxShield)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBHealthAttributeSet, MaxShield, OldMaxShield);
}