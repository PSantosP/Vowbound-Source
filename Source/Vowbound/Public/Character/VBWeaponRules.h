// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBWeaponRules.generated.h"

/**
 * 가진 무기를 읽고 쓰는 규칙. 설계 .sw/prologue-opening 3.1.
 *
 * 왜 Character/ 인가: 가진 무기의 주인은 플레이어 캐릭터의 UVBWeaponStateComponent 다. 규칙 핵심(VBRuleTypes)은 무기를 모른다 -
 *   보상표(Inventory/Loot)·상점(Inventory/Shop)·GE(Player)처럼 갈래마다 자기 효과를 자기 곳에 둔다.
 * 대상은 부른 플레이어(Context.Instigator)의 폰이다.
 *
 * 줍는 액터는 이 효과를 쓰지 않는다 - 결과(새로 얻음/이미 가짐/실패)를 보고 소진·획득 효과를 정해야 해서 컴포넌트를 직접 부른다(설계 8.4).
 *   이 효과는 결과가 필요 없는 자리(대화 선택지·퀘스트 보상)용이다.
 */

/** 부른 플레이어가 그 무기를 가졌나(bNegate 면 안 가졌나). */
USTRUCT(BlueprintType, meta=(DisplayName="무기 가짐"))
struct VOWBOUND_API FVBCondition_HasWeapon : public FVBCondition
{
	GENERATED_BODY()

	// None 이면 «아무 무기라도».
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	EVBWeaponType Weapon = EVBWeaponType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	bool bNegate = false;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

/** 부른 플레이어에게 무기를 준다. 이미 가졌으면 아무 일도 없다. */
USTRUCT(BlueprintType, meta=(DisplayName="무기 주기"))
struct VOWBOUND_API FVBEffect_GrantWeapon : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	EVBWeaponType Weapon = EVBWeaponType::Katana;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
