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
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h" // DamageTakenScale - 다운 취약 배수(design .sw/boss/design.md §14.3)
#include "AbilitySystem/VBHitImpact.h"                    // 반응 등급 해석(DEC-025)
#include "Character/VBDownedImpactReceiver.h"             // 누운 몸 충격(설계 .sw/downed-hit) - 적 클래스를 알지 않게 인터페이스로
#include "GameFramework/CharacterMovementComponent.h"     // 띄움 높이 -> 속도(GetGravityZ)

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

			// 처형을 «거는 중» 무적 (user 결정 2026-09-13: 「다른 애들은 때릴 수 없어야돼」).
			//
			// 회피 무적 바로 옆에 두는 이유: 여기가 「이 피해를 무를 것인가」의 홈이고, 가드·실드보다
			//   앞이라 감쇠 계산을 안 거친다. 다른 곳에서 막으면 판단이 두 곳에 산다.
			// Poise 는 따로 볼 필요가 없다 - 피해가 0 이면 이 함수 뒷단의 Poise 경로도 안 탄다.
			// 피해자(처형당하는 쪽)에는 안 걸린다. 그쪽 태그는 State.Combat.FinisherWindow 로 다르고,
			//   처형 피해는 그 적에게 들어가야 한다.
			else if (OwnerASC && OwnerASC->HasMatchingGameplayTag(
				VBGameplayTags::State_Combat_Finishing))
			{
				VB_LOG(Log, "처형 중 무적: %s 에게 온 피해 %.0f 을 무른다",
				       *GetNameSafe(OwnerASC->GetAvatarActor()), DamageValue);
				DamageValue = 0.0f;
			}
		}

		// 가드/패링 판정 (DEC-009 카타나 파일럿). dodge i-frame 다음, shield 차감 전. 서버 권위 단일 진리점.
		//  IsGuarding 상태는 VBGA_Guard 가 서버에서 BeginGuard/EndGuard 로 관리(GuardStartServerTime=서버 진실). IsGuarding true 면 그 무기의 가드 세트가 등록돼 있다는 뜻(UVBParryConfig::WeaponGuardSets — 2026-07-26 카타나 하드코딩 게이트 소멸).
		//  bGuardedThisHit: 막았으면(블록/패링) 아래 히트리액트(플린치)·넉백을 억제한다 — 가드는 '받아넘김(Accept)'이지 '피격(Hit)'이 아니다(user 06-26).
		bool bGuardedThisHit = false;
		// «답» 의 이행 (design §11): 공격 스펙이 Attack.Answer.DodgeOnly 를 실었으면 가드·패링을 건너뛴다 -
		//  예고 큐가 빨강으로 한 약속을 여기서 지킨다. 값의 홈은 공격 자산(UVBAttackConfig::AttackAnswer) 하나다.
		//  회피 무적(위)은 그대로 통한다 - «회피만» 이지 «무적» 이 아니다.
		const bool bUnblockable = Data.EffectSpec.GetDynamicAssetTags().HasTagExact(VBGameplayTags::Attack_Answer_DodgeOnly);
		if (DamageValue > 0.0f)
		{
			// 피격자를 인터페이스로 본다(FIND-057a). 구체 클래스 의존 제거 — 적/보스도 IVBGuardable 만
			//  구현하면 이 hub 를 그대로 탄다(기존엔 Cast<AVBCharacter> 라 적은 영원히 가드 불가였다).
			//  액터 포인터를 따로 드는 이유: 아래 패링 경직 이벤트의 Instigator 가 AActor* 를 요구한다.
			AActor* VictimActor = OwnerASC ? OwnerASC->GetAvatarActor() : nullptr;
			if (IVBGuardable* Guardable = Cast<IVBGuardable>(VictimActor))
			{
				if (bUnblockable && Guardable->IsGuarding())
				{
					VB_LOG(Log, "Unblockable : guard ignored (Attack.Answer.DodgeOnly)");
				}
				else if (Guardable->IsGuarding())
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

		// 받는 쪽 배수 (design .sw/boss/design.md §14.3). 가드·블록 판정 뒤, 실드 차감 앞 - «이 피해를 무를 것인가» 가 끝난 값에 곱한다.
		//   값의 홈은 UVBCombatAttributeSet::DamageTakenScale(기본 1.0) 하나고, 그로기 중에는 취약 GE 가 그 값을 올린다.
		//   세트가 없는 액터는 곱하지 않는다 - GetNumericAttribute 는 없는 속성에 0 을 돌려주므로 그대로 곱하면 무적이 된다.
		if (DamageValue > 0.0f && OwnerASC &&
		    OwnerASC->HasAttributeSetForAttribute(UVBCombatAttributeSet::GetDamageTakenScaleAttribute()))
		{
			const float Scale = OwnerASC->GetNumericAttribute(UVBCombatAttributeSet::GetDamageTakenScaleAttribute());
			if (!FMath::IsNearlyEqual(Scale, 1.0f))
			{
				VB_LOG(Log, "받는 피해 배수 x%.2f : %.1f -> %.1f", Scale, DamageValue, DamageValue * Scale);
				DamageValue *= Scale;
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
				// 4단계 (0) : 반응 등급 (DEC-025, design .sw/hit-reaction-tiers/design.md §4).
				//  공격이 선언한 등급을 받는 쪽 저항만큼 깎는다. 판정은 여기 한 곳이다 - 반응 GA 는 결과(태그)만 본다.
				//  세트가 없는 액터는 저항 0 / 넉백 배수 1 로 읽는다. GetNumericAttribute 는 없는 속성에 0 을 돌려주므로
				//  그대로 읽으면 넉백 배수가 0(안 밀림)이 된다 - 위 DamageTakenScale 이 피한 것과 같은 함정이다.
				const auto ReadCombatAttribute = [OwnerASC](const FGameplayAttribute& Attribute, float Default)
				{
					return OwnerASC->HasAttributeSetForAttribute(Attribute) ? OwnerASC->GetNumericAttribute(Attribute) : Default;
				};
				const EVBHitImpact DeclaredImpact = VBHitImpactFromMagnitude(Data.EffectSpec.GetSetByCallerMagnitude(
					VBGameplayTags::Data_HitImpact, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f));
				const float ImpactResistance = ReadCombatAttribute(UVBCombatAttributeSet::GetImpactResistanceAttribute(), 0.0f);
				EVBHitImpact Impact = VBResolveHitImpact(DeclaredImpact, ImpactResistance);
				// 넘어짐 GA 가 못 받으면 아래에서 Impact 가 Heavy 로 내려간다. 누운 몸의 충격(5단계)은 그 내림 «전» 의 실효 등급을 쓴다 -
				//   누운 몸이라 넘어짐 GA 가 막힌 것이지 타격이 약해진 것이 아니다.
				const EVBHitImpact ResolvedImpact = Impact;
				const bool bVictimDead = UVBAbilitySystemStatics::IsDead(OwnerASC);
				// 이 타격 «전에» 엎드려 있었나. 넘어짐 이벤트가 Downed 를 새로 붙이므로 그 전에 읽는다 -
				//  뒤에 읽으면 방금 넘어뜨린(띄운) 몸까지 «엎드린 몸» 으로 보여 아래 이동이 통째로 빠진다.
				const bool bDownedBeforeHit = UVBAbilitySystemStatics::IsDowned(OwnerASC);

				// 밀린 방향(수평 단위 벡터). 넉백(아래 5단계)과 반응 GA 가 «같은 값» 을 쓰도록 여기서 한 번만 정한다
				//  (2026-09-29, user 「넉백은 내가 공격하는 정면 방향으로」). 넘어짐 GA 는 피격자를 이 방향의 반대로 돌려 세우고
				//  (넘어짐 클립이 한 방향뿐), 피격 반응 GA 는 몸은 두고 클립의 이동만 이 방향으로 돌린다 - 넉백·이동·넘어지는 쪽이 한 방향이 된다.
				//  반응 이벤트에는 근접 판정이 스펙에 방향을 실은 타격일 때만 싣는다(bSpecHasPushYaw) - 출혈 폭발 같은 비근접 피해의
				//  «공격자 정면» 은 밀린 방향이 아니다(2026-09-30 /sw:verify).
				// 방향은 공격자가 스펙에 실어 보낸 판정 방향이다(2026-08-06 전환).
				// 왜 여기서 직접 만들지 않나: 이 지점은 공격자의 회전만 볼 수 있고 조준 보정
				//   (AimAssistMaxAngle, 최대 35도)과 저작된 스윙 축은 볼 수 없다. 공격자 정면으로
				//   밀면 판정이 꺾여 나간 만큼 넉백이 어긋난다 - 공격 경로에 방향이 셋(판정/넉백/쉐이크)
				//   있었고 서로 달랐던 것이 원인이었다(user 실측 신고).
				// 이력: 위치선(방사형, 스윙 그림이 깨짐) -> 공격자 정면(조준 보정과 어긋남) -> 스윙 방향.
				// 히트 노멀을 안 쓰는 이유는 종전과 같다: 다중히트/캡슐에서 noisy 하다.
				// yaw 하나로 충분한 이유: 상방 성분은 KnockbackZ 가 따로 담당해 수평만 결정하면 된다.
				// 예비 경로(아래)가 쓰는 공격자는 «위치·회전을 가진 몸» 이어야 한다. 효과 컨텍스트의 Instigator 는 ASC 소유자라
				//  플레이어가 때리면 PlayerState(원점, 회전 0)다(AbilitySystemComponent.cpp::UAbilitySystemComponent::MakeEffectContext -
				//  AddInstigator(OwnerActor, AvatarActor)). 그래서 EffectCauser(= 아바타)를 먼저 쓴다(2026-09-29 vowbound-5b 발견,
				//  VBComputeHitDirection 이 같은 이유로 한 분면만 내던 것과 같은 모양).
				AActor* const HitInstigator = Data.EffectSpec.GetContext().GetEffectCauser()
					? Data.EffectSpec.GetContext().GetEffectCauser()
					: Data.EffectSpec.GetContext().GetInstigator();
				FVector PushDir = FVector::ZeroVector;
				bool bSpecHasPushYaw = false;
				{
					const float KnockYaw = Data.EffectSpec.GetSetByCallerMagnitude(
						VBGameplayTags::Data_KnockbackYaw, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/TNumericLimits<float>::Lowest());
					// 유한한 값만 받는다 - NaN 은 GetSafeNormal·IsNearlyZero 를 그대로 통과해 LaunchCharacter 까지 간다.
					if (KnockYaw > TNumericLimits<float>::Lowest() && FMath::IsFinite(KnockYaw))
					{
						PushDir = FRotator(0.0f, KnockYaw, 0.0f).Vector();
						bSpecHasPushYaw = true;
					}
					if (PushDir.IsNearlyZero() && HitInstigator)
					{
						// 스펙에 방향이 없는 경로(구 스펙/비근접 데미지)만 공격자 정면으로 폴백한다.
						PushDir = HitInstigator->GetActorForwardVector();
						PushDir.Z = 0.0f;
					}
					if (PushDir.IsNearlyZero() && HitInstigator && OwnerASC->GetAvatarActor())
					{
						// 정면이 퇴화한 경우(액터 회전이 수직 등)에만 위치선으로 폴백한다.
						PushDir = OwnerASC->GetAvatarActor()->GetActorLocation() - HitInstigator->GetActorLocation();
						PushDir.Z = 0.0f;
					}
					PushDir = PushDir.GetSafeNormal();
				}

				// 넘어뜨림 이상은 넘어짐 GA 에 먼저 묻는다. 켜진 수가 0 이면(그 GA 가 없다·콘텐츠가 없다·이미 엎드려 있다)
				//  무거운 피격으로 내린다 - 「넘어졌어야 했는데 아무 일도 없음」 이 아니라 한 단계 아래 반응이 나온다.
				//  Hit 보다 먼저 보내는 이유: 넘어짐 GA 가 서며 Downed 를 붙이면 뒤따르는 Hit 의 플린치가
				//  UVBGA_HitReact.ActivationBlockedTags(Downed) 에 걸려 저절로 안 켜진다. 반대 순서면 플린치가 한 프레임 섰다가 덮인다.
				//  켜진 수 = HandleGameplayEvent 의 반환값(AbilitySystemComponent_Abilities.cpp::UAbilitySystemComponent::HandleGameplayEvent
				//  가 TriggerAbilityFromGameplayEvent 성공 수를 센다). 넘어짐 GA 는 ShouldAbilityRespondToEvent 로 콘텐츠 유무를 답한다.
				int32 KnockdownTaken = 0;
				if (Impact >= EVBHitImpact::Knockdown && !bVictimDead)
				{
					FGameplayEventData DownEvent;
					DownEvent.EventTag       = VBGameplayTags::Event_Combat_KnockedDown;
					DownEvent.Instigator     = Data.EffectSpec.GetContext().GetInstigator();
					DownEvent.Target         = OwnerASC->GetAvatarActor();
					DownEvent.EventMagnitude = DamageValue;
					DownEvent.ContextHandle  = Data.EffectSpec.GetContext();
					DownEvent.InstigatorTags.AddTag(VBHitImpactToTag(Impact));
					if (bSpecHasPushYaw && !PushDir.IsNearlyZero())
					{
						VBSetPushYaw(DownEvent, PushDir.Rotation().Yaw);
					}
					KnockdownTaken = OwnerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_KnockedDown, &DownEvent);
					if (KnockdownTaken == 0)
					{
						Impact = EVBHitImpact::Heavy;
					}
				}

				FGameplayEventData EventData;
				EventData.EventTag       = VBGameplayTags::Event_Combat_Hit;
				EventData.Instigator     = Data.EffectSpec.GetContext().GetInstigator();
				EventData.Target         = OwnerASC->GetAvatarActor();
				EventData.EventMagnitude = DamageValue;
				// HitResult 를 품은 컨텍스트 동봉 — 방향별 히트리액트 확장 시 payload 로 쓴다(현재 미사용).
				EventData.ContextHandle   = Data.EffectSpec.GetContext();
				// 실효 등급 하나. 반응 GA 가 이것으로 콘텐츠를 고르고, None 이면 켜지지 않는다(UVBGA_HitReact::ShouldAbilityRespondToEvent).
				//  None 이어도 이벤트 자체는 보낸다 - 엎드린 몸의 다운 피격(UVBGA_Knockdown 의 Hit 구독)은 등급과 무관하게 반응해야 한다.
				EventData.InstigatorTags.AddTag(VBHitImpactToTag(Impact));
				if (bSpecHasPushYaw && !PushDir.IsNearlyZero())
				{
					VBSetPushYaw(EventData, PushDir.Rotation().Yaw);
				}

				OwnerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_Hit, &EventData);

				// 5단계 : (A) 넉백 (FIND-035) — 데미지가 실제로 들어간 이 지점이 유일 진리.
				//  넉백=간격 회복, 플린치=반응 → 다른 책임이라 여기(HitReact GA 밖)에 둔다. 슈퍼아머(SuperArmor)가 플린치는
				//  막아도 넉백은 유지되어야 한다(슈퍼아머 적을 못 밀면 '비비는' 결함이 무거운 적에 재발하므로).
				//  단, 슈퍼아머 중 자기 공격 몽타주가 루트모션이면 그 몽타주가 이동의 주인이라 밀리지 않는다 - 아래 이동 규칙이고 엔진이 정한 것이다.
				//  방향 = 공격자→피격자 수평(히트 노멀은 다중히트/캡슐에서 noisy). 세기 = 공격자 스탠스값(SetByCaller).
				//  사망 게이트: 킬링블로우면 위 SetHealth→HandleDeath 가 State.Dead 를 이 지점 '전에' 부여 → 스킵(래그돌 충돌 방지).
				//
				//  이동의 주인은 한 순간 하나다(DEC-025 design §5). 엔진이 애니 루트모션을 무조건 우선한다
				//   (CharacterMovementComponent.cpp::UCharacterMovementComponent::ApplyRootMotionToVelocity -
				//   「Animation root motion is distinct from root motion sources right now and takes precedence」).
				//   반응 GA 는 위 이벤트에서 서버에서 동기로 몽타주를 이미 틀었다. 그래서 지금 루트모션 몽타주가 돌고 있으면
				//   수평은 그 클립이 주인이고 넉백을 걸지 않는다 - 걸어도 수평은 덮이고 팝(KnockbackZ)만 남아 제자리에서 튄다.
				//   종전에는 병사 피격 클립(UE5_SSH_Hurt_Light_*, 루트모션)이 매 타 이렇게 튀었고, 공격 세기 숫자는 병사에게 무의미했다.
				//   판정 함수 ACharacter::IsPlayingRootMotion 은 엔진이 덮어쓸지를 가르는 것과 같은 몽타주 인스턴스를 본다
				//   (AnimInstance.cpp::Montage_PlayInternal 이 루트모션 몽타주를 RootMotionMontageInstance 로 세운다).
				//  받는 쪽 몸집은 KnockbackTakenScale 이 수평 세기에 곱한다. 팝은 곱하지 않는다 - 팝은 «마찰을 넘기는 장치» 라 크기가 몸집과 무관하다.
				//  수직은 띄움일 때만 물리가 주인이다. 엔진은 공중에서 애니 루트모션이 Z 를 덮지 않는다
				//   (CharacterMovementComponent.cpp::ConstrainAnimRootMotionVelocity - Falling 이면 Z 유지). 그래서 넘어짐 클립(루트모션)이
				//   수평을 몰고 가는 동안에도 띄운 높이는 그대로 산다.
				const float KnockStrengthRaw = Data.EffectSpec.GetSetByCallerMagnitude(
					VBGameplayTags::Data_KnockbackStrength, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f)
					* ReadCombatAttribute(UVBCombatAttributeSet::GetKnockbackTakenScaleAttribute(), 1.0f);
				// 음수·NaN 세기는 없던 일로 한다 - 뒤로 당기거나 속도를 깨뜨린다(2026-09-30 /sw:verify).
				const float KnockStrength = FMath::IsFinite(KnockStrengthRaw) ? FMath::Max(0.0f, KnockStrengthRaw) : 0.0f;
				const float LaunchHeight = (Impact == EVBHitImpact::Launch)
					? Data.EffectSpec.GetSetByCallerMagnitude(VBGameplayTags::Data_LaunchHeight, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f)
					: 0.0f;
				ACharacter* VictimChar = Cast<ACharacter>(OwnerASC->GetAvatarActor());
				const bool bClipOwnsHorizontal = VictimChar && VictimChar->IsPlayingRootMotion();
				// 엎드린(그로기) 몸은 밀지 않는다 (2026-09-29, user 「그로기 되는 애니메이션 중에 때리면 몸이 살짝 뜬다」).
				//   넉백은 LaunchCharacter 라 무브먼트를 Falling 으로 바꾸는데, 다운 클립은 전부 루트모션이고 엔진은 루트모션 중
				//   수평 속도만 덮고 수직은 남긴다(CharacterMovementComponent.cpp::ConstrainAnimRootMotionVelocity - Falling 이면 Z 유지).
				//   그래서 옆으로는 안 밀리고 KnockbackZ(기본 120cm/s)만큼 약 7cm 위로만 튀었다. 넉백이 하는 일(간격 회복)이
				//   엎드린 몸에는 없으므로 막는다. 서 있는 경직·공격 중 루트모션에서도 같은 물리가 있으나 그쪽은 체감 보고가 없어 이번 범위 밖이다.
				//   (DEC-025: 그 «범위 밖» 이던 서 있는 루트모션은 위 bClipOwnsHorizontal 이 받는다. 이 줄은 대기 클립이 제자리일 때를 위해 남는다.
				//    «이 타격 전에» 엎드려 있었는지를 본다 - 이 타격이 방금 넘어뜨린 몸은 엎드린 몸이 아니다.)
				FVector LaunchVelocity = FVector::ZeroVector;
				bool bLaunchXYOverride = false;
				bool bLaunchZOverride  = false;
				if (VictimChar && !bVictimDead && !bDownedBeforeHit)
				{
					// 방향은 위(4단계 (0))에서 정한 PushDir 하나다 - 반응 GA 가 받은 방향과 같은 값이라야 클립과 넉백이 한 방향이 된다.
					if (KnockStrength > 0.0f && !bClipOwnsHorizontal && !PushDir.IsNearlyZero())
					{
						// 상방 성분을 실어 실제로 띄운다. Z=0 이면 다음 프레임에 바닥을 찾아 Walking 으로
						// 복귀하고 지상 마찰이 수평 속도를 즉시 지운다(KnockbackZ 주석 참조).
						const float KnockZ = Data.EffectSpec.GetSetByCallerMagnitude(
							VBGameplayTags::Data_KnockbackZ, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f);
						LaunchVelocity    = PushDir * KnockStrength + FVector(0.0f, 0.0f, KnockZ);
						bLaunchXYOverride = true;
					}

					// 띄움: 높이를 속도로 바꾼다(v = sqrt(2 g h)). 중력은 이 캐릭터의 것이다(GravityScale 반영).
					//  수직을 덮어쓴다(bZOverride) - 팝과 더하면 높이가 공격 자산 값보다 커진다.
					if (LaunchHeight > 0.0f)
					{
						const UCharacterMovementComponent* VictimMove = VictimChar->GetCharacterMovement();
						const float Gravity = VictimMove ? FMath::Abs(VictimMove->GetGravityZ()) : 0.0f;
						if (Gravity > 0.0f)
						{
							LaunchVelocity.Z = FMath::Sqrt(2.0f * Gravity * LaunchHeight);
							bLaunchZOverride = true;
						}
					}

					// 서버 전용: 적은 PC 없는 서버권위 AI → LaunchCharacter 가 CMC velocity 를 sim proxy 로 복제(Dodge 동형).
					// 이 블록 자체가 IsOwnerActorAuthoritative 가드 안이라 서버에서만 실행됨.
					if (!LaunchVelocity.IsNearlyZero())
					{
						VictimChar->LaunchCharacter(LaunchVelocity, bLaunchXYOverride, bLaunchZOverride);
					}
				}

				// 물리로 누운 몸(처형을 버텨낸 래그돌)은 넉백 대신 맞은 자리에 충격을 받는다(설계 .sw/downed-hit, user 2026-10-01).
				//   이동의 주인이 물리라 LaunchCharacter 가 할 일이 없다. 방향(PushDir)·실효 등급·몸집 배수는 서 있을 때와 같은 값이다.
				//   밀지 말지(상한)·얼마나(등급별 값)는 받는 쪽이 정한다. 그 몸에는 Downed 가 붙어 있어 위 반응 GA 들은 이미 안 켜졌다.
				float DownedImpactSpeed = 0.0f;
				IVBDownedImpactReceiver* const DownedBody = Cast<IVBDownedImpactReceiver>(OwnerASC->GetAvatarActor());
				if (DownedBody && !bVictimDead && bDownedBeforeHit && DownedBody->IsBodyDownedInPhysics())
				{
					DownedImpactSpeed = DownedBody->ReceiveDownedImpact(Data.EffectSpec.GetContext().GetHitResult(), PushDir, ResolvedImpact,
						ReadCombatAttribute(UVBCombatAttributeSet::GetKnockbackTakenScaleAttribute(), 1.0f));
				}

				// 반응 한 줄. 보내는 쪽 로그(「선언 등급」)와 짝이다 - 둘을 대조하면 «공격은 넘어뜨리려 했는데 왜 플린치인가» 가
				//  저항 때문인지(실효), 넘어짐 GA 가 없어서인지(GA 0개), 클립이 이동을 가져가서인지(이동)가 로그만으로 갈린다.
				VB_LOG(Log, "반응[%s]: 선언 %s -> 실효 %s (저항 %.1f) / 넘어짐 GA %d / 이동 %s 수평 %.0f 수직 %.0f",
				       *GetNameSafe(OwnerASC->GetAvatarActor()), VBHitImpactName(DeclaredImpact), VBHitImpactName(Impact),
				       ImpactResistance, KnockdownTaken,
				       (!VictimChar || bVictimDead) ? TEXT("없음(사망)")
				       : bDownedBeforeHit ? (DownedImpactSpeed > 0.0f ? TEXT("충격(누운 몸)") : TEXT("없음(엎드림)"))
				       : bClipOwnsHorizontal ? TEXT("클립") : (bLaunchXYOverride ? TEXT("넉백") : TEXT("없음")),
				       FVector(LaunchVelocity.X, LaunchVelocity.Y, 0.0f).Size(), LaunchVelocity.Z);

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

			// 6단계 : 처형 창 개방 요청 (DEC-019, 2026-09-11).
			//
			// 여기서 «문턱» 을 판정하지 않는다. 이 함수가 아는 사실은 「체력이 이만큼 남았다」 하나이고,
			//  「그 정도면 처형 창을 열 만한가」는 창의 설정 자산이 가진 판단이다. 문턱을 여기서 읽으면
			//  같은 숫자가 두 곳에 살게 되고, 한쪽만 고친 날 창이 안 열리거나 계속 열린다(규칙 26).
			//  그래서 비율만 실어 보내고 UVBGA_FinisherWindow::ShouldAbilityRespondToEvent 가 가른다 -
			//  엔진이 그 훅을 CanActivateAbility 보다도 먼저 부르므로(AbilitySystemComponent_Abilities.cpp
			//  ::UAbilitySystemComponent::InternalTryActivateAbility) 문턱 위의 타격은 재무장 쿨다운을 태우지 않는다.
			//
			// 사망 게이트: 위 SetHealth 가 동기적으로 State.Dead 를 붙인 뒤 여기 온다(4단계 주석과 같은 경로).
			//  시체에 창을 열면 처형 태그를 든 시체가 서 있게 된다.
			// 가드 억제를 걸지 않는 이유: 막아도 감쇠된 데미지는 들어간다. 그 한 대가 문턱을 넘길 수 있다.
			// RemainingDamage 게이트: 실드가 전부 흡수한 타격은 체력을 한 톨도 안 줄인다. 그런 프레임에도
			//  쏘면 이미 문턱 아래인 적이 «체력 손실 0» 인 타격으로 창을 다시 연다(2026-09-11 /verify 적발).
			//  DamageValue 가 아니라 RemainingDamage 를 보는 것이 요점이다 - 감쇠 뒤 실제로 체력에 닿은 양이다.
			if (OwnerASC && OwnerASC->IsOwnerActorAuthoritative() && !UVBAbilitySystemStatics::IsDead(OwnerASC)
			    && GetMaxHealth() > 0.0f && RemainingDamage > 0.0f)
			{
				FGameplayEventData WindowEvent;
				WindowEvent.EventTag       = VBGameplayTags::Event_Combat_FinisherWindow;
				WindowEvent.Instigator     = Data.EffectSpec.GetContext().GetInstigator();
				WindowEvent.Target         = OwnerASC->GetAvatarActor();
				WindowEvent.EventMagnitude = GetHealth() / GetMaxHealth();   // 남은 체력 비율. 판정은 받는 쪽이 한다
				WindowEvent.ContextHandle  = Data.EffectSpec.GetContext();

				OwnerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_FinisherWindow, &WindowEvent);
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