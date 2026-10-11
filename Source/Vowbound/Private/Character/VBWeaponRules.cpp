// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Character/VBWeaponRules.h"

#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/PlayerState.h"
#include "Vowbound/Vowbound.h"

namespace
{
	UVBWeaponStateComponent* GetInstigatorWeapons(const FVBRuleContext& Context)
	{
		const AVBCharacter* Character = Context.Instigator ? Cast<AVBCharacter>(Context.Instigator->GetPawn()) : nullptr;
		return Character ? Character->GetWeaponStateComponent() : nullptr;
	}
}

bool FVBCondition_HasWeapon::Evaluate(const FVBRuleContext& Context) const
{
	const UVBWeaponStateComponent* Weapons = GetInstigatorWeapons(Context);
	if (!Weapons)
	{
		// 읽을 수 없으면 거짓이다 - bNegate 여도 참으로 뒤집지 않는다(모르는 것을 «안 가졌다» 로 읽으면 빈손 대사가 엉뚱하게 나온다).
		VB_LOG(Warning, "조건(무기 가짐): 부른 플레이어의 무기 상태를 읽을 수 없다 - 거짓으로 친다");
		return false;
	}
	const bool bHas = (Weapon == EVBWeaponType::None) ? !Weapons->GetOwnedWeapons().IsEmpty() : Weapons->IsWeaponOwned(Weapon);
	return bNegate ? !bHas : bHas;
}

void FVBEffect_GrantWeapon::Apply(const FVBRuleContext& Context) const
{
	UVBWeaponStateComponent* Weapons = GetInstigatorWeapons(Context);
	if (!Weapons)
	{
		VB_LOG(Warning, "효과(무기 주기): 부른 플레이어의 무기 상태가 없다 - 주지 못함");
		return;
	}
	Weapons->GrantWeapon(Weapon);
}
