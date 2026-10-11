// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Character/VBContextEffectComponent.h"

#include "Camera/CameraShakeBase.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBContextEffectsLibrary.h"
#include "Engine/World.h"
#include "Enemy/VBEnemyBase.h" // ResolveStrideScale - 몸 배율의 홈
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"
#include "Vowbound/Vowbound.h"

UVBContextEffectComponent::UVBContextEffectComponent()
{
	// 노티파이가 부를 때만 일한다.
	PrimaryComponentTick.bCanEverTick = false;
}

const FVBContextEffectRow* UVBContextEffectComponent::FindRow(const FGameplayTag& EffectTag) const
{
	for (const UVBContextEffectsLibrary* Library : Libraries)
	{
		if (const FVBContextEffectRow* Row = Library ? Library->FindRow(EffectTag, Contexts) : nullptr)
		{
			return Row;
		}
	}
	return nullptr;
}

bool UVBContextEffectComponent::PlayEffect(const FGameplayTag& EffectTag, USkeletalMeshComponent* Mesh, FName Bone,
                                           float VolumeMultiplier, float PitchMultiplier)
{
	const FVBContextEffectRow* Row = EffectTag.IsValid() ? FindRow(EffectTag) : nullptr;
	if (!Row)
	{
#if !UE_BUILD_SHIPPING
		if (EffectTag.IsValid() && !WarnedMissingTags.Contains(EffectTag))
		{
			// 조용한 누락을 막는다(설계 §3). 옛 Foley 가 떨어질 자리였던 때는 Log 였다 - 그 길을 뗀 뒤로는 행이 없으면 곧 소리가 없다(Warning).
			WarnedMissingTags.Add(EffectTag);
			VB_LOG(Warning, "[이펙트] %s 의 표에 %s 행이 없다 - 이 순간은 안 튼다. Libraries 에 그 태그를 가진 표를 꽂아라",
			       *GetNameSafe(GetOwner()), *EffectTag.ToString());
		}
#endif
		return false;
	}
#if !UE_BUILD_SHIPPING
	// 태그마다 처음 한 번만 «울렸다» 를 남긴다 - user 가 «느낌이 없다» 고 할 때 «안 울렸다» 와 «울렸는데 약하다» 를 로그로 가르려고(2026-09-30).
	//   발걸음은 초당 여러 번이라 매번 찍으면 로그가 묻힌다.
	if (!PlayedTags.Contains(EffectTag))
	{
		PlayedTags.Add(EffectTag);
		VB_LOG(Log, "[이펙트] %s 첫 재생 %s (뼈 %s)", *GetNameSafe(GetOwner()), *EffectTag.ToString(), *Bone.ToString());
	}
#endif
	USkeletalMeshComponent* SourceMesh = Mesh ? Mesh : (GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr);
	PlayRow(*Row, SourceMesh, Bone, AVBEnemyBase::ResolveStrideScale(GetOwner()), VolumeMultiplier, PitchMultiplier,
	        GroundTraceChannel, GroundTraceDistance);
	return true;
}

void UVBContextEffectComponent::PlayRow(const FVBContextEffectRow& Row, USkeletalMeshComponent* Mesh, FName Bone, float BodyScale,
                                        float VolumeMultiplier, float PitchMultiplier, ECollisionChannel TraceChannel, float TraceDistance)
{
	UWorld* World = Mesh ? Mesh->GetWorld() : nullptr;
	// 전용 서버는 보고 듣는 사람이 없다. 옛 Foley 와 같은 규칙.
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const bool    bHasBone = Bone != NAME_None && Mesh->DoesSocketExist(Bone);
	const FVector Location = bHasBone ? Mesh->GetSocketLocation(Bone) : Mesh->GetComponentLocation();
	const float   Scale    = Row.bScaleWithBody ? FMath::Max(BodyScale, KINDA_SMALL_NUMBER) : 1.0f;

	if (Row.Sounds.Num() > 0)
	{
		// 뼈에 붙인다 - 옛 Foley 는 몸 뿌리에 붙여 발소리가 몸 한가운데서 났다. 붙이는 이유는 옛 것과 같다: 긴 소리(미끄러짐)가 몸을 따라가게.
		if (USoundBase* Sound = Row.Sounds[FMath::RandRange(0, Row.Sounds.Num() - 1)])
		{
			UGameplayStatics::SpawnSoundAttached(Sound, Mesh, bHasBone ? Bone : NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			                                     EAttachLocation::SnapToTarget, /*bStopWhenAttachedToDestroyed=*/true,
			                                     FMath::Max(0.0f, VolumeMultiplier), FMath::Max(0.01f, PitchMultiplier), 0.0f, Row.Attenuation);
		}
	}
	if (Row.Niagara)
	{
		// 풀에서 꺼내 쓰고 끝나면 엔진이 돌려받는다. 풀링이면 bAutoDestroy 는 무시된다(NiagaraFunctionLibrary.cpp::CreateNiagaraSystem).
		//   Lyra 는 ENCPoolMethod::None 으로 매번 새로 만든다(설계 §9) - 발걸음처럼 잦은 순간에는 풀이 할당을 줄인다.
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Row.Niagara, Location, FRotator::ZeroRotator, FVector(Scale),
		                                               /*bAutoDestroy=*/false, /*bAutoActivate=*/true, ENCPoolMethod::AutoRelease);
	}
	if (Row.DecalMaterial)
	{
		// 데칼이 있는 행만 바닥을 찾는다. 뼈는 땅 위에 떠 있으니 조금 위에서 아래로 쏜다.
		FHitResult            Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VBContextEffectGround), /*bTraceComplex=*/false, Mesh->GetOwner());
		const FVector         Start = Location + FVector(0.0f, 0.0f, 50.0f);
		if (World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0f, 0.0f, TraceDistance + 50.0f), TraceChannel, Params))
		{
			// 데칼은 자기 X 축으로 투영한다 - 바닥 법선의 반대를 보게 세운다(SpawnDecalAtLocation 기본 회전 (-90,0,0) 이 평평한 바닥의 그 값이다).
			const FRotator DecalRotation = (-Hit.ImpactNormal).Rotation();
			if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, Row.DecalMaterial, Row.DecalSize * Scale, Hit.ImpactPoint,
			                                                                     DecalRotation, Row.DecalLifeSeconds + Row.DecalFadeSeconds))
			{
				// 주인 파괴는 끈다 - 이 데칼의 주인은 월드 설정 액터다(GameplayStatics.cpp::SpawnDecalAtLocation -> CreateDecalComponent(..., World->GetWorldSettings(), LifeSpan)).
				//   컴포넌트 수명은 위 LifeSpan 이 정리한다.
				Decal->SetFadeOut(Row.DecalLifeSeconds, Row.DecalFadeSeconds, /*DestroyOwnerAfterFade=*/false);
			}
		}
	}
	if (Row.CameraShake)
	{
		UGameplayStatics::PlayWorldCameraShake(World, Row.CameraShake, Location, Row.ShakeInnerRadius * Scale, Row.ShakeOuterRadius * Scale);
	}
}
