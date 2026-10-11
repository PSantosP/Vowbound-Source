// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Vowbound/Vowbound.h"

UVBCombatAttributeSet::UVBCombatAttributeSet()
{
	InitStamina(0.0f);
	InitMaxStamina(0.0f);
	InitAttackPower(0.0f);
	// Poise 기본값 0 = "이 액터는 경직 시스템을 쓰지 않는다". 쓰는 액터는 GE_InitStats 가 MaxPoise 를 채운다.
	// 이 기본값이 플레이어 회귀를 막는 축이다 - 플레이어는 MaxPoise 를 저작하지 않으므로 영원히 0 이고,
	//   0 이면 파이프라인이 통째로 건너뛰어진다.
	InitPoise(0.0f);
	InitMaxPoise(0.0f);
	InitIncomingPoiseDamage(0.0f);
	// 1.0 = 배수 없음. 다운 취약 GE 가 잠시 올리고, 끝나면 GE 제거로 돌아온다.
	InitDamageTakenScale(1.0f);
	// 0 = 등급 그대로, 1.0 = 넉백 그대로. 이 두 기본값이 저작 전 모든 액터(플레이어 포함)의 현행을 지킨다(DEC-025).
	InitImpactResistance(0.0f);
	InitKnockbackTakenScale(1.0f);
}

void UVBCombatAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (Attribute == GetStaminaAttribute())
	{
		ClampAttributeOnChange(Attribute, NewValue, 0.0f, GetMaxStamina());
	}
	else if (Attribute == GetMaxStaminaAttribute())
	{
		CachedMaxStamina = GetMaxStamina();
		NewValue         = FMath::Max(1.0f, NewValue);
	}
	else if (Attribute == GetAttackPowerAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}
	else if (Attribute == GetPoiseAttribute())
	{
		ClampAttributeOnChange(Attribute, NewValue, 0.0f, GetMaxPoise());
	}
	else if (Attribute == GetImpactResistanceAttribute() || Attribute == GetKnockbackTakenScaleAttribute())
	{
		// 음수 저항은 등급을 올리고(가벼운 타격이 넘어뜨림), 음수 넉백 배수는 방향을 뒤집는다(공격자 쪽으로 끌려옴).
		//   둘 다 뜻이 없는 값이라 0 에서 자른다. 위는 열어 둔다 - 크기는 GE 자산이 정한다.
		NewValue = FMath::Max(0.0f, NewValue);
	}
	else if (Attribute == GetDamageTakenScaleAttribute())
	{
		// 음수 배수는 피해를 회복으로 뒤집는다 - 0 아래는 자르고, 위는 열어 둔다(배수 크기는 GE 자산이 정한다).
		NewValue = FMath::Max(0.0f, NewValue);
	}
	else if (Attribute == GetMaxPoiseAttribute())
	{
		// MaxStamina/MaxHealth 와 달리 1 로 올려 치지 않는다. 0 은 "경직 미사용"이라는 유효한 값이고,
		// 1 로 올리면 경직을 안 쓰는 액터(플레이어)가 한 대에 무너진다.
		NewValue = FMath::Max(0.0f, NewValue);
	}
}

void UVBCombatAttributeSet::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, Stamina,
	                               COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, MaxStamina,
	                               COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, AttackPower,
	                               COND_None, REPNOTIFY_Always);
	// IncomingPoiseDamage 는 meta attribute 라 복제하지 않는다 - 서버에서 계산되고 즉시 0 으로 비워진다.
	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, Poise,
	                               COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, MaxPoise,
	                               COND_None, REPNOTIFY_Always);
}

void UVBCombatAttributeSet::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// 받는 등급 저항·넉백 배수·받는 피해 배수는 «기본값» 까지 0 에서 자른다(2026-09-30 /sw:verify, 피해 배수는 10-01 /verify). PreAttributeChange 는 현재값만 자르는데,
	//   즉시 GE(Override·Add)는 기본값에 쓰고 뒤의 GE 는 그 음수 기본값 위에 쌓인다. 현재값이 아니라 기본값을 읽어 고친다 -
	//   현재값으로 기본값을 덮으면 걸려 있는 지속 GE 의 몫이 두 번 들어간다.
	if (Data.EvaluatedData.Attribute == GetImpactResistanceAttribute() || Data.EvaluatedData.Attribute == GetKnockbackTakenScaleAttribute()
		|| Data.EvaluatedData.Attribute == GetDamageTakenScaleAttribute())
	{
		if (UAbilitySystemComponent* OwnerASC = GetOwningAbilitySystemComponent())
		{
			if (OwnerASC->GetNumericAttributeBase(Data.EvaluatedData.Attribute) < 0.0f)
			{
				OwnerASC->SetNumericAttributeBase(Data.EvaluatedData.Attribute, 0.0f);
			}
		}
		return;
	}

	if (Data.EvaluatedData.Attribute == GetStaminaAttribute())
	{
		SetStamina(FMath::Clamp(GetStamina(), 0.0f, GetMaxStamina()));
	}
	else if (Data.EvaluatedData.Attribute == GetMaxStaminaAttribute())
	{
		if (CachedMaxStamina > 0.0f)
		{
			const float Ratio = GetStamina() / CachedMaxStamina;
			SetStamina(FMath::Clamp(Ratio * GetMaxStamina(), 0.0f, GetMaxStamina()));
		}
	}
	else if (Data.EvaluatedData.Attribute == GetAttackPowerAttribute())
	{
		SetAttackPower(FMath::Max(0.0f, GetAttackPower()));
	}
	else if (Data.EvaluatedData.Attribute == GetIncomingPoiseDamageAttribute())
	{
		// Meta Attribute 파이프라인. IncomingDamage(UVBHealthAttributeSet)와 정확히 같은 형태다 -
		// 검증된 경로를 흉내내는 것이 새 경로를 발명하는 것보다 안전하다.
		const float PoiseDamageValue = GetIncomingPoiseDamage();
		SetIncomingPoiseDamage(0.0f); // 다음 계산을 위해 즉시 비운다

		if (PoiseDamageValue <= 0.0f)
		{
			return;
		}

		UAbilitySystemComponent* OwnerASC = GetOwningAbilitySystemComponent();
		if (!OwnerASC || !OwnerASC->IsOwnerActorAuthoritative())
		{
			return; // 경직 판정은 서버 권위 전용
		}

		if (UVBAbilitySystemStatics::IsDead(OwnerASC))
		{
			return; // 시체는 비틀거리지 않는다. GE_PhysicalDamage 의 모디파이어 순서가 이 게이트를 성립시킨다
			        // (IncomingDamage 가 먼저 돌아 State.Dead 가 붙은 뒤 여기 온다).
		}

		if (GetMaxPoise() <= 0.0f)
		{
			// MaxPoise <= 0 = "이 액터는 경직 시스템을 쓰지 않는다". 0 을 "첫 타에 즉시 붕괴"로 읽으면
			// MaxPoise 를 저작하지 않는 플레이어가 매 피격마다 경직된다 - 강등이 아니라 회귀다.
			// 부재는 값이 아니다. 이 한 줄이 플레이어 회귀를 막는 유일한 게이트다.
			return;
		}

		SetPoise(FMath::Max(0.0f, GetPoise() - PoiseDamageValue));
		if (GetPoise() > 0.0f)
		{
			return;
		}

		// 슈퍼아머(State.Combat.SuperArmor) 중에는 파탄을 미룬다. Poise 는 0 에 둔 채 이벤트를 쏘지 않는다 -
		//   슈퍼아머가 걷힌 뒤 첫 타가 0 - 데미지 = 0 으로 다시 여기 와서 그때 경직이 터진다.
		// 왜: 슈퍼아머 반격(규칙 B)은 «맞아도 안 끊기는 것이 표시된 예외» 인데, 파탄 경직은 UVBGA_EnemyMeleeAttack 의
		//   Staggered 감시를 통해 그 공격을 취소했다(2026-09-06 PIE: 예고 0.41초 만에 «경직 -> 공격 취소»).
		//   반격은 «3대 맞고» 시작하므로 연타하는 플레이어는 예고 안에 거의 늘 파탄에 닿는다 - 미루지 않으면
		//   규칙 B 가 연타에 언제나 진다. 미루면 슈퍼아머로 버티고 난 뒤 첫 타가 그로기다(견딘 쪽의 보상).
		// 왜 UVBGA_Stagger 의 ActivationBlockedTags 가 아닌가: 거기서 막으면 이 아래 리필이 먼저 돌아 파탄이
		//   흔적 없이 삼켜진다. 그 GA 는 «슈퍼아머 ≠ 기절 게이지» 를 일부러 지키고 있다 - 여기서는 횟수를 안 바꾸고 시점만 미룬다.
		if (OwnerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_SuperArmor))
		{
			VB_LOG(Log, "Poise 파탄 보류: %s 슈퍼아머 중 - 슈퍼아머가 걷힌 뒤 첫 타에 경직",
			       *GetNameSafe(OwnerASC->GetAvatarActor()));
			return;
		}

		// 즉시 리필. 리필하지 않으면 0 인 상태가 유지되어 다음 타격마다 연속으로 경직이 터진다.
		SetPoise(GetMaxPoise());

		FGameplayEventData PoiseBrokenEvent;
		PoiseBrokenEvent.EventTag      = VBGameplayTags::Event_Combat_PoiseBroken;
		PoiseBrokenEvent.Instigator    = Data.EffectSpec.GetContext().GetInstigator();
		PoiseBrokenEvent.Target        = OwnerASC->GetAvatarActor();
		PoiseBrokenEvent.ContextHandle = Data.EffectSpec.GetContext();
		OwnerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_PoiseBroken, &PoiseBrokenEvent);
	}
}

void UVBCombatAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, Stamina, OldStamina);
}

void UVBCombatAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, MaxStamina, OldMaxStamina);
}

void UVBCombatAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, AttackPower, OldAttackPower);
}

void UVBCombatAttributeSet::OnRep_Poise(const FGameplayAttributeData& OldPoise)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, Poise, OldPoise);
}

void UVBCombatAttributeSet::OnRep_MaxPoise(const FGameplayAttributeData& OldMaxPoise)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, MaxPoise, OldMaxPoise);
}
