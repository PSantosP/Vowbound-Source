// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Character/VBHitFlashComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Data/VBHitFlashConfig.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayEffectTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"


namespace
{
	// 머티리얼 파라미터 이름. 문자열 리터럴을 호출부에 흩뿌리면 오타 한 글자가 컴파일을 통과한 뒤
	// '아무 일도 안 일어남'으로 나타난다(존재하지 않는 파라미터 설정은 엔진이 조용히 무시한다).
	// 상수로 못박아 오타를 링크 단계에서 잡는다.
	const FName GParamFlashStartTime(TEXT("FlashStartTime"));
	const FName GParamFlashDuration(TEXT("FlashDuration"));
	const FName GParamFlashFalloffExp(TEXT("FlashFalloffExp"));
	const FName GParamFlashIntensity(TEXT("FlashIntensity"));
	const FName GParamFlashColor(TEXT("FlashColor"));
	// 예약(림 라이트). 오늘 머티리얼은 읽지 않지만 push 는 한다 — 미등록 파라미터 설정은 무해한 무동작이라
	// 나중에 머티리얼만 고치면 C++ 변경 없이 켜진다.
	const FName GParamFlashFresnelStrength(TEXT("FlashFresnelStrength"));
	const FName GParamFlashFresnelExp(TEXT("FlashFresnelExp"));

	// 플래시 최소 길이(초). 타이머 rate 가 0 이하면 엔진이 SetTimer 를 clear 로 처리해 만료 콜백이 영영 오지 않는다.
	constexpr float GMinFlashDuration = 0.01f;

	// 재무장 하한(실초). 남은 시간이 이보다 짧으면 만료로 본다 — 마이크로초 단위 재무장이 반복되는 것을 막는다.
	constexpr double GRearmEpsilon = 0.001;
}


UVBHitFlashComponent::UVBHitFlashComponent()
{
	// 감쇠는 GPU 가 한다. CPU 는 피격 순간에만 일하므로 tick 이 필요 없다.
	PrimaryComponentTick.bCanEverTick = false;
}

const UVBHitFlashConfig* UVBHitFlashComponent::GetHitFlashConfig() const
{
	// 할당된 config 우선, 없으면 클래스 CDO 디폴트(절대 null 아님).
	return HitFlashConfig ? HitFlashConfig : GetDefault<UVBHitFlashConfig>();
}

USkeletalMeshComponent* UVBHitFlashComponent::ResolveTargetMesh() const
{
	// 소유자를 ACharacter 로만 본다. AVBCharacter/AVBEnemyBase 는 공통 VB 베이스가 없어서
	// 구체 클래스를 캐스트하면 이 컴포넌트가 둘 중 한쪽 전용이 된다.
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	return OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
}

void UVBHitFlashComponent::TriggerFlash(const FGameplayCueParameters& Parameters)
{
	UWorld* World = GetWorld();
	USkeletalMeshComponent* TargetMesh = ResolveTargetMesh();
	if (!World || !TargetMesh)
	{
		return;
	}

	// 태그로 프로필 해석. OriginalTag 는 큐 실행 태그가 RPC 로 전달돼 클라에서도 채워지는 값이라,
	// 미래의 타입별 플래시(...HitFlash.Infection)가 클라 분기 키로 그대로 쓸 수 있다.
	const UVBHitFlashConfig* Config = GetHitFlashConfig();
	const FVBHitFlashProfile& Profile = Config->ResolveProfile(Parameters.OriginalTag);
	if (!Profile.OverlayMaterial)
	{
		// 머티리얼 미할당 = 이 프로젝트에 아직 플래시 자산이 없다는 뜻. 무연출로 빠진다(코스메틱이라 무해).
		return;
	}

	// MID 는 프로필 머티리얼당 1개. 오버레이는 슬롯 무관하게 전 섹션을 덮으므로 섹션 수만큼 만들 이유가 없다.
	// 프로필이 바뀌었을 때만 다시 만든다.
	if (!IsValid(FlashMID) || FlashSourceMaterial != Profile.OverlayMaterial)
	{
		FlashMID = UMaterialInstanceDynamic::Create(Profile.OverlayMaterial, this);
		FlashSourceMaterial = Profile.OverlayMaterial;
	}
	if (!FlashMID)
	{
		return;
	}

	// 시계는 게임시간이 아니라 실시간이다. UWorld::RealTimeSeconds 는 시간 팽창이 적용되기 전에 누적되고,
	// 셰이더가 읽는 View.RealTime 이 정확히 같은 값이다 — CPU 가 심는 시작시각과 GPU 가 읽는 현재시각이
	// 같은 축 위에 있어야 히트스톱(GlobalTimeDilation=0.01) 중에도 플래시 길이가 100배로 늘지 않는다.
	const double NowRealTime = World->GetRealTimeSeconds();
	const float FlashDuration = FMath::Max(Profile.Duration, GMinFlashDuration);

	// 강도 = 프로필 기본값 x 데미지 커브. 키가 0개면 커브를 적용하지 않는다(빈 커브 = 배수 없음).
	float FlashIntensity = Profile.Intensity;
	if (const FRichCurve* IntensityCurve = Profile.IntensityByDamage.GetRichCurveConst())
	{
		if (IntensityCurve->GetNumKeys() > 0)
		{
			FlashIntensity *= IntensityCurve->Eval(Parameters.RawMagnitude, 1.0f);
		}
	}

	FlashMID->SetScalarParameterValue(GParamFlashStartTime, static_cast<float>(NowRealTime));
	FlashMID->SetScalarParameterValue(GParamFlashDuration, FlashDuration);
	FlashMID->SetScalarParameterValue(GParamFlashFalloffExp, Profile.FalloffExponent);
	FlashMID->SetScalarParameterValue(GParamFlashIntensity, FlashIntensity);
	FlashMID->SetVectorParameterValue(GParamFlashColor, Profile.Color);
	FlashMID->SetScalarParameterValue(GParamFlashFresnelStrength, Profile.FresnelStrength);
	FlashMID->SetScalarParameterValue(GParamFlashFresnelExp, Profile.FresnelExponent);

	// 같은 MID 면 다시 걸지 않는다. SetOverlayMaterial 은 스켈레탈 렌더 상태를 통째로 재생성하므로
	// 연타 때마다 부르면 타격마다 재생성 비용을 낸다. 파라미터 갱신만으로 다음 플래시가 성립한다.
	if (TargetMesh->GetOverlayMaterial() != FlashMID)
	{
		TargetMesh->SetOverlayMaterial(FlashMID);
	}
	if (!FMath::IsNearlyEqual(TargetMesh->GetOverlayMaterialMaxDrawDistance(), Profile.MaxDrawDistance))
	{
		TargetMesh->SetOverlayMaterialMaxDrawDistance(Profile.MaxDrawDistance);
	}

	FlashEndRealTime = NowRealTime + FlashDuration;

	// 실초 값을 게임시간 타이머에 그대로 건다. UVBTimeDilationSubsystem 의 GameDelay = RealDuration x Scale
	// 관용구를 복제하지 않는 이유: 그 증명은 '요청이 지배(D==Scale)인 동안만 산다'를 전제하는데,
	// 플래시는 히트스톱보다 오래 산다. Duration 0.18 / Scale 0.01 이면 GameDelay 0.0018 게임초인데
	// 히트스톱이 0.04 실초에 끝나 D 가 1.0 으로 돌아오면 총 0.041 실초에 발화한다 = 밝기 77% 에서 팝.
	// 반대로 그냥 걸면 D<=1 이라 항상 늦게 발화하므로 시각적으로 안전하고, OnFlashExpired 의 실시간
	// 재검증이 그 늦음을 정확히 걷어낸다.
	// 같은 핸들 SetTimer 는 엔진이 기존 것을 먼저 clear 하므로, 연타해도 중첩 없이 최신 요청이 창을 교체한다.
	World->GetTimerManager().SetTimer(FlashTimerHandle, this, &UVBHitFlashComponent::OnFlashExpired,
	                                  FlashDuration, false);
}

void UVBHitFlashComponent::OnFlashExpired()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 타이머는 게임시간 축이라 실시간과 어긋난다. 진실은 실시간이므로 여기서 다시 판정한다.
	const double Remaining = FlashEndRealTime - World->GetRealTimeSeconds();
	if (Remaining > GRearmEpsilon)
	{
		// 아직 안 끝났다(시간 가속처럼 D>1 인 구간이 있으면 게임시간 타이머가 실시간보다 먼저 온다).
		// 잔여만큼 재무장 — 같은 핸들이라 중첩되지 않는다.
		World->GetTimerManager().SetTimer(FlashTimerHandle, this, &UVBHitFlashComponent::OnFlashExpired,
		                                  static_cast<float>(Remaining), false);
		return;
	}

	// 만료 확정. 오버레이 자체는 해제하지 않는다 — SetOverlayMaterial 이 렌더 상태를 재생성하므로
	// 액터 생애당 set 1 + (사망 시) clear 1 로 수렴시키는 편이 싸다. 대신 강도를 0 으로 눌러
	// GPU 감쇠와 별개로 발광 기여를 확실히 끊는다(파라미터 갱신은 재생성이 아니라 값 쓰기라 저렴하다).
	if (IsValid(FlashMID))
	{
		FlashMID->SetScalarParameterValue(GParamFlashIntensity, 0.0f);
	}
}

void UVBHitFlashComponent::ClearFlash()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FlashTimerHandle);
	}

	// 오버레이 해제. 사망 정리 경로에서만 불린다.
	if (USkeletalMeshComponent* TargetMesh = ResolveTargetMesh())
	{
		if (TargetMesh->GetOverlayMaterial() != nullptr)
		{
			TargetMesh->SetOverlayMaterial(nullptr);
		}
	}

	FlashEndRealTime = 0.0;
}
