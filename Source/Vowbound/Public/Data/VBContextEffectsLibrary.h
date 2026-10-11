// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBContextEffectsLibrary.generated.h"

class FDataValidationContext; // IsDataValid 인자 - 헤더에서 Misc/DataValidation.h 를 끌지 않는다
class UCameraShakeBase;
class UMaterialInterface;
class UNiagaraSystem;
class USoundAttenuation;
class USoundBase;

/**
 * FVBContextEffectRow
 * 애니 속 물리 순간(발 딛음·땅 내려치기·몸 떨어짐) 하나가 내는 출력 묶음. design .sw/context-effects/design.md §3.
 *
 * 왜 한 행에 소리·파티클·데칼·흔들림을 다 두나: 한 순간의 출력이 여러 자산에 흩어지면 세기를 맞출 곳이 여럿이 된다
 *   (노티파이마다 파티클을 직접 붙이던 방식을 버린 이유, 설계 §2). 비어 있는 칸은 그냥 안 튼다.
 * 왜 하드 참조인가: 표는 캐릭터 BP 가 들고 있어 캐릭터와 함께 올라온다 - 첫 발걸음에 적재 끊김이 없다(설계 §10).
 */
USTRUCT(BlueprintType)
struct FVBContextEffectRow
{
	GENERATED_BODY()

	// 이 행이 답하는 순간. 노티파이의 태그와 정확히 같아야 맞는다(부모 태그로 번지지 않는다 - 발걸음이 착지 행을 집는 일이 없게).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	FGameplayTag EffectTag;

	// 이 행을 쓰려면 지금 상황이 가져야 할 태그 전부(예: Surface.Stone). 비면 기본 행. 여러 행이 맞으면 태그가 가장 많은(구체적인) 행.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	FGameplayTagContainer Context;

	// 소리 후보. 틀 때마다 하나를 무작위로 - 같은 발소리가 기계적으로 반복되지 않게.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	TArray<TObjectPtr<USoundBase>> Sounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	TObjectPtr<USoundAttenuation> Attenuation = nullptr;

	// 파티클. 풀에서 꺼내 쓰고 끝나면 엔진이 돌려받는다(ENCPoolMethod::AutoRelease).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	TObjectPtr<UNiagaraSystem> Niagara = nullptr;

	// 바닥에 남는 자국(금 간 땅 등). 있으면 그때만 바닥을 트레이스해 위치·법선을 얻는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	TObjectPtr<UMaterialInterface> DecalMaterial = nullptr;

	// 데칼 크기(cm, X = 깊이 투영 거리, Y/Z = 넓이). 몸 배율을 곱할 수 있다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	FVector DecalSize = FVector(40.0f, 120.0f, 120.0f);

	// 데칼이 보이는 시간(초)과 그 끝의 사라지는 시간(초).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects", meta=(ClampMin="0.0"))
	float DecalLifeSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects", meta=(ClampMin="0.0"))
	float DecalFadeSeconds = 2.0f;

	// 월드 카메라 흔들림. 안쪽 반경 안은 최대, 바깥 반경 밖은 없음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	TSubclassOf<UCameraShakeBase> CameraShake;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects", meta=(ClampMin="0.0"))
	float ShakeInnerRadius = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects", meta=(ClampMin="0.0"))
	float ShakeOuterRadius = 1500.0f;

	// 파티클 크기·데칼 크기·흔들림 반경에 몸 배율(AVBEnemyBase::ResolveStrideScale)을 곱하나. 트롤(1.6)의 발걸음이 사람보다 크게.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	bool bScaleWithBody = true;

	// 몸통은 cpp 에 둔다 - TSubclassOf<UCameraShakeBase> 의 bool 변환은 완전한 형이 필요하다. 정식 빌드는 공용 PCH 가 그 형을 대 주지만,
	//   PCH 없이 도는 -SingleFile 확인 컴파일에서는 이 헤더를 넣은 cpp 마다 C2027 로 멈췄다(10-08). 헤더가 혼자 서게 둔다.
	bool HasAnyOutput() const;
};

/**
 * UVBContextEffectsLibrary
 * «이펙트 태그 + 상황 태그 -> 출력» 표. 캐릭터의 UVBContextEffectComponent 가 여러 개(공용 + 캐릭터 전용)를 들고 순서대로 찾는다.
 *
 * 왜 캐릭터 쪽 데이터인가: 같은 «발 딛음» 이 플레이어는 가벼운 발소리, 트롤은 땅울림이다 - 피격 반응을 맞는 쪽 자산(UVBHitReactConfig)에
 *   둔 것과 같은 관례. 노티파이(애니)는 «무슨 순간인가» 만 말하고 «무엇을 트나» 는 모른다.
 * Lyra 의 ULyraContextEffectsLibrary 와 같은 모양이다. 더한 것: 데칼·카메라 흔들림·몸 배율(트롤 땅 충격의 핵심). 설계 §2·§9.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBContextEffectsLibrary : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	TArray<FVBContextEffectRow> Rows;

	// EffectTag 가 정확히 같고 Context 가 Contexts 에 전부 들어 있는 행 중 가장 구체적인 것. 없으면 null.
	const FVBContextEffectRow* FindRow(const FGameplayTag& EffectTag, const FGameplayTagContainer& Contexts) const;

#if WITH_EDITOR
	// 저작 실수를 저장 전에 잡는다: 태그 없는 행, 출력이 하나도 없는 행, 같은 (태그, 상황) 이 두 번 나오는 행.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
