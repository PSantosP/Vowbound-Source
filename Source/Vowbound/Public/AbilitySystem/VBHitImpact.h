// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VBHitImpact.generated.h"

/**
 * 피격 반응 등급 (DEC-025, design .sw/hit-reaction-tiers/design.md §2).
 *
 * 공격이 «얼마나 세게 때리나» 를 선언하고, 받는 쪽 저항(UVBCombatAttributeSet::ImpactResistance)만큼 깎인 «실효» 등급으로
 *   반응 콘텐츠를 고른다. 순서가 곧 세기다 - 한 단계 깎이면 바로 아래 등급이 된다.
 *   띄움이 한 단계 깎이면 넘어뜨림이다(덩치 큰 적은 뜨지 않고 쓰러진다).
 *
 * «답»(Attack.Answer.* - 막기·패링·회피 중 무엇이 통하나)과 다른 축이다. 섞지 않는다.
 *
 * 왜 공격 자산 헤더가 아니라 여기인가: 공격 쪽(UVBAttackConfig·스윙 프로필)과 받는 쪽(허브·반응 GA)이 같이 쓴다.
 *   한쪽 자산 헤더에 두면 반대쪽이 그 자산을 끌어온다.
 *
 * 서수 값은 SetByCaller(Data.HitImpact)로 float 운반된다 - 순서를 바꾸면 저장된 자산 값의 뜻이 바뀐다. 끝에만 덧붙인다.
 */
UENUM(BlueprintType)
enum class EVBHitImpact : uint8
{
	// 반응 없음(발광·넉백만). 저항이 등급을 다 깎았을 때의 결과이고, 공격 자산에 직접 고르는 값은 아니다.
	None,
	// 플린치. 현행의 모든 피격이 이것이다 - 공격 자산 기본값.
	Light,
	// 무거운 피격(크게 비틀거림). 콘텐츠 = UVBHitReactConfig::HeavySet.
	Heavy,
	// 넘어짐 -> 누운 대기 -> 기상. 콘텐츠 = UVBHitReactConfig::KnockdownSet.
	Knockdown,
	// 공중에 뜸 -> 착지. 넘어짐 계열이라 받는 쪽이 못 받으면 넘어뜨림으로 내려간다.
	Launch
};

// 선언 등급을 받는 쪽 저항만큼 깎는다. 저항은 속성(float)이라 반올림한 정수 단계로 읽고, 0 아래는 0 으로 본다.
// None 은 깎아도 None 이다.
VOWBOUND_API EVBHitImpact VBResolveHitImpact(EVBHitImpact Declared, float Resistance);

// SetByCaller 서수 -> 등급. 범위 밖(0 = 실리지 않음 포함)이면 Light.
//   Light 로 떨어지는 이유: 등급을 안 싣는 피해(출혈 폭발·비근접)가 종전처럼 플린치를 내야 회귀가 없다.
//   None 을 공격이 선언하는 경로는 없다 - None 은 저항이 깎은 결과로만 생긴다.
VOWBOUND_API EVBHitImpact VBHitImpactFromMagnitude(float Magnitude);

// 등급 -> Combat.Impact.* 태그. None 도 자기 태그가 있다(태그 없음 = 옛 경로 = Light 와 갈라야 한다).
VOWBOUND_API FGameplayTag VBHitImpactToTag(EVBHitImpact Impact);

// 이벤트 태그 컨테이너에서 실효 등급을 읽는다. Combat.Impact.* 가 하나도 없으면 Light(패링 되받기 같은 옛 경로의 현행 보존).
VOWBOUND_API EVBHitImpact VBHitImpactFromTags(const FGameplayTagContainer& Tags);

// 로그용 이름.
VOWBOUND_API const TCHAR* VBHitImpactName(EVBHitImpact Impact);

struct FGameplayEventData;

// «밀린 방향»(월드 요, 도)을 반응 이벤트에 싣고 읽는다(2026-09-29, user 「넉백은 내가 공격하는 정면 방향으로」).
// 허브가 넉백에 쓰는 방향(공격자가 스펙에 실은 판정 방향)과 같은 값을 반응 GA 가 받는다. 넘어짐은 피격자를 그 반대로 돌려 세우고
//   (넘어짐 클립이 한 방향뿐), 피격 반응은 몸은 두고 클립의 이동만 그 방향으로 돌린다 - 넉백·이동·넘어지는 쪽이 한 방향이 된다.
//   근접 판정이 방향을 실은 타격에만 실린다(출혈 폭발 같은 비근접 피해에는 없다).
// 왜 EventMagnitude 가 아닌가: Hit 이벤트의 그 칸은 피해량이고 패링 되받기는 다른 뜻으로 쓴다. 한 칸에 두 뜻을 겹치지 않는다.
//   TargetData(위치 정보 한 개, 회전만 쓴다)는 ServerInitiated 활성화 때 이벤트와 함께 소유 클라로도 넘어간다.
VOWBOUND_API void VBSetPushYaw(FGameplayEventData& Event, float Yaw);
// 실려 있으면 true 와 요. 없으면(비근접 피해·패링 되받기 등 옛 경로) false - 호출자는 방향에 기대는 처리를 건너뛴다.
VOWBOUND_API bool VBGetPushYaw(const FGameplayEventData& Event, float& OutYaw);
