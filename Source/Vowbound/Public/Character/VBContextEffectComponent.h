// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "VBContextEffectComponent.generated.h"

class USkeletalMeshComponent;
class UVBContextEffectsLibrary;
struct FVBContextEffectRow;

/**
 * UVBContextEffectComponent
 * 애니 속 물리 순간(발 딛음·땅 내려치기·몸 떨어짐)을 받아 캐릭터의 표에서 출력을 골라 트는 쪽. design .sw/context-effects/design.md §3·§10.
 *
 * 흐름: 노티파이(UVBAnimNotify_ContextEffect, 옛 UVBGASPAnimNotify_FoleyEvent 도) -> PlayEffect(태그, 뼈) -> 표에서 한 행 -> PlayRow 가
 *   소리(뼈에 붙임)·파티클(풀)·데칼(바닥 트레이스)·카메라 흔들림을 튼다. 몸 배율(AVBEnemyBase::ResolveStrideScale)을 행이 원하면 곱한다.
 * 경계(설계 §1): 휘두름 궤적·휘두름 소리는 UVBSwingTrailComponent, 맞았을 때 불꽃은 게임플레이 큐(GCN_DamagePhysical)가 맡는다.
 *   이 컴포넌트는 «애니가 스스로 일으키는 순간» 만 맡는다 - 모든 머신이 애니를 평가하며 같은 노티파이를 받으므로 네트워크를 안 탄다.
 * 옛 GASP Foley 를 흡수했다(설계 §10): Foley 노티파이(UVBGASPAnimNotify_FoleyEvent)도 이것만 부른다. 옛 UVBGASPFoleyComponent 는 2026-09-30 에 뗐다.
 */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBContextEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBContextEffectComponent();

	// 찾는 순서. 앞 표에 행이 있으면 뒤는 안 본다 - 캐릭터 전용 표(예: 트롤)를 앞에, 공용 표를 뒤에 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	TArray<TObjectPtr<UVBContextEffectsLibrary>> Libraries;

	// 지금 상황 태그. 비어 있으면 표의 기본 행(상황 없음)이 맞는다. 바닥 재질 태그는 물리 표면이 정의되는 날 여기에 더한다
	//   - 지금 프로젝트에 물리 표면이 0개라 발걸음마다 바닥을 트레이스해도 얻는 것이 없다(설계 §9 d).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Effects")
	FGameplayTagContainer Contexts;

	// 데칼이 있는 행만 이 채널로 바닥을 찾는다(뼈 위치에서 아래로 이 거리).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Effects", meta=(ClampMin="0.0"))
	float GroundTraceDistance = 200.0f;

	// 순간 하나를 튼다. 행을 찾아 틀었으면 true(행이 없으면 태그당 한 번 경고하고 false).
	// Mesh 가 null 이면 주인의 첫 스켈레탈 메시. Bone 이 NAME_None 이거나 메시에 없으면 메시 위치.
	bool PlayEffect(const FGameplayTag& EffectTag, USkeletalMeshComponent* Mesh, FName Bone,
	                float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	// 행 하나를 실제로 튼다. 컴포넌트가 없는 미리보기 액터(애님 에디터)에서 노티파이도 이 함수를 쓴다 - 트는 법이 두 곳에 갈라지지 않게.
	static void PlayRow(const FVBContextEffectRow& Row, USkeletalMeshComponent* Mesh, FName Bone, float BodyScale,
	                    float VolumeMultiplier, float PitchMultiplier, ECollisionChannel TraceChannel, float TraceDistance);

private:
	const FVBContextEffectRow* FindRow(const FGameplayTag& EffectTag) const;

#if !UE_BUILD_SHIPPING
	// 행 없는 태그를 태그당 한 번만 말한다. 발걸음은 초당 여러 번이라 매번 찍으면 로그가 묻힌다.
	TSet<FGameplayTag> WarnedMissingTags;
	// 처음 울린 태그 - 한 번만 로그를 남긴다(PlayEffect 주석).
	TSet<FGameplayTag> PlayedTags;
#endif
};
