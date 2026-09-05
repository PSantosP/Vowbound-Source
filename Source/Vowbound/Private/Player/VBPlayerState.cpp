// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Player/VBPlayerState.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Character/VBWeaponStateTypes.h"
#include "Vowbound/Vowbound.h"

AVBPlayerState::AVBPlayerState()
{
	// ASC 생성 - PlayerState가 소유
	AbilitySystemComponent = CreateDefaultSubobject<UVBAbilitySystemComponent>(
		TEXT("AbilitySystemComponent"));
	
	// SetIsReplicated, SetReplicationMode는 UVBAbilitySystemComponent 생성자에서 처리
	
	// HealthAttributeSet 생성 -> ASC에 자동 등록
	// CreateDefaultSubObject 시 ASC가 자동으로 감지
	HealthAttributeSet = CreateDefaultSubobject<UVBHealthAttributeSet>(
		TEXT("HealthAttributeSet"));
	
	CombatAttributeSet = CreateDefaultSubobject<UVBCombatAttributeSet>(
		TEXT("CombatAttributeSet"));
	
	ReputationAttributeSet = CreateDefaultSubobject<UVBReputationAttributeSet>(
		TEXT("ReputationAttributeSet"));

	// 저장 대상은 플레이어가 실제로 소유한 값뿐이다. 상한(Max*)은 여기 없다 - GE 가 소유한다.
	// 왜(FIND-078): Max* 를 주는 것은 GE_InitStats_Player 의 OVERRIDE 하나뿐인데, 세이브가 그 값을
	//   복사해 두고 로드 때 GE 보다 나중에 되돌렸다. 결과로 GE 를 튜닝해도 기존 세이브는 옛 값을 유지했고,
	//   "GE 를 고쳤는데 안 변한다" 가 됐다. 상한의 홈은 하나여야 하고 그 홈은 GE 다.
	// 업그레이드로 상한이 오르는 날에는 "GE 가 정한 기본 + 세이브가 가진 증가분" 형태가 맞다.
	//   지금 그 채널을 미리 만들지 않는 이유는 상한을 올리는 경로가 게임에 아직 없기 때문이다(전수 확인).
	//   구 세이브의 MaxHealth/MaxShield 키는 이 목록에 없으므로 그냥 읽히지 않는다 - 별도 마이그레이션 불요.
	SavedAttributes = {
		UVBHealthAttributeSet::GetHealthAttribute(),
		UVBHealthAttributeSet::GetShieldAttribute(),
		UVBReputationAttributeSet::GetReputationAttribute(),
	};

	// 상한이 GE 소유가 됐으므로 복원값이 현재 상한을 넘을 수 있다(디자이너가 상한을 내린 경우).
	// Reputation 은 상한이 없어 넣지 않는다 - 없으면 자르지 않는다.
	SavedAttributeCaps = {
		{ UVBHealthAttributeSet::GetHealthAttribute(), UVBHealthAttributeSet::GetMaxHealthAttribute() },
		{ UVBHealthAttributeSet::GetShieldAttribute(), UVBHealthAttributeSet::GetMaxShieldAttribute() },
	};
}

FName AVBPlayerState::MakeAttributeSaveKey(const FGameplayAttribute& Attribute)
{
	// FGameplayAttribute::GetName() 은 AttributeName(프로퍼티 이름) 을 그대로 돌려준다.
	// 기존 세이브의 키("Health"/"MaxHealth"/"Shield")와 문자 그대로 같아 하위호환이 유지된다.
	return FName(*Attribute.GetName());
}

UAbilitySystemComponent* AVBPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

UVBAbilitySystemComponent* AVBPlayerState::GetVBAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

const UVBHealthAttributeSet* AVBPlayerState::GetVBHealthAttributeSet() const
{
	return HealthAttributeSet;
}

const UVBCombatAttributeSet* AVBPlayerState::GetVBCombatAttributeSet() const
{
	return CombatAttributeSet;
}

bool AVBPlayerState::AreStartupAbilitiesGranted() const
{
	return bStartupAbilitiesGranted;
}

void AVBPlayerState::MarkStartupAbilitiesGranted()
{
	 bStartupAbilitiesGranted = true;
}

bool AVBPlayerState::AreStartupEffectsApplied() const
{
	return bStartupEffectsApplied;
}

void AVBPlayerState::MarkStartupEffectsApplied()
{
	bStartupEffectsApplied = true;
}

void AVBPlayerState::SnapshotBuild(FVBPlayerBuildSaveData& Out) const
{
	// 저장 대상은 SavedAttributes 하나다(생성자에 나열). ApplyBuild 가 같은 배열을 돌아 목록이 갈리지 않는다.
	// base 값을 저장한다. ApplyBuild 가 Set*(=base 쓰기)로 복원하므로 반드시 대칭이어야 한다(FIND-047).
	// 왜: Get*() 는 현재값(base + 활성 GE 모디파이어)이다. 버프 GE 가 걸린 채 저장하면 그 일시적 가산분이
	//     로드 시 영구 base 로 구워지고, 저장/로드를 반복할수록 상향 드리프트한다.
	//     우리는 GE 스택을 저장/복원하지 않으므로 "버프 없는 소지값"이 유일하게 재현 가능한 진실이다.
	// 부작용(의도): 버프 중 저장 → 로드 시 버프분은 사라진다. 드리프트보다 이쪽이 옳다.
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ensureMsgf(ASC, TEXT("SnapshotBuild: ASC 없음")))
	{
		// Out 을 건드리지 않는다 = 직전 스냅샷 보존. 빈 맵을 쓰면 다음 저장이 디스크의 멀쩡한 값을 지운다.
		// 현재값 폴백은 두지 않는다 - 위 base/base 대칭 계약을 스스로 위반하는 경로였다.
		VB_LOG(Error, "SnapshotBuild: ASC 부재 - 어트리뷰트 저장 생략(직전 값 유지)");
		return;
	}

	TMap<FName, float> Values;
	Values.Reserve(SavedAttributes.Num());
	for (const FGameplayAttribute& Attribute : SavedAttributes)
	{
		if (!ensureMsgf(Attribute.IsValid() && ASC->HasAttributeSetForAttribute(Attribute),
		                TEXT("SnapshotBuild: 미등록 어트리뷰트 %s"), *Attribute.GetName()))
		{
			continue;
		}
		Values.Add(MakeAttributeSaveKey(Attribute), ASC->GetNumericAttributeBase(Attribute));
	}
	Out.AttributeValues = MoveTemp(Values);

	// 현재 무기 → UEnum 이름 문자열(append-only 안정 식별자). None 이면 빈 값.
	if (const AVBCharacter* C = Cast<AVBCharacter>(GetPawn()))
	{
		if (const UVBWeaponStateComponent* W = C->GetWeaponStateComponent())
		{
			const EVBWeaponType T = W->GetResummonWeaponType();
			if (T != EVBWeaponType::None)
			{
				Out.EquippedWeaponId = FName(*StaticEnum<EVBWeaponType>()->GetNameStringByValue((int64)T));
			}
		}
	}
}

float AVBPlayerState::ClampToCap(const UAbilitySystemComponent& ASC, const FGameplayAttribute& Attribute, float Value) const
{
	const FGameplayAttribute* Cap = SavedAttributeCaps.Find(Attribute);
	if (!Cap || !Cap->IsValid() || !ASC.HasAttributeSetForAttribute(*Cap))
	{
		return Value;
	}

	const float Ceiling = ASC.GetNumericAttribute(*Cap);
	if (Value > Ceiling)
	{
		// 조용히 자르지 않는다. 상한을 내린 결과인지 세이브가 깨진 것인지는 눈으로 봐야 구분된다.
		VB_LOG(Log, "ApplyBuild: %s 저장값 %.1f 이 현재 상한 %.1f 초과 - 상한으로 자름",
		       *Attribute.GetName(), Value, Ceiling);
	}
	return FMath::Clamp(Value, 0.f, Ceiling);
}

void AVBPlayerState::ApplyBuild(const FVBPlayerBuildSaveData& In)
{
	// GAS 로드 정석: base 값을 직접 세팅(SetNumericAttributeBase — ATTRIBUTE_ACCESSORS 의 Set* 와 동일 정의).
	// GE 스택/모디파이어를 건드리지 않고 저장된 최종값을 재현한다. 저장과 같은 배열을 같은 순서로 돌아
	// Max* 를 먼저 세팅한다 - Health/Shield 의 PreAttributeChange 클램프가 올바른 상한을 봐야 한다.
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ensureMsgf(ASC, TEXT("ApplyBuild: ASC 없음")))
	{
		VB_LOG(Error, "ApplyBuild: ASC 부재 - 어트리뷰트 복원 생략");
	}
	else
	{
		for (const FGameplayAttribute& Attribute : SavedAttributes)
		{
			if (!Attribute.IsValid() || !ASC->HasAttributeSetForAttribute(Attribute))
			{
				continue;
			}
			if (const float* Value = In.AttributeValues.Find(MakeAttributeSaveKey(Attribute)))
			{
				ASC->SetNumericAttributeBase(Attribute, ClampToCap(*ASC, Attribute, *Value));
			}
			// 키 부재 = 그 어트리뷰트가 없던 시절의 구 세이브. 초기화 GE 값을 유지한다(하위호환 경로).
		}
	}

	// 무기 복원: FName → enum → 재소환(서버 권위 — GameMode 에서 호출되므로 정합).
	if (!In.EquippedWeaponId.IsNone())
	{
		const int64 Val = StaticEnum<EVBWeaponType>()->GetValueByNameString(In.EquippedWeaponId.ToString());
		if (Val != INDEX_NONE && (EVBWeaponType)Val != EVBWeaponType::None)
		{
			if (AVBCharacter* C = Cast<AVBCharacter>(GetPawn()))
			{
				if (UVBWeaponStateComponent* W = C->GetWeaponStateComponent())
				{
					W->EnsureWeaponSummoned((EVBWeaponType)Val);
				}
			}
		}
	}
	VB_LOG(Log, "AVBPlayerState::ApplyBuild — 세이브 빌드 적용 완료(무기=%s)", *In.EquippedWeaponId.ToString());
}
