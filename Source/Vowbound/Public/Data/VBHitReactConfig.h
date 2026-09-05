// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Character/VBWeaponStateTypes.h"
#include "VBHitReactConfig.generated.h"

class UAnimMontage;

/**
 * FVBHitReactSet
 * 한 스탠스의 히트리액트 콘텐츠 묶음.
 *
 * 왜 1필드 struct 인가 (지금은 몽타주 하나뿐인데):
 *  - 방향별(Front/Back/Left/Right) 히트리액트 콘텐츠가 생기면 이 struct 에 필드를 추가하는 것이 확장 지점이다.
 *  - 맵의 값 타입을 TObjectPtr<UAnimMontage> 로 두면 방향 추가 시 TMap 값 타입이 바뀌어
 *    이미 직렬화된 스탠스 엔트리가 전부 소실된다.
 *  - FVBWeaponMoveset(VBWeaponMovesetConfig.h) 가 같은 이유로 같은 형태를 쓴다 — 프로젝트 관례 일치.
 */
USTRUCT(BlueprintType)
struct FVBHitReactSet
{
	GENERATED_BODY()

	// 무방향 기본 히트리액트. 오늘 유일한 콘텐츠(AM_HitReaction)가 여기 들어간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> Montage = nullptr;
};

/**
 * UVBHitReactConfig
 *
 * 피격 반응(히트리액트) 데이터 홈. 피격자 스탠스 → 히트리액트 묶음 매핑.
 *
 * 왜 피격자 소유인가:
 *  - 히트리액트는 "맞은 쪽"의 반응이다. 공격자 소유인 UVBAttackConfig 에 두면 의미가 역전된다
 *    (카타나로 때렸다 → 맞은 쪽이 카타나 플린치? 아니다. 맞은 쪽이 카타나 자세였으면 카타나 플린치다).
 *  - 그래서 이 자산은 재생 주체(히트리액트 GA / GCN 베이스)가 EditDefaultsOnly 로 참조한다.
 *
 * 왜 DataAsset 인가:
 *  - GameplayCueNotify_Static 은 CDO 로 실행되어 인스턴스 상태를 가질 수 없다 → 데이터는 외부 자산이어야 한다.
 *  - CLAUDE.md 데이터 주도 원칙: 스탠스별 분기를 C++ if 문이 아니라 데이터로 표현한다.
 *
 * 폴백 규약: 미등록 스탠스 / 미할당 몽타주는 DefaultSet 으로 폴백한다.
 *   (UVBWeaponMovesetConfig::GetAttackConfig 의 "미등록이면 폴백" 관례와 동일 — 회귀 안전)
 *   적(AVBEnemyBase)은 UVBWeaponStateComponent 를 갖지 않으므로 항상 None → DefaultSet 이 쓰인다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBHitReactConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 스탠스 미등록/미할당 시 항상 이것. 오늘의 AM_HitReaction 이 여기 들어간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	FVBHitReactSet DefaultSet;

	// 피격자의 현재 스탠스별 오버라이드. 미등록 = DefaultSet 폴백.
	// 오늘은 비워둔다 — 스탠스별 콘텐츠가 아직 없어 전원 DefaultSet 을 타며 현행 동작이 100% 보존된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TMap<EVBWeaponType, FVBHitReactSet> StanceOverrides;

	// 경직(Poise break) 반응 콘텐츠. 히트리액트보다 무겁고 길다.
	// 왜 새 자산(UVBStaggerConfig)을 만들지 않는가: 둘 다 "맞은 쪽의 반응 콘텐츠"라는 같은 관심사이고,
	//   자산을 가르면 적 BP 가 반응 자산을 두 개 꽂아야 해서 한쪽만 빠지는 사고가 생긴다.
	// 스탠스별 오버라이드를 두지 않는 이유: 경직 콘텐츠는 아직 하나뿐이고, 필요해지면 히트리액트와
	//   같은 형태(TMap<EVBWeaponType, FVBHitReactSet>)로 확장한다 - 그때도 홈은 여기 하나다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	FVBHitReactSet StaggerSet;

	// 히트리액트 몽타주 재생 속도. 경직도 이 값을 쓴다 -
	// 근거 없이 노브를 늘리면 두 값이 서로 다른 방향으로 흘러간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	float MontagePlayRate = 1.0f;

	// 피격자 스탠스로 히트리액트 몽타주 해석. 미등록/미할당이면 DefaultSet.Montage,
	// 그것도 없으면 nullptr → 호출부가 재생을 스킵한다(크래시 대신 무동작).
	UAnimMontage* ResolveMontage(EVBWeaponType Stance) const;

	// 경직 몽타주 해석. 미할당이면 nullptr 를 돌려준다 - 히트리액트 몽타주로 폴백하지 않는다.
	//   폴백하면 "경직 콘텐츠 없음"이 "가벼운 플린치"로 보여 미저작이 눈에 안 띈다.
	// 형제 ResolveMontage 와 달리 스탠스 인자를 받지 않는다: StaggerSet 은 단일 세트라 스탠스가
	//   결과에 영향을 줄 수 없다. 못 쓰는 인자를 받으면 디자이너가 스탠스별 경직을 저작하고
	//   아무 일도 안 일어나는 것을 에러 없이 겪는다 - 시그니처는 할 수 있는 것만 말해야 한다.
	//   스탠스별 경직 콘텐츠가 생기면 그때 StanceOverrides 와 인자를 함께 추가한다(호출부 1곳).
	UAnimMontage* ResolveStaggerMontage() const;
};
