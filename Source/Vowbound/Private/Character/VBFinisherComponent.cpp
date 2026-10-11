// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Character/VBFinisherComponent.h"
#include "Character/VBFinisherApproachPolicy.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"

#include "ContextualAnimSceneActorComponent.h"
#include "ContextualAnimTypes.h"      // FContextualAnimSceneBindingContext / FContextualAnimSceneBindings
#include "ContextualAnimUtilities.h"  // BP_CreateContextualAnimSceneBindingsForTwoActors
#include "Animation/AnimMontage.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/VBInteractionAnimInterface.h"
#include "Animation/VBInteractionIK.h"
#include "Animation/VBAnimNotify_InteractionRagdoll.h"
#include "Animation/VBAnimNotifyState_InteractionWarp.h"
#include "Character/VBWeaponStateComponent.h"
#include "Types/VBWeaponStateTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBFinisherConfig.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier.h"
#include "Net/UnrealNetwork.h"        // DOREPLIFETIME
#include "PoseSearch/MultiAnimAsset.h"
#include "PoseSearch/PoseSearchDatabase.h"           // 불완전 타입이면 GetNameSafe 가 UObject* 로 변환을 못 한다
#include "PoseSearch/PoseSearchInteractionLibrary.h"  // MotionMatchMulti + FPoseSearchMotionMatchMultiQuery
#include "PoseSearch/PoseSearchLibrary.h"             // UPoseSearchLibrary::GetActor
#include "Vowbound/Vowbound.h"        // VB_LOG

// 클래스 하나에 붙는 델리게이트다. 구독자는 자기 월드인지 스스로 가른다(헤더 선언 주석).
FVBFinisherConfigArrived UVBFinisherComponent::OnConfigArrived;
TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UVBFinisherComponent>> UVBFinisherComponent::ParticipantOwners;

UVBFinisherComponent::UVBFinisherComponent()
{
	// 적을 제자리로 «나눠서» 데려오는 동안에만 돈다. 그래서 켤 수는 있게 두되 꺼진 채로 시작한다.
	//   bCanEverTick 이 false 면 SetComponentTickEnabled(true) 가 조용히 아무 일도 안 한다 -
	//   2026-09-13 에 실제로 그랬다. BeginAlign 은 로그까지 찍었는데 TickComponent 는 한 번도 안 불렸다.
	PrimaryComponentTick.bCanEverTick          = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// ActiveConfig 를 원격 클라로 보내기 위해서다. 컴포넌트가 복제 대상이 아니면 그 안의
	// Replicated 프로퍼티는 조용히 아무 데도 안 간다 - 헤더만 보면 되는 줄 알게 되는 자리다.
	SetIsReplicatedByDefault(true);
}

void UVBFinisherComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 조건을 걸지 않는다. 처형은 구경하는 클라에게도 보이는 연출이고, 한 번의 처형에 두 번 바뀌는
	// 포인터 하나라 대역폭이 문제될 자리가 아니다.
	DOREPLIFETIME(UVBFinisherComponent, ActiveConfig);
	DOREPLIFETIME(UVBFinisherComponent, InteractionReplay);
}

void UVBFinisherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(InteractionReplayRetryTimer);
	}
	FinishInteraction(true, false, true);
	Super::EndPlay(EndPlayReason);
}

void UVBFinisherComponent::OnRep_ActiveConfig()
{
	if (LocalInteractionSession != 0 && LocalInteractionSession == InteractionReplay.SessionId && InteractionReplay.bActive)
	{
		ActiveConfig = InteractionReplay.Config;
	}
	VB_LOG(Log, "처형 설정 복제 도착: %s (owner=%s)",
	       ActiveConfig ? *GetNameSafe(ActiveConfig) : TEXT("없음(장면 종료)"), *GetNameSafe(GetOwner()));

	// 「도착했다」를 알리는 것뿐이다. 여기서 연출을 «시작하지» 않는다 - 그 구분을 지워 두면
	// 다음 사람이 이 자리에 카메라를 얹고, 그러면 장면 컴포넌트가 연출을 아는 상태가 된다.
	// 듣는 쪽이 장면 시작까지 함께 기다렸다가 자기 판단으로 시작한다(둘의 도착 순서가 안 정해졌다).
	OnConfigArrived.Broadcast(GetOwner(), ActiveConfig);
}

const UVBFinisherConfig* UVBFinisherComponent::SelectFinisher(const AActor* Target) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Target)
	{
		return nullptr;
	}

	const UVBWeaponStateComponent* WSC = Owner->FindComponentByClass<UVBWeaponStateComponent>();
	if (!WSC)
	{
		return nullptr;
	}

	const TArray<TObjectPtr<UVBFinisherConfig>>* Configs = WSC->GetFinisherConfigs();
	if (!Configs || Configs->IsEmpty())
	{
		// 막을 수 없는 경우다 - 처형 자산이 아직 없는 무기가 정상적으로 여기 온다(규칙 27: 말밖에 못 하면 말한다).
		// 무기당 한 번만 남긴다. 처형 입력은 연타로 들어오므로 매번 찍으면 다른 계측을 덮는다.
		const uint8 Key = static_cast<uint8>(WSC->GetCurrentWeaponType());
		if (!WarnedEmptyWeapons.Contains(Key))
		{
			WarnedEmptyWeapons.Add(Key);
			VB_LOG(Warning, "SelectFinisher: 무기 %d 에 FinisherConfigs 가 비어 있다 - 처형이 안 걸린다. "
			                "BP_VBCharacter 의 WeaponDataMap 에 설정 자산을 배선하라.", Key);
		}
		return nullptr;
	}

	TArray<const UVBFinisherConfig*> Candidates;
	for (const TObjectPtr<UVBFinisherConfig>& Config : *Configs)
	{
		// null 항목은 저작 사고다. 건너뛰되 조용히 넘기지 않는다 - 그 자리가 비면 랜덤 폭이 줄어든다.
		if (!Config)
		{
			VB_LOG(Warning, "SelectFinisher: FinisherConfigs 에 빈 항목이 있다");
			continue;
		}
		if (Config->Scene && VBFinisherApproachPolicy::Matches(Config->Approach,
			Owner->GetActorLocation(), Target->GetActorLocation(), Target->GetActorForwardVector()))
		{
			Candidates.Add(Config);
		}
	}

	if (Candidates.IsEmpty())
	{
		return nullptr;
	}

	return Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
}

UContextualAnimSceneActorComponent* UVBFinisherComponent::FindSceneActorComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	// FindComponentByClass 를 쓴다 - 두 기초 클래스가 각자 다른 멤버 이름으로 들고 있고, 여기서
	// 그 이름들을 알 이유가 없다. 폰 타입에 묶이지 않는 것이 이 정적 질의의 존재 이유다.
	return const_cast<AActor*>(Actor)->FindComponentByClass<UContextualAnimSceneActorComponent>();
}

void UVBFinisherComponent::BeginAlign(AActor& Victim, const FTransform& Desired, float BlendTime)
{
	// 어긋남을 «지금 한 번» 잰다. 매 틱 다시 재면 루트모션이 만든 이동까지 어긋남으로 읽어
	//   장면 내내 적을 따라다니며 밀게 된다.
	const FVector StartLoc = Victim.GetActorLocation();
	const float   StartYaw = Victim.GetActorRotation().Yaw;

	PendingAlignActor  = &Victim;
	PendingAlignOffset = Desired.GetLocation() - StartLoc;

	// 회전은 yaw 만. pitch/roll 을 캡슐에 넣으면 기울어진 채로 선다.
	//   최단 경로로 돈다 - 350도 차이는 -10도로 읽어야 한 바퀴를 안 돈다.
	PendingAlignYaw  = FRotator::NormalizeAxis(Desired.Rotator().Yaw - StartYaw);
	PendingAlignTime = FMath::Max(BlendTime, 0.0f);

	if (PendingAlignTime <= KINDA_SMALL_NUMBER)
	{
		// 0 이면 그 자리에서 다 민다 - 종전과 같은 즉시 이동이다.
		Victim.AddActorWorldOffset(PendingAlignOffset, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		Victim.AddActorWorldRotation(FRotator(0.0f, PendingAlignYaw, 0.0f), /*bSweep=*/false, nullptr,
		                             ETeleportType::TeleportPhysics);
		PendingAlignActor = nullptr;
		VB_LOG(Log, "StartFinisher: 적을 즉시 데려왔다 (%.0fcm, %.0f도)",
		       PendingAlignOffset.Size(), PendingAlignYaw);
		return;
	}

	SetComponentTickEnabled(true);

	// 켜졌는지 확인하고 말한다. 안 켜지면 보정이 통째로 일어나지 않는데, 그 실패가
	//   「적이 안 따라온다」로만 보이고 로그에는 시작 줄만 남아 원인을 못 가린다.
	if (!IsComponentTickEnabled())
	{
		VB_LOG(Warning, "StartFinisher: 틱을 못 켰다 - 적을 그 자리에서 옮긴다 "
		                "(PrimaryComponentTick.bCanEverTick 을 확인해라)");
		Victim.AddActorWorldOffset(PendingAlignOffset, false, nullptr, ETeleportType::None);
		Victim.AddActorWorldRotation(FRotator(0.0f, PendingAlignYaw, 0.0f), false, nullptr,
		                             ETeleportType::None);
		PendingAlignActor = nullptr;
		return;
	}

	VB_LOG(Log, "StartFinisher: 적을 %.2f초에 걸쳐 데려온다 (%.0fcm, %.0f도)",
	       PendingAlignTime, PendingAlignOffset.Size(), PendingAlignYaw);
}

void UVBFinisherComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                         FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Victim = PendingAlignActor.Get();
	if (!Victim || PendingAlignTime <= 0.0f)
	{
		PendingAlignActor = nullptr;
		SetComponentTickEnabled(false);   // 할 일이 없는 틱은 켜 두지 않는다
		return;
	}

	// 남은 양 중 이번 프레임 몫만 «더한다». 절대 위치를 세팅하지 않는 이유는
	//   처형 몽타주의 루트모션이 적을 스스로도 움직이기 때문이다 - 세팅하면 그것을 지운다.
	const float Step = FMath::Clamp(DeltaTime / PendingAlignTime, 0.0f, 1.0f);

	const FVector MoveNow = PendingAlignOffset * Step;
	const float   TurnNow = PendingAlignYaw * Step;

	// 나눠 미는 구간은 «일반 이동» 이다. TeleportPhysics 로 밀면 원격 클라가 그것을 순간이동으로 받아
	//   보간이 매 프레임 끊긴다 - 부드럽게 하려고 나눈 것이 반대 결과를 낸다.
	//   즉시 이동(BlendTime 0)만 텔레포트가 맞다.
	Victim->AddActorWorldOffset(MoveNow, /*bSweep=*/false, nullptr, ETeleportType::None);
	Victim->AddActorWorldRotation(FRotator(0.0f, TurnNow, 0.0f), /*bSweep=*/false, nullptr,
	                              ETeleportType::None);

	PendingAlignOffset -= MoveNow;
	PendingAlignYaw    -= TurnNow;
	PendingAlignTime   -= DeltaTime;

	if (PendingAlignTime <= 0.0f)
	{
		// 남은 찌꺼기를 마지막에 털어 넣는다. 안 털면 매번 조금씩 어긋난 채로 끝난다.
		Victim->AddActorWorldOffset(PendingAlignOffset, /*bSweep=*/false, nullptr, ETeleportType::None);
		Victim->AddActorWorldRotation(FRotator(0.0f, PendingAlignYaw, 0.0f), /*bSweep=*/false, nullptr,
		                              ETeleportType::None);
		PendingAlignActor  = nullptr;
		PendingAlignOffset = FVector::ZeroVector;
		PendingAlignYaw    = 0.0f;
		SetComponentTickEnabled(false);
	}
}

bool UVBFinisherComponent::StartFinisher(const UVBFinisherConfig* Config, AActor* Target)
{
	bLastFinisherCancelled = false;
	AActor* Owner = GetOwner();
	if (!Owner || !Target || !Config || !Config->Scene)
	{
		return false;
	}

	// 씬 시작은 권위 행위다. 클라가 시작하면 두 액터의 상태가 갈린다.
	if (!Owner->HasAuthority())
	{
		return false;
	}

	if (IsPlaying())
	{
		// 이미 장면 안이다. 겹쳐 시작하면 플러그인 쪽 바인딩이 어긋난다.
		return false;
	}

	UContextualAnimSceneActorComponent* OwnerScene = FindSceneActorComponent(Owner);
	if (!OwnerScene || !FindSceneActorComponent(Target))
	{
		// 두 기초 클래스(AVBCharacter / AVBEnemyBase)의 생성자가 이것을 보장한다. 여기 걸렸다는 것은
		// 그 둘을 안 거치는 액터가 처형에 끼어들었다는 뜻이다 - 설정 오류이고 고칠 곳은 그 액터의 클래스다.
		VB_LOG(Warning, "StartFinisher: SceneActorComponent 없음 (owner=%s target=%s). "
		                "처형에 참여하는 액터는 AVBCharacter 또는 AVBEnemyBase 를 상속해야 한다",
		       *GetNameSafe(Owner), *GetNameSafe(Target));
		return false;
	}

	// 역할 배정은 씬 자산이 든다(PrimaryRole=Attacker). 여기서는 «누가 주역인가» 만 넘긴다.
	const FContextualAnimSceneBindingContext Primary(Owner);
	const FContextualAnimSceneBindingContext Secondary(Target);

	FContextualAnimSceneBindings Bindings;
	if (!UContextualAnimUtilities::BP_CreateContextualAnimSceneBindingsForTwoActors(
		Config->Scene, Primary, Secondary, Bindings))
	{
		// 씬 자산의 진입 조건(SelectionCriteria)에 안 맞으면 여기서 실패한다. 정상 경로다.
		VB_LOG(Log, "StartFinisher: 바인딩 생성 실패 - 씬 조건 불충족 (scene=%s)", *GetNameSafe(Config->Scene));
		return false;
	}

	// 클립이 기대하는 자리로 «적을» 데려온다. 플레이어는 자기가 선 자리에서 시작한다.
	//
	// 왜 적인가: 기준은 플레이어다 (2026-09-13 user 「캐릭터 위주로 잡아야지 몬스터의 회전이랑
	//   포지션은 캐릭터 기준으로 잡아야 해」). 이 자리의 종전 주석은 반대로 적고 있었는데
	//   (2026-09-11 「적이 미끄러지는 것은 결함으로 읽힌다」), user 가 움직이는 것을 보고 정했다.
	//   플레이어가 끌려가면 내가 조종하던 것이 남의 것이 된다 - 그쪽이 더 크게 거슬린다.
	// 왜 한 프레임에 안 옮기나: 적에는 MotionWarpingComponent 가 없어서(실측 2026-09-13)
	//   플러그인이 부드럽게 끌어줄 수가 없다. 그 몫을 AlignBlendTime 동안 우리가 나눠서 민다.
	// 어디로: 플러그인이 알려준다. 계산하지 않는다 - 오프셋은 씬 자산에 저작돼 있고
	//   그것을 우리가 다시 계산하면 자산을 고칠 때마다 코드도 고쳐야 한다.
	{
		const FContextualAnimSceneBinding* OwnerBinding  = Bindings.FindBindingByActor(Owner);
		const FContextualAnimSceneBinding* TargetBinding = Bindings.FindBindingByActor(Target);
		if (OwnerBinding && TargetBinding)
		{
			const FName OwnerRole  = Bindings.GetRoleFromBinding(*OwnerBinding);
			const FName TargetRole = Bindings.GetRoleFromBinding(*TargetBinding);

			// 장면 시작 시점(t=0)의 «피해자가 공격자에 대해 어디 서야 하는가».
			const FTransform RelToOwner = Config->Scene->GetAlignmentTransformForRoleRelativeToOtherRole(
				/*SectionIdx=*/0, /*AnimSetIdx=*/0, TargetRole, OwnerRole, /*Time=*/0.0f);

			// 역할 이름을 못 찾으면 플러그인이 조용히 항등을 돌려준다
			//   (엔진 소스 ContextualAnimSceneAsset.cpp ::GetAlignmentTransformForRoleRelativeToOtherRole 의 마지막 return).
			//   항등을 그대로 쓰면 적을 플레이어와 «같은 자리» 로 데려와 두 몸이 겹친다.
			// 막을 수 있는 자리이므로 막는다(규칙 27) - 자리 맞춤만 건너뛰고 장면은 종전대로 진행한다.
			if (RelToOwner.Equals(FTransform::Identity))
			{
				VB_LOG(Warning, "StartFinisher: 정렬 변환이 항등이다 - 역할 이름 불일치로 보인다 "
				                "(scene=%s roles=%s/%s). 자리 맞춤을 건너뛴다",
				       *GetNameSafe(Config->Scene), *OwnerRole.ToString(), *TargetRole.ToString());
			}
			else
			{
				BeginAlign(*Target, RelToOwner * Owner->GetActorTransform(), Config->AlignBlendTime);
			}
		}
	}

	// 바인딩은 여기서 다시 만들지 않는다. 아무도 «지금» 옮기지 않았기 때문이다 -
	//   적은 앞으로 몇 프레임에 걸쳐 온다. 종전에는 공격자를 즉시 옮겼으므로 다시 만들어야 했다.

	if (!OwnerScene->StartContextualAnimScene(Bindings))
	{
		VB_LOG(Warning, "StartFinisher: StartContextualAnimScene 실패 (scene=%s)", *GetNameSafe(Config->Scene));
		return false;
	}

	// 끝을 알아야 피해와 카메라 복귀가 걸린다. 중복 구독을 막으려고 AddUniqueDynamic 를 쓴다 -
	// 처형은 여러 번 걸리고 그때마다 구독하면 한 번 끝날 때 여러 번 발화한다.
	OwnerScene->OnLeftSceneDelegate.AddUniqueDynamic(this, &UVBFinisherComponent::HandleLeftScene);

	ActiveConfig = Config;

	// 서버에서도 손으로 부른다. 엔진은 OnRep_ 을 «값을 받은 쪽» 에서만 부르고, 값을 «넣은 쪽» 인
	// 서버에서는 안 부른다. 그래서 이 한 줄이 없으면 리슨 서버 호스트에서 OnConfigArrived 가
	// 한 번도 안 터지고 연출이 통째로 죽는다 - 싱글 플레이가 정확히 그 경우다.
	// 2026-09-13 PIE 로 실증: StartFinisher 는 2회 찍혔는데 도착 로그는 0회였다.
	OnRep_ActiveConfig();

	VB_LOG(Log, "StartFinisher: 시작 (scene=%s target=%s)", *GetNameSafe(Config->Scene), *GetNameSafe(Target));
	return true;
}

// ============================================================================
// GASP 5.8 경로 — chooser -> MotionMatchMulti -> 역할별 몽타주 + 모션워핑
// 설계·실측 근거: .sw/gasp58/finisher-interaction-wiring.md
// ============================================================================

namespace
{
	// 역할 이름. PSIA 자산의 Items[].Role 과 글자까지 같아야 한다 - 다르면 짝이 조용히 안 지어진다.
	// 우리 ContextualAnim 장면이 이미 같은 이름을 쓰고 있어서 개명이 필요 없었다
	//   (2026-09-19 실측: CAS_Exec_* 의 역할이 Attacker/Victim).
	const FName VBFinisherRoleAttacker(TEXT("Attacker"));
	const FName VBFinisherRoleVictim(TEXT("Victim"));

	// 액터의 «몸» 메시. 못 찾으면 nullptr - 만들지 않는다.
	//
	// 왜 FindComponentByClass 가 아닌가: 그것은 «처음 찾은» 스켈레탈 메시를 돌려준다. BP 가 무기·장신구로
	//   스켈레탈 메시를 하나라도 더 붙이면 그쪽이 걸릴 수 있고, 그러면 처형이 무기에 걸린다.
	//   캐릭터의 몸은 ACharacter 가 이름으로 들고 있으므로 그것을 먼저 묻는다.
	USkeletalMeshComponent* FindBodyMesh(const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return nullptr;
		}
		if (const ACharacter* Character = Cast<ACharacter>(Actor))
		{
			return Character->GetMesh();
		}
		return Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	}

	// 액터의 AnimInstance. 못 찾으면 nullptr - 만들지 않는다.
	UAnimInstance* FindAnimInstance(const AActor* Actor)
	{
		const USkeletalMeshComponent* Mesh = FindBodyMesh(Actor);
		return Mesh ? Mesh->GetAnimInstance() : nullptr;
	}

	// 상호작용 결과를 들 수 있는 AnimInstance 인가. 플레이어와 적이 형제 클래스라 인터페이스로 묻는다
	//   (이유는 IVBInteractionAnimInterface 헤더).
	IVBInteractionAnimInterface* FindInteractionAnim(const AActor* Actor)
	{
		return Cast<IVBInteractionAnimInterface>(FindAnimInstance(Actor));
	}

	// 짝을 «짓는» 쪽. chooser 와 Pose History 이름을 들어야 하므로 플레이어 AnimInstance 여야 한다.
	UVBAnimInstance* FindAttackerAnim(const AActor* Actor)
	{
		return Cast<UVBAnimInstance>(FindAnimInstance(Actor));
	}
}

void UVBFinisherComponent::GatherInteractionDatabases(TArray<UPoseSearchDatabase*>& OutDatabases, const AActor* Target) const
{
	OutDatabases.Reset();
	const UVBWeaponStateComponent* Weapons = GetOwner() ? GetOwner()->FindComponentByClass<UVBWeaponStateComponent>() : nullptr;
	const TArray<TObjectPtr<UVBFinisherConfig>>* Configs = Weapons ? Weapons->GetFinisherConfigs() : nullptr;
	if (Configs && !Configs->IsEmpty())
	{
		for (const UVBFinisherConfig* Config : *Configs)
		{
			if (Config && Config->InteractionDatabase && (!Target || VBFinisherApproachPolicy::Matches(Config->Approach,
				GetOwner()->GetActorLocation(), Target->GetActorLocation(), Target->GetActorForwardVector())))
			{
				OutDatabases.AddUnique(Config->InteractionDatabase);
			}
		}
		return; // 무기 전용 목록을 Fighter 공용 chooser와 섞지 않는다.
	}
	if (const UVBAnimInstance* Anim = FindAttackerAnim(GetOwner()))
	{
		Anim->EvaluateFinisherDatabases(OutDatabases);
	}
}

bool UVBFinisherComponent::HasInteractionFinisherData() const
{
	TArray<UPoseSearchDatabase*> Databases;
	GatherInteractionDatabases(Databases);
	return !Databases.IsEmpty();
}

const UVBFinisherConfig* UVBFinisherComponent::FindInteractionConfig(const UPoseSearchDatabase* Database) const
{
	const UVBWeaponStateComponent* Weapons = GetOwner() ? GetOwner()->FindComponentByClass<UVBWeaponStateComponent>() : nullptr;
	const TArray<TObjectPtr<UVBFinisherConfig>>* Configs = Weapons ? Weapons->GetFinisherConfigs() : nullptr;
	if (Configs)
	{
		for (const UVBFinisherConfig* Config : *Configs)
		{
			if (Config && Config->InteractionDatabase == Database)
			{
				return Config;
			}
		}
	}
	return nullptr;
}

bool UVBFinisherComponent::CanStartInteractionFinisher(const AActor* Target) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Target)
	{
		return false;
	}

	// 옛 경로와 같은 이유로 권한에서만 시작한다 - 두 액터의 상태가 갈리면 안 된다.
	if (!Owner->HasAuthority() || Target == Owner || IsPlaying() || IsActorInScene(Target))
	{
		return false;
	}

	// 공격자는 UVBAnimInstance(chooser·Pose History 이름을 든다), 피해자는 인터페이스만 있으면 된다.
	//   없으면 조용히 물러나 옛 경로로 간다 - 이행기에는 그쪽이 정상이므로 경고하지 않는다.
	const UVBAnimInstance* AttackerAnim = FindAttackerAnim(Owner);
	if (!AttackerAnim || !FindInteractionAnim(Target))
	{
		return false;
	}

	// 워핑 컴포넌트는 «있어야만» 자리가 맞는다. 없으면 클립은 도는데 두 몸이 어긋난 채로 돈다 -
	//   엔진은 그 경우에 아무 말도 안 하므로 여기서 막는다(규칙 27: 막을 수 있으면 막는다).
	if (!Owner->FindComponentByClass<UMotionWarpingComponent>() ||
	    !Target->FindComponentByClass<UMotionWarpingComponent>())
	{
		VB_LOG(Warning, "CanStartInteractionFinisher: MotionWarpingComponent 가 없다 "
		                "(attacker=%s victim=%s). 상호작용 처형은 그것으로 자리를 맞춘다",
		       *GetNameSafe(Owner), *GetNameSafe(Target));
		return false;
	}

	// Motion warping is consumed by CMC, not by montage playback itself.
	// MOVE_None still advances the montage but discards its root motion. Do not
	// enable movement here: death/ragdoll may intentionally own that state.
	for (const AActor* Participant : {Owner, Target})
	{
		const ACharacter* Character = Cast<ACharacter>(Participant);
		const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		const UAnimInstance* Anim = FindAnimInstance(Participant);
		const UPrimitiveComponent* Capsule = Movement ? Movement->UpdatedPrimitive : nullptr;
		const bool bConsumesRootMotion = Anim &&
			(Anim->RootMotionMode == ERootMotionMode::RootMotionFromMontagesOnly ||
			 Anim->RootMotionMode == ERootMotionMode::RootMotionFromEverything);
		if (!Movement || Movement->MovementMode == MOVE_None || !Movement->IsActive() ||
			!Movement->IsComponentTickEnabled() || !Capsule ||
			Capsule->Mobility != EComponentMobility::Movable || Capsule->IsSimulatingPhysics() || !bConsumesRootMotion)
		{
			VB_LOG(Warning, "InteractionFinisher blocked: actor=%s movementMode=%d active=%d tick=%d rootMotionMode=%d capsulePhysics=%d",
				*GetNameSafe(Participant), Movement ? static_cast<int32>(Movement->MovementMode) : -1,
				Movement && Movement->IsActive(), Movement && Movement->IsComponentTickEnabled(),
				Anim ? static_cast<int32>(Anim->RootMotionMode) : -1, Capsule && Capsule->IsSimulatingPhysics());
			return false;
		}
	}

	// chooser 미배선이거나 이 무기에 상호작용 처형이 없다 = 이 기능이 아직 안 켜진 것이다.
	TArray<UPoseSearchDatabase*> Databases;
	GatherInteractionDatabases(Databases, Target);
	FilterDatabasesBySkeleton(Databases, Owner, Target);
	return !Databases.IsEmpty();
}

void UVBFinisherComponent::FilterDatabasesBySkeleton(TArray<UPoseSearchDatabase*>& InOutDatabases,
                                                     const AActor* Attacker, const AActor* Victim)
{
	const UAnimInstance* AttackerAnim = FindAnimInstance(Attacker);
	const UAnimInstance* VictimAnim   = FindAnimInstance(Victim);

	InOutDatabases.RemoveAll([AttackerAnim, VictimAnim](const UPoseSearchDatabase* Database)
	{
		const bool bOk =
			UVBInteractionIKLibrary::DoesSkeletonMatchSchema(Database, VBFinisherRoleAttacker, AttackerAnim) &&
			UVBInteractionIKLibrary::DoesSkeletonMatchSchema(Database, VBFinisherRoleVictim,   VictimAnim);

		if (!bOk)
		{
			// 이 DB 는 우리 두 골격에 안 맞는다. 조용히 버리면 「왜 안 걸리지」가 되고,
			//   안 버리면 엔진이 뼈 인덱스로 크래시한다(2026-09-19 user PIE). 그래서 버리고 말한다.
			// DB 당 한 번만 남긴다 - 이 판정은 처형 입력마다 도는데 후보가 셋이라 연타에 로그가 죽는다
			//   (같은 파일 WarnedEmptyWeapons 와 같은 규약).
			static TSet<TWeakObjectPtr<const UPoseSearchDatabase>> Warned;
			if (!Warned.Contains(Database))
			{
				Warned.Add(Database);
				VB_LOG(Log, "상호작용 처형: %s 는 골격이 안 맞아 후보에서 뺀다 "
				            "(스키마가 선언한 역할 골격과 이 두 액터의 골격이 다르다)", *GetNameSafe(Database));
			}
		}
		return !bOk;
	});
}

bool UVBFinisherComponent::StartInteractionFinisher(AActor* Target)
{
	// 조건 판정의 홈은 하나다. 부르는 쪽이 「걸 수 있나」를 먼저 물어보고 오지만, 그 사이에 상태가
	//   바뀔 수 있으므로 여기서 다시 묻는다 - 묻는 식이 같은 함수라 둘이 어긋날 수 없다.
	if (!CanStartInteractionFinisher(Target))
	{
		return false;
	}

	AActor* Owner = GetOwner();
	UVBAnimInstance* AttackerAnim = FindAttackerAnim(Owner);
	UAnimInstance*   VictimAnim   = FindAnimInstance(Target);

	TArray<UPoseSearchDatabase*> Databases;
	GatherInteractionDatabases(Databases, Target);
	FilterDatabasesBySkeleton(Databases, Owner, Target);

	// 후보 DB 마다 «누가 어느 역할을 맡을 수 있는가» 를 달아 질의를 만든다.
	// 역할은 고정이다 - 공격자는 언제나 Attacker, 대상은 언제나 Victim. GASP 는 이것도 chooser 출력에
	//   담지만(행마다 다른 역할 조합) 우리 처형에는 그 자유도가 필요 없다. 필요해지는 날 늘린다.
	TArray<FPoseSearchMotionMatchMultiQuery> Queries;
	Queries.Reserve(Databases.Num());
	for (UPoseSearchDatabase* Database : Databases)
	{
		FPoseSearchMotionMatchMultiQuery& Query = Queries.AddDefaulted_GetRef();
		Query.Database = Database;

		FPoseSearchAnimContextRoles& AttackerRoles = Query.AnimContextsRoles.AddDefaulted_GetRef();
		AttackerRoles.AnimContext = AttackerAnim;
		AttackerRoles.Roles.Add(VBFinisherRoleAttacker);

		FPoseSearchAnimContextRoles& VictimRoles = Query.AnimContextsRoles.AddDefaulted_GetRef();
		VictimRoles.AnimContext = VictimAnim;
		VictimRoles.Roles.Add(VBFinisherRoleVictim);
	}

	// 이 검색은 지금 이 프레임에 답한다. 매 틱 발행하고 한 프레임 뒤에 받는
	//   MotionMatchInteraction 과 다른 함수다 - 버튼을 누른 그 프레임에 시작해야 하는 우리에게는
	//   이쪽이 맞다(2026-09-19: 처음에 발행식으로 지었다가 GASP 실측을 보고 갈아탔다).
	TArray<FPoseSearchBlueprintResult> Results;
	UPoseSearchInteractionLibrary::MotionMatchMulti(
		Queries, AttackerAnim->GetInteractionPoseHistoryName(), FPoseSearchContinuingProperties(), Results);

	// 공격자 몫을 먼저 찾는다. 그것이 없으면 짝이 안 지어진 것이므로 아무것도 시작하지 않는다.
	const FPoseSearchBlueprintResult* AttackerResult = Results.FindByPredicate(
		[Owner](const FPoseSearchBlueprintResult& R)
		{
			return R.bIsInteraction && R.SelectedAnim && UPoseSearchLibrary::GetActor(R) == Owner;
		});

	if (!AttackerResult)
	{
		VB_LOG(Log, "StartInteractionFinisher: 짝을 못 지었다 (DB %d개, 결과 %d개) - 피해 없이 취소",
		       Databases.Num(), Results.Num());
		return false;
	}

	const UMultiAnimAsset* Asset = Cast<UMultiAnimAsset>(AttackerResult->SelectedAnim);
	if (!Asset || Asset->GetNumRoles() != 2 || Results.Num() != 2)
	{
		return false;
	}

	FVBInteractionReplay Replay;
	Replay.SessionId = InteractionReplay.SessionId + 1;
	Replay.bActive = true;
	Replay.Asset = const_cast<UMultiAnimAsset*>(Asset);
	Replay.Database = const_cast<UPoseSearchDatabase*>(AttackerResult->SelectedDatabase.Get());
	Replay.Config = FindInteractionConfig(Replay.Database);
	Replay.RootTransforms = AttackerResult->ActorRootTransforms;
	Replay.RootBoneTransforms = AttackerResult->ActorRootBoneTransforms;
	Replay.BlendParameters = AttackerResult->BlendParameters;
	Replay.Actors.SetNum(2);
	Replay.StartTimes.SetNum(2);
	Replay.PlayRates.SetNum(2);
	Replay.SupersededMontagePlayIds.SetNum(2);
	for (const FPoseSearchBlueprintResult& Result : Results)
	{
		AActor* Actor = const_cast<AActor*>(UPoseSearchLibrary::GetActor(Result));
		if (!Replay.Actors.IsValidIndex(Result.RoleIndex) || Replay.Actors[Result.RoleIndex] ||
		    Result.SelectedAnim != Asset || (Actor != Owner && Actor != Target) ||
		    Result.Role != (Actor == Owner ? VBFinisherRoleAttacker : VBFinisherRoleVictim))
		{
			return false;
		}
		Replay.Actors[Result.RoleIndex] = Actor;
		Replay.StartTimes[Result.RoleIndex] = Result.SelectedTime;
		Replay.PlayRates[Result.RoleIndex] = Result.WantedPlayRate;
		if (const UVBAbilitySystemComponent* ASC = Cast<UVBAbilitySystemComponent>(
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor)))
		{
			Replay.SupersededMontagePlayIds[Result.RoleIndex] = ASC->GetMontagePlayInstanceId();
		}
	}

	if (!PlayInteractionLocally(Results, 0.0f))
	{
		return false;
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	Replay.ServerStartTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	InteractionReplay = MoveTemp(Replay);
	LocalInteractionSession = InteractionReplay.SessionId;
	ActiveConfig = InteractionReplay.Config;
	OnRep_ActiveConfig();
	Owner->ForceNetUpdate();
	VB_LOG(Log, "상호작용 처형: 두 역할 시작, 세션 %d", LocalInteractionSession);
	return true;
}

bool UVBFinisherComponent::PlayInteractionLocally(const TArray<FPoseSearchBlueprintResult>& Results,
                                                 float ElapsedSeconds)
{
	if (Results.Num() != 2 || !InteractionParticipants.IsEmpty())
	{
		return false;
	}
	TArray<AActor*> Actors;
	TArray<UAnimMontage*> Montages;
	TArray<float> Positions;
	TArray<UVBAbilitySystemComponent*> AbilitySystems;
	bool bHasOwner = false;
	for (const FPoseSearchBlueprintResult& Result : Results)
	{
		AActor* Actor = const_cast<AActor*>(UPoseSearchLibrary::GetActor(Result));
		UAbilitySystemComponent* BaseASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		UVBAbilitySystemComponent* ASC = Cast<UVBAbilitySystemComponent>(BaseASC);
		if (ASC && !GetOwner()->HasAuthority() && !InteractionReplay.SupersededMontagePlayIds.IsValidIndex(Result.RoleIndex))
		{
			return false;
		}
		UAnimInstance* Anim = FindAnimInstance(Actor);
		const UMultiAnimAsset* Asset = Cast<UMultiAnimAsset>(Result.SelectedAnim);
		UAnimMontage* Montage = Asset ? Cast<UAnimMontage>(Asset->GetAnimationAsset(Result.Role)) : nullptr;
		const TWeakObjectPtr<UVBFinisherComponent>* ExistingOwner = ParticipantOwners.Find(Actor);
		// Montage_Play 자체는 미러링을 지원하지 않는다. 검색 결과만 반사하면 포즈와 워프가 달라진다.
		if (!Actor || (BaseASC && !ASC) || !Anim || !FindInteractionAnim(Actor) || !Montage || Actors.Contains(Actor) ||
		    (ExistingOwner && ExistingOwner->IsValid()) || Result.bIsMirrored || Result.bLoop ||
		    !FMath::IsFinite(Result.WantedPlayRate) || Result.WantedPlayRate <= 0.0f ||
		    !FMath::IsFinite(Montage->RateScale) || Montage->RateScale <= 0.0f ||
		    !UVBInteractionIKLibrary::DoesSkeletonMatchSchema(Result.SelectedDatabase, Result.Role, Anim) ||
		    Result.ActorRootTransforms.Num() != 2 || Result.ActorRootBoneTransforms.Num() != 2 ||
		    Result.AnimContexts.Num() != 2 || !Result.AnimContexts.IsValidIndex(Result.RoleIndex))
		{
			VB_LOG(Warning, "상호작용 처형: 참가자/골격/재생 설정 검증 실패 (%s)", *GetNameSafe(Actor));
			return false;
		}
		const float Position = Result.SelectedTime + ElapsedSeconds * Result.WantedPlayRate * Montage->RateScale;
		const bool bReleasedRemoteVictim = !GetOwner()->HasAuthority() && Actor != GetOwner() && InteractionReplay.bVictimReleased;
		if (!FMath::IsFinite(Position) || Position < 0.0f ||
		    (Position >= Montage->GetPlayLength() && !bReleasedRemoteVictim))
		{
			return false;
		}
		if (Actor != GetOwner())
		{
			const bool bHasRagdollNotify = Montage->Notifies.ContainsByPredicate(
				[&Result](const FAnimNotifyEvent& Event)
				{
					return Cast<UVBAnimNotify_InteractionRagdoll>(Event.Notify) && Event.GetTriggerTime() >= Result.SelectedTime;
				});
			if (!bHasRagdollNotify)
			{
				VB_LOG(Warning, "상호작용 처형: %s 에 native InteractionRagdoll notify가 없다", *GetNameSafe(Montage));
				return false;
			}
		}
		bHasOwner |= Actor == GetOwner();
		Actors.Add(Actor);
		Montages.Add(Montage);
		AbilitySystems.Add(ASC);
		const bool bSkipReleasedPose = bReleasedRemoteVictim &&
		    (Position >= Montage->GetPlayLength() || FindBodyMesh(Actor)->IsAnySimulatingPhysics());
		Positions.Add(bSkipReleasedPose ? -1.0f : Position);
	}
	if (!bHasOwner)
	{
		return false;
	}

	++LocalGeneration;
	bInteractionVictimLanded = !GetOwner()->HasAuthority() && InteractionReplay.bVictimReleased;
	bLastFinisherCancelled = false;
	// 모든 전제를 확인한 뒤에만 결과/소유권을 설치한다. 재생 실패는 아래에서 전체 롤백한다.
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		InteractionParticipants.Add(Actors[Index]);
		InteractionMontages.Add(Montages[Index]);
		InteractionAbilitySystems.Add(AbilitySystems[Index]);
		InteractionMontageInstanceIds.Add(INDEX_NONE);
		AddedTickPrerequisite.Add(false);
		AddedOwnerCollisionIgnore.Add(false);
		AddedVictimCollisionIgnore.Add(false);
		ParticipantOwners.Add(Actors[Index], this);
		Actors[Index]->OnDestroyed.AddUniqueDynamic(this, &UVBFinisherComponent::HandleInteractionParticipantDestroyed);
		FindInteractionAnim(Actors[Index])->SetInteractionResult(Results[Index]);
	}
	const int32 StartGeneration = LocalGeneration;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		if (Positions[Index] < 0.0f) continue;
		const ACharacter* Character = Cast<ACharacter>(Actors[Index]);
		if (UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr)
		{
			Movement->StopMovementImmediately();
			Movement->ConsumeInputVector();
		}
	}
	for (int32 Index = 0; Index < AbilitySystems.Num(); ++Index)
	{
		UVBAbilitySystemComponent* ASC = AbilitySystems[Index];
		const uint8 PreviousPlayId = ASC ? (GetOwner()->HasAuthority() ? ASC->GetMontagePlayInstanceId() :
			InteractionReplay.SupersededMontagePlayIds[Results[Index].RoleIndex]) : 0;
		if (ASC && !ASC->AcquireExternalMontageOwnership(this, PreviousPlayId))
		{
			FinishInteraction(true, false, true);
			return false;
		}
		if (LocalGeneration != StartGeneration)
		{
			return false; // Stopping a previous GAS task may synchronously destroy/cancel a participant.
		}
	}
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		if (Positions[Index] < 0.0f)
		{
			continue; // 이미 물리로 전환된 원격 피해자의 몽타주를 다시 시작하지 않는다.
		}
		if (FindAnimInstance(Actors[Index])->Montage_Play(Montages[Index], Results[Index].WantedPlayRate,
		    EMontagePlayReturnType::MontageLength, Positions[Index], false) <= 0.0f)
		{
			FinishInteraction(true, false, true);
			return false;
		}
		if (FAnimMontageInstance* Instance = FindAnimInstance(Actors[Index])->GetActiveInstanceForMontage(Montages[Index]))
		{
			InteractionMontageInstanceIds[Index] = Instance->GetInstanceID();
		}
		const ACharacter* Character = Cast<ACharacter>(Actors[Index]);
		const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		VB_LOG(Log, "InteractionFinisher start: actor=%s role=%s movementMode=%d montage=%s position=%.3f",
			*GetNameSafe(Actors[Index]), *Results[Index].Role.ToString(),
			Movement ? static_cast<int32>(Movement->MovementMode) : -1,
			*Montages[Index]->GetPathName(), Positions[Index]);
	}
	USkeletalMeshComponent* OwnerMesh = FindBodyMesh(GetOwner());
	UPrimitiveComponent* OwnerRoot = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		AActor* Actor = Actors[Index];
		FOnMontageEnded EndDelegate;
		if (Actor == GetOwner())
		{
			EndDelegate.BindUObject(this, &UVBFinisherComponent::HandleInteractionMontageEnded, LocalGeneration);
		}
		else
		{
			EndDelegate.BindUObject(this, &UVBFinisherComponent::HandleInteractionVictimMontageEnded, LocalGeneration);
			if (OwnerMesh)
			{
				USkeletalMeshComponent* OtherMesh = FindBodyMesh(Actor);
				AddedTickPrerequisite[Index] = !OwnerMesh->PrimaryComponentTick.GetPrerequisites().Contains(
					FTickPrerequisite(OtherMesh, OtherMesh->PrimaryComponentTick));
				OwnerMesh->AddTickPrerequisiteComponent(OtherMesh);
			}
			if (UPrimitiveComponent* OtherRoot = Cast<UPrimitiveComponent>(Actor->GetRootComponent()); OwnerRoot && OtherRoot)
			{
				AddedOwnerCollisionIgnore[Index] = !OwnerRoot->GetMoveIgnoreComponents().Contains(OtherRoot);
				AddedVictimCollisionIgnore[Index] = !OtherRoot->GetMoveIgnoreComponents().Contains(OwnerRoot);
				OwnerRoot->IgnoreComponentWhenMoving(OtherRoot, true);
				OtherRoot->IgnoreComponentWhenMoving(OwnerRoot, true);
			}
		}
		UAnimInstance* Anim = FindAnimInstance(Actor);
		if (Positions[Index] < 0.0f)
		{
			continue;
		}
		Anim->Montage_SetEndDelegate(EndDelegate, Montages[Index]);
		FOnMontageBlendingOutStarted BlendDelegate;
		BlendDelegate.BindUObject(this, &UVBFinisherComponent::HandleInteractionBlendOut, LocalGeneration);
		Anim->Montage_SetBlendingOutDelegate(BlendDelegate, Montages[Index]);
	}
	return true;
}

void UVBFinisherComponent::OnRep_InteractionReplay()
{
	const bool bWasRetrying = GetWorld()->GetTimerManager().IsTimerActive(InteractionReplayRetryTimer);
	GetWorld()->GetTimerManager().ClearTimer(InteractionReplayRetryTimer);
	if (ReplayRetrySession != InteractionReplay.SessionId ||
	    (!bWasRetrying && LocalInteractionSession == 0 && FinishedInteractionSession != InteractionReplay.SessionId))
	{
		ReplayRetrySession = InteractionReplay.SessionId;
		// 늦은 relevancy 진입에도 수신 후 준비 시간을 준다. 서버 재생 경과와 다른 시계다.
		ReplayRetryDeadline = GetWorld()->GetTimeSeconds() + 2.0;
	}
	if (LocalInteractionSession != InteractionReplay.SessionId || !InteractionReplay.bActive)
	{
		FinishInteraction(true, false, true);
	}
	if (!InteractionReplay.bActive)
	{
		FinishedInteractionSession = InteractionReplay.SessionId;
		return;
	}
	if (InteractionReplay.bActive && FinishedInteractionSession != InteractionReplay.SessionId)
	{
		if (LocalInteractionSession == InteractionReplay.SessionId && InteractionReplay.bVictimReleased)
		{
			ApplyReplicatedVictimRelease();
		}
		TryReplayInteraction();
	}
}

void UVBFinisherComponent::TryReplayInteraction()
{
	if (!GetWorld() || !InteractionReplay.bActive || LocalInteractionSession == InteractionReplay.SessionId ||
	    FinishedInteractionSession == InteractionReplay.SessionId)
	{
		return;
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float Elapsed = FMath::Max(0.0, ServerNow - InteractionReplay.ServerStartTime);
	const UMultiAnimAsset* Asset = InteractionReplay.Asset;
	TArray<FPoseSearchBlueprintResult> Results;
	if (Asset && InteractionReplay.Database && Asset->GetNumRoles() == 2 &&
	    InteractionReplay.Actors.Num() == 2 && InteractionReplay.StartTimes.Num() == 2 &&
	    InteractionReplay.PlayRates.Num() == 2)
	{
		TArray<TObjectPtr<const UObject>> Contexts;
		for (AActor* Actor : InteractionReplay.Actors)
		{
			Contexts.Add(FindAnimInstance(Actor));
		}
		if (Contexts[0] && Contexts[1])
		{
			for (int32 Index = 0; Index < 2; ++Index)
			{
				FPoseSearchBlueprintResult& Result = Results.AddDefaulted_GetRef();
				Result.SelectedAnim = InteractionReplay.Asset;
				Result.SelectedDatabase = InteractionReplay.Database;
				Result.SelectedTime = InteractionReplay.StartTimes[Index];
				Result.WantedPlayRate = InteractionReplay.PlayRates[Index];
				Result.Role = Asset->GetRole(Index);
				Result.RoleIndex = Index;
				Result.bIsInteraction = true;
				Result.BlendParameters = InteractionReplay.BlendParameters;
				Result.ActorRootTransforms = InteractionReplay.RootTransforms;
				Result.ActorRootBoneTransforms = InteractionReplay.RootBoneTransforms;
				Result.AnimContexts = Contexts;
			}
		}
	}
	if (Results.Num() == 2 && PlayInteractionLocally(Results, Elapsed))
	{
		LocalInteractionSession = InteractionReplay.SessionId;
		ActiveConfig = InteractionReplay.Config;
		OnRep_ActiveConfig();
		if (InteractionReplay.bVictimReleased)
		{
			ApplyReplicatedVictimRelease();
		}
		return;
	}
	// Actor/AnimInstance가 아직 준비되지 않은 채널 순서를 허용하되 영구 재시도하지 않는다.
	if (GetWorld()->GetTimeSeconds() < ReplayRetryDeadline)
	{
		GetWorld()->GetTimerManager().SetTimer(InteractionReplayRetryTimer, this,
		    &UVBFinisherComponent::TryReplayInteraction, 0.05f, false);
	}
	else
	{
		VB_LOG(Warning, "상호작용 재생: 세션 %d 참가자가 준비되지 않았거나 이미 종료됨", InteractionReplay.SessionId);
	}
}

void UVBFinisherComponent::ApplyReplicatedVictimRelease()
{
	// notify 시각을 지나서 수신/재생한 클라이언트도 원본의 release blend-out을 적용한다.
	// 피해는 재방송하지 않으며, 중지 콜백보다 먼저 latch를 올린다.
	bInteractionVictimLanded = true;
	for (int32 Index = 0; Index < InteractionParticipants.Num(); ++Index)
	{
		AActor* Actor = InteractionParticipants[Index].Get();
		UAnimMontage* Montage = InteractionMontages[Index].Get();
		if (!Actor || Actor == GetOwner() || !Montage)
		{
			continue;
		}
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			if (const UVBAnimNotify_InteractionRagdoll* Notify = Cast<UVBAnimNotify_InteractionRagdoll>(Event.Notify))
			{
				Notify->StopReleasedMontage(FindAnimInstance(Actor), Montage, InteractionMontageInstanceIds[Index]);
				break;
			}
		}
		if (InteractionAbilitySystems.IsValidIndex(Index))
		{
			if (UVBAbilitySystemComponent* ASC = InteractionAbilitySystems[Index].Get())
			{
				ASC->ReleaseExternalMontageOwnership(this);
			}
		}
	}
}

bool UVBFinisherComponent::NotifyInteractionRagdoll(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, int32 MontageInstanceId)
{
	if (!Mesh)
	{
		return false;
	}
	if (const TWeakObjectPtr<UVBFinisherComponent>* Owner = ParticipantOwners.Find(Mesh->GetOwner()))
	{
		if (Owner->IsValid())
		{
			return Owner->Get()->HandleRagdollNotify(Mesh->GetOwner(), Animation, MontageInstanceId);
		}
	}
	return false;
}

bool UVBFinisherComponent::HandleRagdollNotify(AActor* Victim, UAnimSequenceBase* Animation, int32 MontageInstanceId)
{
	const int32 Index = InteractionParticipants.IndexOfByPredicate(
		[Victim](const TWeakObjectPtr<AActor>& Actor) { return Actor.Get() == Victim; });
	if (bFinishingInteraction || Victim == GetOwner() || Index == INDEX_NONE ||
	    !InteractionMontages.IsValidIndex(Index) || Animation != InteractionMontages[Index].Get() ||
	    MontageInstanceId == INDEX_NONE || InteractionMontageInstanceIds[Index] != MontageInstanceId)
	{
		return false;
	}
	UAnimInstance* Anim = FindAnimInstance(Victim);
	UAnimMontage* Montage = InteractionMontages[Index].Get();
	if (!Anim || !Anim->Montage_IsPlaying(Montage))
	{
		return false;
	}
	// 서버의 전환 복제가 로컬 notify보다 먼저 도착해도 같은 역할의 blend-out은 허용한다.
	// 위의 instance ID 검증 때문에 이전 세션의 notify에는 이 경로가 열리지 않는다.
	if (bInteractionVictimLanded)
	{
		return true;
	}
	// 피해에 따른 사망/몽타주 중단 콜백이 동기적으로 들어와도 이미 도달한 전환으로 취급한다.
	bInteractionVictimLanded = true;
	// The victim has left the paired animation. The attacker may keep playing,
	// but the victim's ASC must be free to play its get-up montage before that ends.
	if (InteractionAbilitySystems.IsValidIndex(Index))
	{
		if (UVBAbilitySystemComponent* ASC = InteractionAbilitySystems[Index].Get())
		{
			ASC->ReleaseExternalMontageOwnership(this);
		}
	}
	if (GetOwner()->HasAuthority())
	{
		InteractionReplay.bVictimReleased = true;
		GetOwner()->ForceNetUpdate();
		OnInteractionVictimLanded.Broadcast(ComputeVictimLaunch(Anim, Montage));
	}
	return true;
}

FVector UVBFinisherComponent::ComputeVictimLaunch(UAnimInstance* Anim, UAnimMontage* Montage) const
{
	USkeletalMeshComponent* Mesh = Anim ? Anim->GetSkelMeshComponent() : nullptr;
	if (!Mesh || !Montage)
	{
		return FVector::ZeroVector;
	}
	// 전환 직후의 저작 루트 운동만 읽는다. 미래 전체의 최고 속도를 임의로 고르지 않는다.
	const float Start = Anim->Montage_GetPosition(Montage);
	const float End = FMath::Min(Start + 0.05f, Montage->GetPlayLength());
	const float Rate = Anim->Montage_GetPlayRate(Montage) * Montage->RateScale;
	if (End <= Start || Rate <= 0.0f)
	{
		return FVector::ZeroVector;
	}
	FAnimExtractContext Context;
	Context.bExtractRootMotion = true;
	const FTransform Delta = Montage->ExtractRootMotionFromTrackRange(Start, End, Context);
	return Mesh->ConvertLocalRootMotionToWorld(Delta).GetTranslation() *
	       (Rate / (End - Start)) * InteractionRagdollLaunchScale;
}

void UVBFinisherComponent::HandleInteractionBlendOut(UAnimMontage* Montage, bool bInterrupted, int32 Generation)
{
	if (Generation != LocalGeneration || InteractionParticipants.IsEmpty())
	{
		return;
	}
	if (!bInterrupted || bFinishingInteraction || !GetOwner()->HasAuthority())
	{
		return;
	}
	for (int32 Index = 0; Index < InteractionMontages.Num(); ++Index)
	{
		if (InteractionMontages[Index].Get() == Montage &&
		    (InteractionParticipants[Index].Get() == GetOwner() || !bInteractionVictimLanded))
		{
			FinishInteraction(true, true, true);
			return;
		}
	}
}

void UVBFinisherComponent::HandleInteractionVictimMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Generation)
{
	if (Generation != LocalGeneration || InteractionParticipants.IsEmpty())
	{
		return;
	}
	if (!bFinishingInteraction && !bInteractionVictimLanded && GetOwner()->HasAuthority())
	{
		VB_LOG(Warning, "상호작용 피해자 종료: 전환 notify 미도달 (%s, 중단=%d)", *GetNameSafe(Montage), bInterrupted);
		FinishInteraction(true, true, true);
	}
}

void UVBFinisherComponent::HandleInteractionMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Generation)
{
	if (Generation != LocalGeneration || InteractionParticipants.IsEmpty())
	{
		return;
	}
	FinishInteraction(bInterrupted || !bInteractionVictimLanded, true, true);
}

void UVBFinisherComponent::HandleInteractionParticipantDestroyed(AActor* Actor)
{
	const bool bRetryLocalLoss = GetOwner() && !GetOwner()->HasAuthority() && Actor != GetOwner() &&
	    InteractionReplay.bActive && InteractionReplay.SessionId == LocalInteractionSession;
	FinishInteraction(true, true, true);
	if (bRetryLocalLoss)
	{
		// 클라이언트의 relevancy 이탈은 서버의 세션 취소가 아니다.
		FinishedInteractionSession = 0;
		ReplayRetryDeadline = GetWorld()->GetTimeSeconds() + 2.0;
		GetWorld()->GetTimerManager().SetTimer(InteractionReplayRetryTimer, this,
		    &UVBFinisherComponent::TryReplayInteraction, 0.05f, false);
	}
}

void UVBFinisherComponent::FinishInteraction(bool bCancelled, bool bBroadcast, bool bStopMontages)
{
	if (bFinishingInteraction || InteractionParticipants.IsEmpty())
	{
		return;
	}
	TGuardValue<bool> FinishingGuard(bFinishingInteraction, true);
	++LocalGeneration; // 이전 몽타주의 늦은 종료 콜백은 다음 세션을 건드리지 못한다.
	bLastFinisherCancelled = bCancelled;
	USkeletalMeshComponent* OwnerMesh = FindBodyMesh(GetOwner());
	UPrimitiveComponent* OwnerRoot = GetOwner() ? Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()) : nullptr;
	for (int32 Index = 0; Index < InteractionParticipants.Num(); ++Index)
	{
		AActor* Actor = InteractionParticipants[Index].Get();
		UAnimMontage* Montage = InteractionMontages.IsValidIndex(Index) ? InteractionMontages[Index].Get() : nullptr;
		if (UAnimInstance* Anim = FindAnimInstance(Actor); Anim && Montage)
		{
			FAnimMontageInstance* Instance = Anim->GetMontageInstanceForID(InteractionMontageInstanceIds[Index]);
			if (Instance)
			{
				Instance->OnMontageEnded.Unbind();
				Instance->OnMontageBlendingOutStarted.Unbind();
				if (bStopMontages && Instance == Anim->GetActiveInstanceForMontage(Montage))
				{
					Anim->Montage_Stop(InteractionEarlyOutBlendTime, Montage);
				}
			}
		}
		if (IVBInteractionAnimInterface* Anim = FindInteractionAnim(Actor))
		{
			Anim->SetInteractionResult(FPoseSearchBlueprintResult());
		}
		if (UMotionWarpingComponent* Warping = Actor ? Actor->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
		    Warping && Montage)
		{
			for (const FAnimNotifyEvent& Event : Montage->Notifies)
			{
				const UVBAnimNotifyState_InteractionWarp* Notify = Cast<UVBAnimNotifyState_InteractionWarp>(Event.NotifyStateClass);
				const URootMotionModifier_Warp* Modifier = Notify ? Cast<URootMotionModifier_Warp>(Notify->RootMotionModifier) : nullptr;
				if (Modifier)
				{
					Warping->RemoveWarpTarget(Modifier->WarpTargetName);
				}
			}
		}
		ParticipantOwners.Remove(InteractionParticipants[Index]);
		if (Actor)
		{
			Actor->OnDestroyed.RemoveDynamic(this, &UVBFinisherComponent::HandleInteractionParticipantDestroyed);
		}
		if (InteractionAbilitySystems.IsValidIndex(Index))
		{
			if (UVBAbilitySystemComponent* ASC = InteractionAbilitySystems[Index].Get())
			{
				ASC->ReleaseExternalMontageOwnership(this);
			}
		}
		if (!Actor || Actor == GetOwner())
		{
			continue;
		}
		if (OwnerMesh && FindBodyMesh(Actor) && AddedTickPrerequisite[Index])
		{
			OwnerMesh->RemoveTickPrerequisiteComponent(FindBodyMesh(Actor));
		}
		if (UPrimitiveComponent* OtherRoot = Cast<UPrimitiveComponent>(Actor->GetRootComponent()); OwnerRoot && OtherRoot)
		{
			if (AddedOwnerCollisionIgnore[Index])
			{
				OwnerRoot->IgnoreComponentWhenMoving(OtherRoot, false);
			}
			if (AddedVictimCollisionIgnore[Index])
			{
				OtherRoot->IgnoreComponentWhenMoving(OwnerRoot, false);
			}
		}
	}
	InteractionParticipants.Reset();
	InteractionMontages.Reset();
	InteractionAbilitySystems.Reset();
	InteractionMontageInstanceIds.Reset();
	AddedTickPrerequisite.Reset();
	AddedOwnerCollisionIgnore.Reset();
	AddedVictimCollisionIgnore.Reset();
	FinishedInteractionSession = LocalInteractionSession;
	LocalInteractionSession = 0;
	ActiveConfig = nullptr;
	OnRep_ActiveConfig();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		InteractionReplay.bActive = false;
		GetOwner()->ForceNetUpdate();
		if (bBroadcast)
		{
			OnFinisherEnded.Broadcast();
		}
	}
}

bool UVBFinisherComponent::TransitionSection(FName SectionName)
{
	AActor* Owner = GetOwner();
	if (!Owner || !IsPlaying())
	{
		return false;
	}

	UContextualAnimSceneActorComponent* Scene = FindSceneActorComponent(Owner);
	return Scene && Scene->TransitionContextualAnimScene(SectionName);
}

void UVBFinisherComponent::EarlyOut()
{
	AActor* Owner = GetOwner();
	if (!Owner || !IsPlaying())
	{
		return;
	}

	// 새 경로는 취소 즉시 세션을 닫는다. 블렌드아웃 중의 notify도 더 이상 피해를 주지 못한다.
	if (!InteractionParticipants.IsEmpty())
	{
		FinishInteraction(true, true, true);
		return;
	}

	if (UContextualAnimSceneActorComponent* Scene = FindSceneActorComponent(Owner))
	{
		// 둘 다 멈춘다. 한쪽만 멈추면 피해자가 처형 자세로 남는다.
		// 상태 정리와 델리게이트 발화는 여기서 하지 않는다 - 플러그인이 OnLeftScene 을 부르고
		// 그것을 HandleLeftScene 이 받는다. 여기서도 정리하면 끝나는 자리가 둘이 된다.
		Scene->EarlyOutContextualAnimScene(/*bStopEveryone=*/true);
	}
}

void UVBFinisherComponent::HandleLeftScene(UContextualAnimSceneActorComponent* SceneActorComponent)
{
	if (SceneActorComponent)
	{
		SceneActorComponent->OnLeftSceneDelegate.RemoveDynamic(this, &UVBFinisherComponent::HandleLeftScene);
	}

	// 상태를 먼저 비운다. 구독자가 IsPlaying() 을 물었을 때 「아직 도는 중」이면 안 된다.
	ActiveConfig = nullptr;

	// 시작 때와 같은 이유로 손으로 부른다(위 StartFinisher 주석). 이게 없으면 호스트에서
	// 연출이 시작은 돼도 끝나지 않아 카메라가 그 자리에 남는다.
	OnRep_ActiveConfig();

	OnFinisherEnded.Broadcast();
}

bool UVBFinisherComponent::IsActorInScene(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	// 플러그인 상태를 진실원으로 쓴다. 우리 쪽 플래그를 따로 들면 둘이 어긋날 수 있고,
	// 어긋나는 순간 «끝났는데 안 끝났다고 믿는» 상태가 된다.
	const UContextualAnimSceneActorComponent* Scene = FindSceneActorComponent(Actor);
	if (Scene && Scene->IsInActiveScene())
	{
		return true;
	}

	// 새 경로(GASP 상호작용)에는 «장면» 이 없다. 진실원은 그 액터의 AnimInstance 가 든 결과다 -
	//   시작할 때 심고 끝날 때 비우므로 살아 있는 동안만 참이다.
	//
	// 왜 이 함수가 둘을 다 알아야 하나: 이것은 「이 액터가 지금 처형 안에 있나」를 묻는 유일한 정적
	//   질의이고, 부르는 쪽들은 어느 경로인지 모른다. 2026-09-19 user PIE 에서 그 구멍이 드러났다 -
	//   `UVBGA_FinisherWindow::OnWindowExpired` 가 이 함수로 «장면 중이면 창을 닫지 말라» 를 판정하는데
	//   새 경로에서 false 가 나와 처형 도중에 창이 닫혔다. 그러면 State.Combat.FinisherWindow 에
	//   걸려 있던 적 BT 게이트가 풀려 적이 날아가다 말고 제 로코모션(Idle)으로 돌아갔다.
	//   그 함수의 주석이 이미 같은 증상을 적고 있다(2026-09-11) - 경로가 하나 늘었는데 판정이 안 따라왔다.
	if (const IVBInteractionAnimInterface* InteractionAnim = FindInteractionAnim(Actor))
	{
		const FPoseSearchBlueprintResult& Result = InteractionAnim->GetInteractionResult();
		return Result.bIsInteraction && Result.SelectedAnim != nullptr;
	}

	return false;
}

bool UVBFinisherComponent::IsPlaying() const
{
	// 경로가 둘이라 진실원도 둘이다 - 옛 경로는 플러그인 장면, 새 경로는 우리 참가자 목록이다.
	// 여기서 합쳐 두는 이유: 부르는 쪽(어빌리티·연출)은 «처형 중인가» 만 알면 되고, 어느 경로인지는
	//   알 필요가 없다. 분기를 밖으로 새게 하면 이행이 끝날 때 고칠 곳이 그만큼 늘어난다.
	return IsActorInScene(GetOwner()) || !InteractionParticipants.IsEmpty();
}

AActor* UVBFinisherComponent::GetParticipant(FName Role) const
{
	if (!InteractionParticipants.IsEmpty())
	{
		for (const TWeakObjectPtr<AActor>& Participant : InteractionParticipants)
		{
			AActor* Actor = Participant.Get();
			if (Actor && Role == (Actor == GetOwner() ? VBFinisherRoleAttacker : VBFinisherRoleVictim))
			{
				return Actor;
			}
		}
		return nullptr;
	}
	const UContextualAnimSceneActorComponent* Scene = FindSceneActorComponent(GetOwner());
	const FContextualAnimSceneBinding* Binding = Scene && Scene->IsInActiveScene()
		? Scene->GetBindings().FindBindingByRole(Role) : nullptr;
	return Binding ? Binding->GetActor() : nullptr;
}

void UVBFinisherComponent::GetParticipants(TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	if (!InteractionParticipants.IsEmpty())
	{
		for (const TWeakObjectPtr<AActor>& Participant : InteractionParticipants)
		{
			if (AActor* Actor = Participant.Get()) OutActors.Add(Actor);
		}
		return;
	}
	const UContextualAnimSceneActorComponent* Scene = FindSceneActorComponent(GetOwner());
	if (Scene && Scene->IsInActiveScene())
	{
		for (const FContextualAnimSceneBinding& Binding : Scene->GetBindings())
		{
			if (AActor* Actor = Binding.GetActor()) OutActors.Add(Actor);
		}
	}
}

float UVBFinisherComponent::GetPlaybackTime() const
{
	const int32 Index = InteractionParticipants.IndexOfByPredicate(
		[this](const TWeakObjectPtr<AActor>& Actor) { return Actor.Get() == GetOwner(); });
	if (Index != INDEX_NONE && InteractionMontages.IsValidIndex(Index))
	{
		const UAnimInstance* Anim = FindAnimInstance(GetOwner());
		return Anim ? Anim->Montage_GetPosition(InteractionMontages[Index].Get()) : -1.0f;
	}
	const UContextualAnimSceneActorComponent* Scene = FindSceneActorComponent(GetOwner());
	const FContextualAnimSceneBinding* Binding = Scene && Scene->IsInActiveScene()
		? Scene->GetBindings().FindBindingByActor(GetOwner()) : nullptr;
	return Binding ? Binding->GetAnimMontageTime() : -1.0f;
}
