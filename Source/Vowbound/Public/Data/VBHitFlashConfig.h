// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBHitFlashConfig.generated.h"

class UMaterialInterface;

/**
 * FVBHitFlashProfile
 * 히트 플래시 한 종류의 표현 묶음(머티리얼 + 길이 + 색/강도/감쇠).
 *
 * 왜 필드 나열이 아니라 struct 인가:
 *  - 데미지 타입별 분기(감염/정화 등)가 생기면 이 struct 가 TMap 의 값 타입이 된다.
 *  - 값 타입을 나중에 스칼라에서 struct 로 바꾸면 이미 직렬화된 엔트리가 전부 소실된다.
 *  - FVBHitReactSet(VBHitReactConfig.h) 가 같은 이유로 같은 형태를 쓴다 — 프로젝트 관례 일치.
 */
USTRUCT(BlueprintType)
struct FVBHitFlashProfile
{
	GENERATED_BODY()

	// 메시에 덮을 오버레이 머티리얼(MI). 오버레이는 베이스 머티리얼에 의존하지 않으므로
	// 적이 Epic 스톡 마스터를 쓰더라도 베이스를 개조할 필요가 없다.
	// 미할당이면 이 프로필은 '미등록'으로 취급된다(ResolveProfile 폴백 대상).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	TObjectPtr<UMaterialInterface> OverlayMaterial = nullptr;

	// 플래시 길이(실초). 게임시간이 아니라 실시간 기준이라 히트스톱 중에도 화면상 길이가 같다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	float Duration = 0.18f;

	// 발광 배수. 최종 강도는 이 값 x IntensityByDamage 평가값.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	float Intensity = 2.0f;

	// 플래시 색. 오늘은 순백 1종. 감염/정화 색 분기는 이 필드를 데이터로 바꾸는 것으로 끝나며 C++ 변경이 없다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	FLinearColor Color = FLinearColor::White;

	// 감쇠 지수. 1=선형, >1=초반에 확 죽는 임팩트형.
	// 커브 아틀라스 배선이 없어 커브 대신 지수를 쓴다 — 노브 하나로 감쇠 성격을 바꾼다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	float FalloffExponent = 1.0f;

	// 예약 파라미터(림 라이트). 오늘 머티리얼은 읽지 않지만 값은 이미 push 된다 —
	// 존재하지 않는 파라미터 설정은 무해한 무동작이라, 나중에 머티리얼만 고치면 C++ 변경 없이 켜진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	float FresnelStrength = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	float FresnelExponent = 2.0f;

	// 데미지량 -> 강도 배수. 키 0개면 1.0(= 배수 없음).
	// bool 플래그가 아닌 이유: 플래그는 "켰을 때 뭘 할지"를 소비 코드에 또 적게 만든다.
	// 빈 커브는 그 자체가 '적용 안 함'이라 분기가 데이터 안에서 끝난다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	FRuntimeFloatCurve IntensityByDamage;

	// 오버레이 드로우 패스의 원거리 컷(cm). 0 = 무제한.
	// 오버레이는 액터 생애 동안 유지되므로(R11), 멀리 있는 시체까지 매 프레임 래스터하지 않게 하는 노브다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	float MaxDrawDistance = 0.0f;
};

/**
 * UVBHitFlashConfig
 *
 * 피격 플래시 데이터 홈. 큐 태그 -> 표현 프로필 매핑.
 *
 * 왜 피격자 소유인가:
 *  - 플래시는 '맞은 쪽'의 몸에 나타나는 코스메틱이다. 공격자 소유인 UVBAttackConfig 에 두면
 *    "카타나로 때렸으니 맞은 쪽이 카타나 색으로 빛난다"가 되어 의미가 역전된다
 *    (UVBHitReactConfig 가 같은 이유로 같은 소유 구조다).
 *
 * 왜 DataAsset 인가:
 *  - GameplayCueNotify_Static 은 CDO 로 실행되어 인스턴스 상태를 가질 수 없다 -> 데이터는 외부 자산이어야 한다.
 *  - CLAUDE.md 데이터 주도 원칙: 색/강도/감쇠/거리 분기를 C++ if 문이 아니라 데이터로 표현한다.
 *
 * 왜 키가 GameplayTag 인가:
 *  - FGameplayCueParameters 의 태그 컨테이너는 복제되지 않지만, 실행 태그 자체는 RPC 인자로 전달되어
 *    클라에서 Parameters.OriginalTag 에 채워진다. 즉 태그는 클라에서 신뢰 가능한 유일한 분기 키다.
 *  - 자식 태그는 부모 GCN 으로 자동 라우팅되므로, 미래의 GameplayCue.Combat.HitFlash.Infection 은
 *    이 맵에 한 줄 추가하는 것으로 끝난다(C++ 변경 없음).
 *
 * 폴백 규약: 정확 일치 -> 부모 태그 순회 -> DefaultProfile.
 *   오늘 TagOverrides 는 비워 둔다 — 전원 DefaultProfile 을 타 첫 구현 동작이 100% 단일하다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBHitFlashConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 태그 미등록/미할당 시 항상 이것. 오늘의 흰색 플래시 MI 가 여기 들어간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	FVBHitFlashProfile DefaultProfile;

	// 큐 태그별 오버라이드. 미등록 = DefaultProfile 폴백.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	TMap<FGameplayTag, FVBHitFlashProfile> TagOverrides;

	// 큐 태그로 표현 프로필 해석. 반환 참조는 이 자산이 소유하므로 호출부 수명 관리가 필요 없다.
	const FVBHitFlashProfile& ResolveProfile(const FGameplayTag& CueTag) const;
};
