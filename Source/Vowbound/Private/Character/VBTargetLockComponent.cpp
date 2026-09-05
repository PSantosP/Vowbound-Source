// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Character/VBTargetLockComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Character/VBTargetable.h"
#include "Data/VBTargetLockConfig.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Vowbound/Vowbound.h"
#include "Net/UnrealNetwork.h"
#include "Player/VBPlayerState.h"


UVBTargetLockComponent::UVBTargetLockComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.SetTickFunctionEnable(false);
	// 생성자(CDO 생성 시점)에선 SetIsReplicated 가 ensure 발화 — ByDefault 가 공식 경로 (ActorComponent.cpp::UActorComponent::SetIsReplicated)
	SetIsReplicatedByDefault(true);
}

const UVBTargetLockConfig* UVBTargetLockComponent::GetTargetLockConfig() const
{
	// 할당된 config 우선, 없으면 클래스 CDO 디폴트(절대 null 아님).
	return LockConfig ? LockConfig : GetDefault<UVBTargetLockConfig>();
}

void UVBTargetLockComponent::ToggleLockOn()
{
	// 클라 진입점 — 로컬 카메라 기반 탐색 후 서버 RPC.
	// 의도: remote client의 서버 측 PlayerCameraManager는 tick이 안 되어 카메라 정보가 stale.
	// 반드시 로컬(소유 클라)에서 target 탐색 후 actor ref만 서버로 전달해야 한다.
	if (!OwnerCharacter.IsValid()) return;

	if (IsLockedOn())
	{
		ServerClearLockOn();
	}
	else
	{
		if (AActor* Target = FindBestTarget())
		{
			ServerSetLockOn(Target);
		}
	}
}

void UVBTargetLockComponent::SwitchTarget(float AxisValue)
{
	// 로컬 early-return
	if (!IsLockedOn())                  return;
	if (FMath::IsNearlyZero(AxisValue)) return;

	// 로컬 카메라 기반 좌/우 탐색 — 원격 클라에서도 정확
	if (AActor* NextTarget = FindSideTarget(AxisValue))
	{
		ServerSetLockOn(NextTarget);
	}
}

void UVBTargetLockComponent::ServerSetLockOn_Implementation(AActor* Target)
{
	// 서버 validation — 클라가 임의 actor를 보낼 수 있으므로 재검증
	if (!IsValid(Target)) return;
	if (!OwnerCharacter.IsValid()) return;
	if (!IsTargetValid(Target)) return;            // 거리 + IVBTargetable
	if (LockedTarget.Get() == Target) return;      // 이미 같은 타겟이면 noop

	SetLockOn(Target);
}

bool UVBTargetLockComponent::ServerSetLockOn_Validate(AActor* /*Target*/)
{
	// 기본 검증만. 실제 validation은 Implementation 내부.
	// 치트 방지 강화가 필요하면 Target 거리/type 체크 추가.
	return true;
}

void UVBTargetLockComponent::ServerClearLockOn_Implementation()
{
	if (!IsLockedOn()) return;
	SetLockOn(nullptr);
}

bool UVBTargetLockComponent::ServerClearLockOn_Validate()
{
	return true;
}

AActor* UVBTargetLockComponent::GetLockedTarget() const
{
	return LockedTarget.Get();
}

bool UVBTargetLockComponent::IsLockedOn() const
{
	return IsValid(LockedTarget);
}

void UVBTargetLockComponent::TickComponent(float                        DeltaTime, enum ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Timeline tick — 모든 Net Role (시각 효과)
	CameraTransitionTimeline.TickTimeline(DeltaTime);

	if (!IsLockedOn())
	{
		if (!CameraTransitionTimeline.IsPlaying())
		{
			SetComponentTickEnabled(false);
		}
		return;
	}

	if (!OwnerCharacter.IsValid()) return;

	// 로컬 플레이어 전용 — 카메라는 로컬에서만 정확, 자동 재탐색도 로컬 기준
	if (OwnerCharacter->IsLocallyControlled())
	{
		const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
		if (!OwnerController.IsValid())
		{
			ServerClearLockOn();
			return;
		}

		// 타겟 유효성 재검사 — 로컬 감지 후 서버 RPC로 반영
		if (!IsTargetValid(LockedTarget.Get()))
		{
			AActor* NextTarget = FindBestTarget();
			if (NextTarget)
			{
				ServerSetLockOn(NextTarget);
			}
			else
			{
				ServerClearLockOn();
			}
			return;
		}

		// 거리 체크
		if (const float Distance = FVector::Dist(OwnerCharacter->GetActorLocation(), LockedTarget->GetActorLocation());
			Distance > Cfg->LockBreakDistance)
		{
			ServerClearLockOn();
			return;
		}

		// 카메라 보간 — 로컬 카메라만 조작
		// 조준점은 액터 위치(캡슐 중심)가 아니라 '올린 마커 지점'(가슴)을 쓴다 → 근접 시 pitch 탑다운 완화.
		// IVBTargetable 캐스트 실패 시 방어적으로 액터 위치 폴백(현재 타겟은 적뿐이나 방어).
		FVector TargetAimPoint = LockedTarget->GetActorLocation();
		if (const IVBTargetable* Targetable = Cast<IVBTargetable>(LockedTarget.Get()))
		{
			TargetAimPoint = Targetable->GetTargetMarkerLocation();
		}
		FVector  DirectionToTarget = TargetAimPoint - OwnerCharacter->GetCamera()->GetComponentLocation();
		FRotator LookAtRotation    = DirectionToTarget.Rotation();
		FRotator CurrentRotation   = OwnerController->GetControlRotation();
		FRotator NewRotation       = FMath::RInterpTo(CurrentRotation, LookAtRotation, DeltaTime, Cfg->InterpSpeed);

		// Pitch 클램핑 (config 외부화 — 과거 하드코딩 -35/15)
		NewRotation.Pitch = FMath::ClampAngle(NewRotation.Pitch, Cfg->MinCameraPitch, Cfg->MaxCameraPitch);
		OwnerController->SetControlRotation(NewRotation);
	}

	// 서버 전용 — 락온 유지 = 지속 전투 자극 (Host는 로컬+서버 모두 해당)
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		OwnerCharacter->NotifyCombatStimulus();
	}
}

void UVBTargetLockComponent::BeginPlay()
{
	Super::BeginPlay();

	// OwnerCharacter, OwnerController 캐싱
	if (AVBCharacter* Owner = Cast<AVBCharacter>(GetOwner()))
	{
		OwnerCharacter  = Owner;
		OwnerController = OwnerCharacter->GetController<APlayerController>();
	}

	if (OwnerCharacter.IsValid())
	{
		// 카메라 기본값 캐시
		DefaultTargetArmLength = OwnerCharacter->GetSpringArm()->TargetArmLength;
		DefaultSocketOffset    = OwnerCharacter->GetSpringArm()->SocketOffset;
		DefaultCameraLagSpeed  = OwnerCharacter->GetSpringArm()->CameraLagSpeed;
		DefaultFieldOfView     = OwnerCharacter->GetCamera()->FieldOfView;

		// FTimeline 초기화 (UCurveFloat 바인딩)
		if (CameraTransitionCurve)
		{
			FOnTimelineFloat ProgressCallback;
			ProgressCallback.BindUFunction(this, FName("OnCameraTransitionUpdate"));
			CameraTransitionTimeline.AddInterpFloat(CameraTransitionCurve, ProgressCallback);
			CameraTransitionTimeline.SetTimelineLength(GetTargetLockConfig()->CameraTransitionDuration);
			CameraTransitionTimeline.SetLooping(false);
		}
		else
		{
			// 커브가 없으면 전투 카메라 전환(팔길이/소켓오프셋/FOV Lerp)이 통째로 스킵된다.
			// 예전엔 이 스킵이 무음이라 FOV 를 180 으로 놓고 라이브로 돌려 보다가 발견됐다(FIND-046).
			// 짝인 CameraTransitionDuration 은 DA 에 있어 눈에 띄는데 커브만 컴포넌트 디테일 패널이라
			// 배선 누락이 다른 화면에 숨는다. 커브를 Config 로 옮기기 전까지는 이 로그가 방어선이다.
			VB_LOG(Warning, "CameraTransitionCurve 미배선 - 전투 카메라 전환이 전부 스킵된다");
		}
	}

}

void UVBTargetLockComponent::OnCameraTransitionUpdate(float Alpha)
{
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();

	if (USpringArmComponent* SpringArm = OwnerCharacter->GetSpringArm())
	{
		SpringArm->TargetArmLength = FMath::Lerp(DefaultTargetArmLength, Cfg->CombatTargetArmLength, Alpha);
		SpringArm->SocketOffset    = FMath::Lerp(DefaultSocketOffset, Cfg->CombatSocketOffset, Alpha);
		SpringArm->CameraLagSpeed  = FMath::Lerp(DefaultCameraLagSpeed, Cfg->CombatCameraLagSpeed, Alpha);
	}

	if (UCameraComponent* Camera = OwnerCharacter->GetCamera())
	{
		Camera->SetFieldOfView(FMath::Lerp(DefaultFieldOfView, Cfg->CombatFieldOfView, Alpha));
	}
}

AActor* UVBTargetLockComponent::FindBestTarget()
{
	TArray<AActor*>                       OverlapResults;
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(GetTargetLockConfig()->SearchTraceChannel));
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(OwnerCharacter.Get());


	const FVector Start   = OwnerCharacter->GetActorLocation();
	bool          bIsFind = UKismetSystemLibrary::SphereOverlapActors(
	                                                                  OwnerCharacter.Get(),
	                                                                  Start,
	                                                                  GetTargetLockConfig()->TargetSearchRadius,
	                                                                  ObjectTypes,
	                                                                  nullptr,
	                                                                  IgnoreActors,
	                                                                  OverlapResults
	                                                                 );

	if (!OwnerController.IsValid() || !OwnerController->PlayerCameraManager) return nullptr;
	
	AActor* BestTarget = nullptr;
	
	if (bIsFind)
	{
		const FVector Location      = OwnerController->PlayerCameraManager->GetCameraLocation();
		const FVector CameraForward = OwnerController->PlayerCameraManager->GetCameraRotation().Vector();

		
		float   BestScore  = -1.0f;

		for (AActor* Candidate : OverlapResults)
		{
			if (!Cast<IVBTargetable>(Candidate)) continue;
			if (!IsTargetValid(Candidate)) continue;

			FVector DirectionToTarget = (Candidate->GetActorLocation() - Location).GetSafeNormal();
			float   Score             = FVector::DotProduct(CameraForward, DirectionToTarget);
			if (Score < FMath::Cos(FMath::DegreesToRadians(GetTargetLockConfig()->LockAngle))) continue;
			float Distance      = FVector::Dist(Start, Candidate->GetActorLocation());
			float DistanceScore = 1.0f - (Distance / GetTargetLockConfig()->TargetSearchRadius);
			float FinalScore    = Score + DistanceScore * GetTargetLockConfig()->DistanceScoreWeight;
			if (FinalScore > BestScore)
			{
				BestScore  = FinalScore;
				BestTarget = Candidate;
			}
		}
	}
	
	return BestTarget;
}


AActor* UVBTargetLockComponent::FindSideTarget(float AxisValue)
{
	// 로컬 카메라 기반 좌/우 target 탐색. remote client에서도 로컬 카메라는 정확.
	if (!OwnerController.IsValid() || !OwnerController->PlayerCameraManager) return nullptr;
	if (!OwnerCharacter.IsValid()) return nullptr;

	const FRotator CameraRotation = OwnerController->PlayerCameraManager->GetCameraRotation();
	const FVector  CameraRight    = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Y);
	const FVector  CameraLocation = OwnerController->PlayerCameraManager->GetCameraLocation();

	TArray<AActor*>                       OverlapResults;
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(GetTargetLockConfig()->SearchTraceChannel));
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(OwnerCharacter.Get());
	IgnoreActors.Add(LockedTarget.Get());

	const bool bIsFind = UKismetSystemLibrary::SphereOverlapActors(
		OwnerCharacter.Get(),
		OwnerCharacter->GetActorLocation(),
		GetTargetLockConfig()->TargetSearchRadius,
		ObjectTypes,
		nullptr,
		IgnoreActors,
		OverlapResults);

	if (!bIsFind) return nullptr;

	AActor* BestCandidate = nullptr;
	float   BestDistance  = MAX_FLT;
	for (AActor* Candidate : OverlapResults)
	{
		if (!Cast<IVBTargetable>(Candidate)) continue;
		if (!IsTargetValid(Candidate)) continue;

		// 좌/우 판단
		const FVector DirectionToCandidate = (Candidate->GetActorLocation() - CameraLocation).GetSafeNormal();
		const float   RightDot             = FVector::DotProduct(CameraRight, DirectionToCandidate);

		if (AxisValue > 0 && RightDot <= 0) continue;  // 오른쪽 입력 — 왼쪽 적 스킵
		if (AxisValue < 0 && RightDot >= 0) continue;  // 왼쪽 입력 — 오른쪽 적 스킵

		const float Distance = FVector::Dist(OwnerCharacter->GetActorLocation(), Candidate->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance  = Distance;
			BestCandidate = Candidate;
		}
	}

	return BestCandidate;
}


bool UVBTargetLockComponent::IsTargetValid(AActor* Actor) const
{
	// 액터 유효성 체크
	if (!IsValid(Actor)) return false;

	// 적 타겟 체크
	IVBTargetable* Targetable = Cast<IVBTargetable>(Actor);
	if (!Targetable || !Targetable->IsTargetable()) return false;

	// 거리 체크
	float Distance = FVector::Dist(OwnerCharacter->GetActorLocation(), Actor->GetActorLocation());
	if (Distance > GetTargetLockConfig()->MaxLockDistance) return false;

	return true;
}

AActor* UVBTargetLockComponent::FindSoftLungeTarget() const
{
	// 락온 없이 공격 시작 시 첫 스윙이 돌진할 정면 최근접 적을 고른다(GoW/DMC식 소프트 타겟).
	// 넷-세이프 핵심: 카메라(PlayerCameraManager)가 아니라 액터 forward 를 쓴다.
	//   SetWarpTargetLockedEnemy 는 서버+소유클라 양쪽에서 돌고 워프가 서버에 있어야 권위 루트모션이 움직인다.
	//   카메라는 서버에서 remote client 에 대해 stale → 서버가 다른 타겟 픽 → 무브 desync. 액터 회전은 복제되므로
	//   양쪽이 동일 입력 → 동일 타겟(결정론). 이는 PerformAttackTrace 가 서버에서 쓰는 것과 같은 기준이다.
	// 상태 변이 없음(const) — 하드락 LockedTarget 은 절대 건드리지 않는다(워프 전용 transient).
	if (!OwnerCharacter.IsValid()) return nullptr;

	const UVBTargetLockConfig* Cfg = GetTargetLockConfig(); // CDO 폴백 — 절대 null 아님

	TArray<AActor*>                       OverlapResults;
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(Cfg->SearchTraceChannel)); // config 외부화(현행 ECC_Pawn)
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(OwnerCharacter.Get());

	const FVector Origin  = OwnerCharacter->GetActorLocation();
	const FVector Forward = OwnerCharacter->GetActorForwardVector(); // 카메라가 아니라 액터 forward

	if (!UKismetSystemLibrary::SphereOverlapActors(
	                                               OwnerCharacter.Get(),
	                                               Origin,
	                                               Cfg->SoftLungeRadius,
	                                               ObjectTypes,
	                                               nullptr,
	                                               IgnoreActors,
	                                               OverlapResults))
	{
		return nullptr;
	}

	const float CosLimit  = FMath::Cos(FMath::DegreesToRadians(Cfg->SoftLungeAngle));
	AActor*     BestTarget = nullptr;
	float       BestScore  = -1.0f;

	for (AActor* Candidate : OverlapResults)
	{
		if (!Cast<IVBTargetable>(Candidate)) continue;
		if (!IsTargetValid(Candidate)) continue; // IsValid + IsTargetable + MaxLockDistance 재사용

		const FVector Dir = (Candidate->GetActorLocation() - Origin).GetSafeNormal();
		const float   Dot = FVector::DotProduct(Forward, Dir);
		if (Dot < CosLimit) continue; // 정면 콘 밖 제외

		const float Distance   = FVector::Dist(Origin, Candidate->GetActorLocation());
		const float FinalScore  = Dot + (1.0f - Distance / Cfg->SoftLungeRadius) * Cfg->DistanceScoreWeight;
		if (FinalScore > BestScore) // 결정론적 최고점수 픽(무작위 없음 → 서버·클라 합의)
		{
			BestScore  = FinalScore;
			BestTarget = Candidate;
		}
	}

	return BestTarget; // 상태 변이 0 — 워프 전용
}

void UVBTargetLockComponent::TryAutoLockOnTarget(AActor* Target)
{
	// 서버 전용 자동 락온. PerformAttackTrace(서버 권위)에서만 호출되지만 방어적 권위 가드.
	if (!GetOwner() || !GetOwner()->HasAuthority()) return; // 서버만 상태 변이

	// 설정 토글 — getter 는 미할당 시 CDO 디폴트 반환(절대 null 아님).
	if (!GetTargetLockConfig()->bAutoLockOnHit) return;

	// 정책: 이미 락온 중이면 무시 — 수동/기존 타겟에서 카메라를 뺏지 않는다.
	if (IsLockedOn()) return;

	// 거리(근접이라 항상 통과) + IVBTargetable(살아있음) 재검증.
	if (!IsTargetValid(Target)) return;

	// 기존 서버 권위 상태 변이 재사용 — 복제 LockedTarget + 호스트 수동 OnRep(카메라/HUD).
	SetLockOn(Target);
}

// 소유 클라에서 카메라 기준으로 대상을 찾아 락온을 요청한다. 이미 락온 중이면 아무것도 하지 않는다.
// ToggleLockOn 과의 차이는 '해제하지 않는다' 하나다 - 가드/패링 같은 자동 훅은 걸기만 원한다.
void UVBTargetLockComponent::TryLockOnFromLocalView()
{
	if (!OwnerCharacter.IsValid()) { return; }

	// 로컬 조종자만 카메라를 신뢰할 수 있다. 서버는 원격 클라의 카메라를 tick 하지 않는다.
	if (!OwnerCharacter->IsLocallyControlled()) { return; }

	// 수동 락이든 자동 락이든 이미 걸려 있으면 뺏지 않는다(bAutoLockOnHit 과 같은 정책).
	if (IsLockedOn()) { return; }

	if (AActor* Target = FindBestTarget())
	{
		ServerSetLockOn(Target);
	}
}

void UVBTargetLockComponent::SetLockOn(AActor* Target)
{
	// 서버 전용 상태 변경 — ServerSetLockOn/ServerClearLockOn/TryAutoLockOnTarget에서만 호출.
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;

	// 이전 target의 gameplay 효과 제거 (ASC 태그 등)
	// TObjectPtr는 .IsValid() 없음 — 전역 IsValid(TObjectPtr) 사용 (null + pending-kill 동시 검사)
	if (IsValid(LockedTarget))
	{
		RemoveLockOnGameplayEffects();
	}

	// Replicated 상태 변경 → 원격 클라의 OnRep 발동. 서버는 수동 호출 필요(아래 참고).
	LockedTarget = Target;

	if (Target)
	{
		ApplyLockOnGameplayEffects();
	}

	// 서버(Host 포함)에서 로컬 시각 효과 수동 호출.
	// REPNOTIFY_Always라도 OnRep은 서버 자체에는 발동되지 않는다 (replication 수신 측만 발동).
	OnRep_LockedTarget();
}

void UVBTargetLockComponent::ClearLockOn()
{
	if (!IsLockedOn()) return;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		// 서버 컨텍스트 — 직접 상태 변경
		SetLockOn(nullptr);
	}
	else
	{
		// 클라 컨텍스트 — Server RPC로 요청
		ServerClearLockOn();
	}
}

void UVBTargetLockComponent::ApplyLockOnGameplayEffects()
{
	// 서버 전용 — ASC 태그 + 전투 자극(WSC) + Sprint 해제. RemoveLockOnGameplayEffects와 대칭.
	if (!OwnerCharacter.IsValid()) return;

	if (OwnerCharacter->GetIsSprinting())
	{
		OwnerCharacter->StopSprint();
	}
	// 락온 = 전투 자극 — 평상이면 상태2(전투·무기Off) 진입, 발도는 하지 않는다 (DEC-006 ①)
	OwnerCharacter->NotifyCombatStimulus();

	if (AVBPlayerState* PS = Cast<AVBPlayerState>(OwnerCharacter->GetPlayerState()))
	{
		if (UAbilitySystemComponent* ASC = PS->GetVBAbilitySystemComponent())
		{
			ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_LockedOn, 1, EGameplayTagReplicationState::CountToOwner);
		}
	}
}

void UVBTargetLockComponent::RemoveLockOnGameplayEffects()
{
	// 서버 전용 — ASC 태그 제거. CombatMode는 타이머 기반이라 여기서 강제 해제 X.
	if (!OwnerCharacter.IsValid()) return;

	if (AVBPlayerState* PS = Cast<AVBPlayerState>(OwnerCharacter->GetPlayerState()))
	{
		if (UAbilitySystemComponent* ASC = PS->GetVBAbilitySystemComponent())
		{
			ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_LockedOn, 1, EGameplayTagReplicationState::CountToOwner);
		}
	}
}

void UVBTargetLockComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Lock-On 타겟은 소유자(자기 자신)만 알면 됨 — 다른 클라엔 표시/시뮬레이션에 불필요. 대역폭 절감.
	DOREPLIFETIME_CONDITION_NOTIFY(UVBTargetLockComponent, LockedTarget, COND_OwnerOnly, REPNOTIFY_Always);
}

void UVBTargetLockComponent::OnRep_LockedTarget()
{
	// 모든 Net Role에서 실행 — 로컬 시각/UI 효과.
	// 서버: SetLockOn에서 수동 호출. 클라: replication 수신 시 자동 호출 (REPNOTIFY_Always).
	if (IsValid(LockedTarget))
	{
		CameraTransitionTimeline.PlayFromStart();
		SetComponentTickEnabled(true);
		OnTargetLocked.Broadcast(LockedTarget.Get());
	}
	else
	{
		CameraTransitionTimeline.Reverse();
		// Tick disable은 TickComponent가 Timeline 종료 후 자동 수행
		OnTargetUnlocked.Broadcast(nullptr);
	}
}
