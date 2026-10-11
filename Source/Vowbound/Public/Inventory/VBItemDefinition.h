// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBItemDefinition.generated.h"

class UAnimMontage;
class UTexture2D;
class UStaticMesh;

/**
 * 아이템 조각의 바탕. «어떤 아이템만 갖는 성질» 을 조각으로 붙인다(사용 가능, 가격, 장비 연결...). 설계 .sw/narrative 3.1절.
 * 아이템 종류마다 클래스를 파생하지 않는다 - Lyra 의 아이템 정의 + 조각 구조와 같은 판단이다. 새 성질 = 조각 하나.
 */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBItemFragment
{
	GENERATED_BODY()

	virtual ~FVBItemFragment() = default;
};

/**
 * 쓸 수 있는 아이템(회복약·치료제). 쓰는 것 = 2단계 규칙을 그대로 부른다 - 조건이 참이면 효과를 적용한다.
 * 아이템 전용 효과 코드를 두지 않는 이유: 회복은 «GE 걸기» 효과(GE_Heal + Data.Heal), 사실을 켜는 열쇠는 «사실 바꾸기» 효과로 이미 적힌다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="사용"))
struct VOWBOUND_API FVBItemFragment_Usable : public FVBItemFragment
{
	GENERATED_BODY()

	// 전부 참일 때만 쓸 수 있다. 비면 늘 쓸 수 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item", meta=(BaseStruct="/Script/Vowbound.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	// 쓰면 차례대로 적용한다(서버). 부른 플레이어 = 쓴 사람.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item", meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> Effects;

	// 쓰면 하나 줄어드나. 꺼 두면 몇 번이고 쓰는 도구(예: 등불)다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item")
	bool bConsumeOnUse = true;

	// 쓰는 몸 동작(.sw/quickslot-input 6.1절). 비면 바로 쓴다. 있으면 UVBGA_UseItem 이 틀고, 몽타주의 표식(UVBAN_ItemUseMarker)
	//   시각에 소모·효과를 확정한다 - 그 전에 맞거나 회피·가드하면 아무것도 소모하지 않는다. 표식은 정확히 하나여야 한다(IsDataValid).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item")
	TObjectPtr<UAnimMontage> UseMontage;

	// 쓰는 동안 든 무기를 넣었다가 끝나면 다시 든다(user 2026-10-06 «거즈 치료할 때 무기는 넣어 줘야»). 두 손을 쓰는 동작에 켠다.
	//   상호작용과 같은 임시 수납(UVBWeaponStateComponent::TempSheatheForParkour)이라 수납·꺼냄 몸짓 없이 바로 숨겼다 바로 돌아온다. 몸 동작이 없으면 쓰이지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item")
	bool bSheatheWeaponDuringUse = false;
};

/**
 * 입을 수 있는 아이템(방어구·장신구). 설계 .sw/remaining-features/b1-equipment.md.
 * 효과는 GE 하나(UVBEquipmentComponent::EquipmentEffect - 능력치마다 SetByCaller 덧셈)에 이 지도의 값을 실어 건다 - 장비마다 GE 를 만들지 않는다.
 *   새 장비 = 데이터 자산 하나. 지도에 없는 능력치는 0(무변화).
 */
USTRUCT(BlueprintType, meta=(DisplayName="장비"))
struct VOWBOUND_API FVBItemFragment_Equippable : public FVBItemFragment
{
	GENERATED_BODY()

	// 들어갈 칸 종류(머리·몸·팔·장신구).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item", meta=(Categories="Equipment.Slot"))
	FGameplayTag SlotType;

	// 능력치 -> 더할 값. 예: Data.Stat.MaxHealth -> 20, Data.Stat.DamageTakenScale -> -0.05(받는 피해 5% 줄임). 스킬 트리 칸과 같은 태그(VBStatBonus).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item", meta=(Categories="Data.Stat"))
	TMap<FGameplayTag, float> Stats;
};

/**
 * 사고팔 수 있는 아이템의 기준 값. 설계 .sw/narrative 13.2절.
 * 값을 아이템에 두는 이유: 같은 물건은 어느 상점에서나 같은 값에서 시작한다. 상점은 다르게 할 때만 줄에 덮어쓴다(UVBShopDefinition).
 * 실제로 치르는 값은 이 기준에 값 규칙(경제 설정·상점)의 배율을 곱한 것이다 - 계산의 홈은 VBShop::UnitPrice 하나다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="가격"))
struct VOWBOUND_API FVBItemFragment_Price : public FVBItemFragment
{
	GENERATED_BODY()

	// 값 규칙을 걸기 전의 값(돈 아이템 개수).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item", meta=(ClampMin="1"))
	int32 BaseValue = 1;
};

/**
 * 아이템 하나의 정의(실행 중에 바뀌지 않는 데이터). 설계 .sw/narrative 3.1절·10.1절.
 *
 * 자산 ID = (VBItem, ItemId 태그 이름). 에셋 매니저가 /Game/Vowbound/Items 를 훑어 저장된 ItemId 로 이 자산을 되찾는다(DefaultGame.ini).
 * 저장은 자산 경로가 아니라 ItemId 태그로 한다 - 자산을 옮겨도 옛 슬롯이 비지 않고, 태그 이름을 바꾸면 GameplayTagRedirects 가 따라온다.
 * 목록 자산(카탈로그)을 따로 두지 않는다 - 아이템을 만들 때마다 목록에도 더해야 하는 두 번째 집이 생긴다.
 */
UCLASS(BlueprintType, Const)
class VOWBOUND_API UVBItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static FPrimaryAssetType GetItemAssetType();
	static FPrimaryAssetId MakeItemAssetId(FGameplayTag InItemId);

	// ItemId -> 정의. 에셋 매니저가 훑어 둔 목록에서 되찾는다(위 주석). 못 찾으면 nullptr.
	//   저장 줄을 되찾는 모든 곳(인벤토리·서랍장 남은 몫)이 이것 하나를 쓴다 - 되찾는 규칙이 둘이 되지 않게.
	static const UVBItemDefinition* FindById(const FGameplayTag& InItemId);

	// 저장·조건·퀘스트 목표가 이 아이템을 부르는 이름. 자산마다 하나, 겹치면 안 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item", meta=(Categories="Item"))
	FGameplayTag ItemId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item", meta=(MultiLine=true))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item")
	TSoftObjectPtr<UTexture2D> Icon;

	// 땅에 놓였을 때의 모습(AVBWorldItem 이 쓴다). 소프트 - 인벤토리에 든 아이템이 메시까지 끌어오지 않게.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item")
	TSoftObjectPtr<UStaticMesh> WorldMesh;

	// 가질 수 있는 최대 수. 0 = 무제한(돈 등). 칸 격자가 없어서 «쌓기» 는 이 한 줄의 수 상한이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item", meta=(ClampMin="0"))
	int32 MaxCount = 99;

	// 인벤토리 화면에서 버릴 수 있나(.sw/remaining-features/a1-inventory.md). 버리면 발밑에 다시 주울 수 있는 땅의 아이템으로 떨어진다.
	//   돈·진행을 막는 열쇠처럼 잃으면 안 되는 것은 끈다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item")
	bool bCanDiscard = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Item", meta=(BaseStruct="/Script/Vowbound.VBItemFragment", ExcludeBaseStruct))
	TArray<FInstancedStruct> Fragments;

	// 이 종류의 조각(없으면 nullptr). 같은 종류가 둘이면 앞의 것.
	template <typename T>
	const T* FindFragment() const
	{
		for (const FInstancedStruct& Fragment : Fragments)
		{
			if (const T* Found = Fragment.GetPtr<T>())
			{
				return Found;
			}
		}
		return nullptr;
	}

	// 넣을 수 있는 수(지금 Current 개 가졌을 때 Requested 개를 더 넣으려 하면).
	int32 ClampToMax(int32 Current, int32 Requested) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
