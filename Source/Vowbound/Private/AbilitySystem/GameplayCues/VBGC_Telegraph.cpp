// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBGC_Telegraph.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "CharacterCommon/VBHitFlashComponent.h"
#include "Components/SceneComponent.h"
#include "Data/VBHitFlashConfig.h"
#include "Data/VBTelegraphConfig.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystem/VBThreatTrackerSubsystem.h"
#include "Vowbound/Vowbound.h"

const UVBTelegraphConfig* UVBGC_Telegraph::GetConfig() const
{
	return TelegraphConfig ? TelegraphConfig.Get() : GetDefault<UVBTelegraphConfig>();
}

FGameplayTag UVBGC_Telegraph::ResolveAnswerTag(const FGameplayCueParameters& Parameters)
{
	// 답 태그의 부모는 Parryable 의 직계 부모(Attack.Answer)다. 부모 태그 상수를 따로 두면 홈이 둘이 된다.
	const FGameplayTag AnswerParent = VBGameplayTags::Attack_Answer_Parryable.GetTag().RequestDirectParent();
	for (const FGameplayTag& Tag : Parameters.AggregatedSourceTags)
	{
		if (Tag.MatchesTag(AnswerParent))
		{
			return Tag;
		}
	}
	return FGameplayTag();
}

bool UVBGC_Telegraph::OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	if (!MyTarget)
	{
		return false;
	}
	UWorld* World = MyTarget->GetWorld();
	if (!World)
	{
		return false;
	}

	const UVBTelegraphConfig* Config = GetConfig();
	const FGameplayTag Answer = ResolveAnswerTag(Parameters);
	const FVBTelegraphAnswerCue* AnswerCue = Config->FindAnswerCue(Answer);
	if (!AnswerCue)
	{
		// 답 행이 없다 = 공격 자산이 답을 안 실었거나 연출 자산에 그 답이 없다. 공통 연출과 흰 화살표는 그대로 내되 소리 내어 밝힌다.
		VB_LOG(Warning, "VBGC_Telegraph: %s 의 답 '%s' 에 연출 행이 없다 - 색·답 소리 없이 예고 (연출 자산 %s)",
		       *MyTarget->GetName(), *Answer.ToString(), *GetNameSafe(Config));
	}
	const FLinearColor Color = AnswerCue ? AnswerCue->Color : FLinearColor::White;

	// 1) 몸 발광. 길이는 서버가 실어 보낸 예고 길이(RawMagnitude). 색·세기·감쇠는 연출 자산.
	if (AnswerCue && Config->GlowOverlayMaterial)
	{
		if (UVBHitFlashComponent* Flash = MyTarget->FindComponentByClass<UVBHitFlashComponent>())
		{
			FVBHitFlashProfile Glow;
			Glow.OverlayMaterial = Config->GlowOverlayMaterial;
			Glow.Duration        = Parameters.RawMagnitude;
			Glow.Intensity       = AnswerCue->GlowIntensity;
			Glow.Color           = AnswerCue->Color;
			Glow.FalloffExponent = AnswerCue->GlowFalloffExponent;
			Flash->BeginGlow(Glow);
		}
		else
		{
			VB_LOG(Verbose, "VBGC_Telegraph: %s 에 UVBHitFlashComponent 없음 - 발광 스킵", *MyTarget->GetName());
		}
	}

	// 2) 소리. 적 루트에 3D 부착 - 적이 사라지면 함께 멈춘다(bStopWhenAttachedToDestroyed).
	if (USceneComponent* AttachTo = MyTarget->GetRootComponent())
	{
		auto PlayAttached = [&](USoundBase* Sound)
		{
			if (Sound)
			{
				UGameplayStatics::SpawnSoundAttached(Sound, AttachTo, NAME_None, FVector::ZeroVector,
				                                     EAttachLocation::KeepRelativeOffset,
				                                     /*bStopWhenAttachedToDestroyed=*/true, 1.0f, 1.0f, 0.0f,
				                                     Config->Attenuation);
			}
		};
		PlayAttached(Config->TelegraphVoice);
		PlayAttached(Config->TelegraphWhoosh);
		if (AnswerCue)
		{
			PlayAttached(AnswerCue->AnswerSound);
		}
	}

	// 3) 위협 등록 + 화면 밖 경고. 화면 판정은 로컬 첫 컨트롤러 기준 - 큐는 각 클라에서 따로 돌므로 그 클라의 화면이다.
	bool bOnScreen = true;
	if (UVBThreatTrackerSubsystem* Tracker = World->GetSubsystem<UVBThreatTrackerSubsystem>())
	{
		Tracker->Register(MyTarget, Answer, Color);

		const APlayerController* LocalPC = World->GetFirstPlayerController();
		if (LocalPC && LocalPC->IsLocalController())
		{
			bOnScreen = UVBThreatTrackerSubsystem::IsOnScreen(LocalPC, MyTarget->GetActorLocation());
			if (!bOnScreen && Config->OffScreenWarningSound)
			{
				UGameplayStatics::PlaySound2D(World, Config->OffScreenWarningSound);
			}
		}
	}

	// 계측: 큐가 실제로 도착했는지·무슨 답으로·화면 안팎을 한 줄로. «연출이 안 보인다» 를 큐 미등록과 자산 미할당으로 가르는 첫 근거.
	VB_LOG(Log, "[Telegraph] %s 예고 시작 - 답=%s 길이=%.2f 화면=%s 발광=%s", *MyTarget->GetName(), *Answer.ToString(),
	       Parameters.RawMagnitude, bOnScreen ? TEXT("안") : TEXT("밖"), (AnswerCue && Config->GlowOverlayMaterial) ? TEXT("O") : TEXT("X"));

	return true;
}

bool UVBGC_Telegraph::OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	if (!MyTarget)
	{
		return false;
	}

	// 발광 정지. 오버레이는 남기고 강도만 0 - 다음 피격 플래시가 같은 MID 를 그대로 쓴다.
	if (UVBHitFlashComponent* Flash = MyTarget->FindComponentByClass<UVBHitFlashComponent>())
	{
		Flash->EndGlow();
	}

	if (UWorld* World = MyTarget->GetWorld())
	{
		if (UVBThreatTrackerSubsystem* Tracker = World->GetSubsystem<UVBThreatTrackerSubsystem>())
		{
			Tracker->Unregister(MyTarget);
		}
	}
	VB_LOG(Log, "[Telegraph] %s 예고 끝", *MyTarget->GetName());
	return true;
}
