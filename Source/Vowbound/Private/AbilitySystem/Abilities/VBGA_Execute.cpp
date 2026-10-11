// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_Execute.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"

#include "AbilitySystemComponent.h"  // CancelAbilities - 진행 중인 내 공격 끊기
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/VBEnemyAnimInstance.h"
#include "Character/VBFinisherComponent.h"
#include "Data/VBFinisherConfig.h"
#include "Enemy/VBEnemyBase.h" // KeepRagdollMomentum - 던져진 래그돌은 운동량을 지우지 않는다
#include "GameFramework/Actor.h"
#include "Vowbound/Vowbound.h" // VB_LOG

UVBGA_Execute::UVBGA_Execute()
{
	InputTag = VBGameplayTags::Input_Execute;

	// 처형이 도는 동안 공격을 막는다.
	//
	// 왜 필요한가: 피해자 쪽은 UVBGA_FinisherWindow 가 진행 중인 공격을 취소하고 신규를 막는데,
	//   공격자 쪽에는 그 대응이 없었다. 연출 갈래는 어빌리티를 끝내지 않고 장면이 끝날 때까지 살아 있으므로,
	//   그 사이 플레이어가 공격을 누르면 그 몽타주가 처형 장면 위에 겹친다 -
	//   2026-09-11 에 피해자 쪽에서 고친 「애니가 겹친다」의 공격자 판이다(같은 날 /verify 적발).
	// 즉시 갈래에서도 무해하다: 그쪽은 같은 프레임에 EndAbility 로 끝나 차단이 곧바로 풀린다.
	// 부모 하나로 Ability.Attack.* 전부가 걸린다 - 엔진이 후보 GA 의 AssetTags 를 부모까지 확장해 비교한다.
	BlockAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Attack);

	// 회피·가드도 막는다. 부모 하나로 Ability.Defense.* 전부가 걸린다.
	// 가드 몽타주(AVBCharacter::BeginGuard)는 UVBGA_Guard 가 부르므로 GA 를 막으면 같이 막힌다.
	BlockAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Defense);

	// 「지금 처형을 걸고 있다」를 알린다. 피해자 쪽 State.Combat.FinisherWindow 와 짝이다.
	//
	// user 결정(2026-09-13): 「끊기지 않아야돼 그리고 다른 애들은 때릴 수 없어야돼. '처형 중'이니까」.
	// 이 태그가 그 둘을 한 곳에서 낸다 - 반응 GA 가 자기를 막는 데 쓰고(피격·경직), 피해 계산이
	//   무적으로 읽고, 비-GA 진입점(무기 교체·트래버설)이 IsActorFinishing 으로 묻는다.
	// 왜 SuperArmor 재사용이 아닌가: 그것은 「안 비틀거린다」이지 무적이 아니고, 적 공격 GA 가 이미
	//   자기 용도로 붙였다 뗀다. 한 태그에 주인이 둘이면 한쪽이 뗄 때 다른 쪽이 풀린다(규칙 26).
	// 즉시 갈래(처형 자산이 없는 무기)에서도 무해하다 - 같은 프레임에 EndAbility 로 끝나 태그도 같이 사라진다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Finishing);

	// 공격 버튼으로도 들어온다. 전용 키(InputTag) 는 그대로 남는다 - 두 진입이 같은 어빌리티로
	// 모이므로 주인이 둘이 되지 않고, 되돌리기가 이 블록 삭제 한 번이다.
	//
	// 생산자는 UVBGA_MeleeAttackBase 의 bCanOpenFinisher 가 켜진 것뿐이고 지금은 경공격 하나다.
	// 자격 판정은 여기가 한다 - 공격 쪽은 사거리도 대상도 모른 채 이벤트만 쏘고,
	//   FindMedicalTarget 이 실패하면 이 어빌리티가 그냥 끝나 공격이 평소대로 이어진다.
	//   그렇게 두는 이유: 사거리가 두 곳에 있으면 어긋나는 날 「가끔 안 걸린다」가 된다.
	FAbilityTriggerData ExecuteTrigger;
	ExecuteTrigger.TriggerTag    = VBGameplayTags::Event_Combat_ExecuteRequested;
	ExecuteTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(ExecuteTrigger);
}

void UVBGA_Execute::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                   const FGameplayAbilityActorInfo*     ActorInfo,
                                   const FGameplayAbilityActivationInfo ActivationInfo,
                                   const FGameplayEventData*            TriggerEventData)
{
	// 원격 클라는 아무것도 하지 않는다.
	//
	// 여기서 아래 Super 로 떨어지면 부모가 EndAbility(bReplicateEndAbility=true) 를 부르고,
	//   그것이 ServerEndAbility 로 올라가 서버에서 돌고 있는 처형을 끝낸다
	//   (bServerRespectsRemoteAbilityCancellation 기본값 true). 그러면 두 몸은 장면을 계속 도는데
	//   HandleFinisherEnded 가 영영 안 불려 즉사 피해도 명성도 하나도 안 들어간다.
	// 호스트(listen server)에서는 IsNetAuthority 가 true 라 이 경로가 안 열린다 - PIE 로는 못 잡는다
	//   (2026-09-11 /verify 가 엔진 소스 사슬로 적발).
	// 클라가 할 일이 없는 것이 맞다: 대상 찾기·장면 시작·GE 적용이 전부 서버 몫이고,
	//   종료는 서버의 EndAbility 가 복제해 온다.
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	AActor* Avatar = GetAvatarActorFromActorInfo();
	UVBFinisherComponent* Finisher = Avatar ? Avatar->FindComponentByClass<UVBFinisherComponent>() : nullptr;

	if (Finisher)   // 권위는 위에서 이미 걸렀다
	{
		// 대상 조건(빈사)은 부모의 찾기가 든다. 여기서 다시 판정하면 조건이 두 곳에 산다.
		AActor* const Target = FindMedicalTarget(ActorInfo);

		// 여기부터 두 갈래가 조용히 실패한다. 그러면 화면에서 「F 를 눌렀는데 아무 일도 없다」 하나로만
		// 보이고, 「대상을 못 찾았다」와 「그 무기에 맞는 처형이 없다」가 구별이 안 된다.
		// 2026-09-13 에 실제로 못 갈랐다 - 감염체에서 처형이 0회인데 StartFinisher 실패 로그도 0줄이라
		//   그 앞에서 빠진 것까지만 알고 어디서인지는 몰랐다. 막을 수 있는 자리가 아니므로 말한다(규칙 27).
		if (!Target)
		{
			VB_LOG(Log, "%s: 처형 대상 없음 - 락온 대상도 전방 원뿔도 «처형 창에 든 적» 을 못 찾았다",
			       *GetName());
		}

		if (Target)
		{
			if (UVBFinisherComponent::IsActorInScene(Target))
			{
				EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
				return;
			}
			// 새 경로의 배선이 없는 무기에는 기존 contextual 경로를 유지한다.
			// 왜 앞인가: 둘 다 걸 수 있는 상황이면 새 경로가 이겨야 이행이 진행된다. 뒤에 두면
			//   옛 자산이 남아 있는 한 새 경로가 영영 안 돈다.
			if (Finisher->HasInteractionFinisherData())
			{
				if (!Finisher->CanStartInteractionFinisher(Target))
				{
					EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
					return;
				}
				CancelOwnAttacks();
				bFinisherActive = true;
				bFinisherUsedInteraction = true;
				bFinisherDamageApplied = false;
				bFinisherVictimEnteredRecoverableRagdoll = false;
				FinisherTarget = Target;
				FinisherConfig.Reset();
				Finisher->OnFinisherEnded.AddUniqueDynamic(this, &UVBGA_Execute::HandleFinisherEnded);
				// 피해자 클립이 공격자보다 먼저 끝난다. 피해를 그 시각에 줘야 사이에 일어서지 않는다.
				Finisher->OnInteractionVictimLanded.AddUniqueDynamic(
					this, &UVBGA_Execute::HandleInteractionVictimLanded);

				if (Finisher->StartInteractionFinisher(Target))
				{
					FinisherConfig = Finisher->GetActiveConfig();
					// 새 경로는 처형별 FinisherConfig 대신 피해자의 RagdollConfig 가
					// 살아남았을 때의 물리 전환과 기상 후보 몽타주를 소유한다.
					VB_LOG(Log, "%s: 상호작용 처형 시작 (target=%s)", *GetName(), *GetNameSafe(Target));
					return;
				}

				// 짝을 못 지었다. 두 구독을 되돌리고 피해 없이 종료한다.
				Finisher->OnFinisherEnded.RemoveDynamic(this, &UVBGA_Execute::HandleFinisherEnded);
				Finisher->OnInteractionVictimLanded.RemoveDynamic(
					this, &UVBGA_Execute::HandleInteractionVictimLanded);
				// 참가자 재생 실패를 즉사/옛 연출로 바꾸지 않는다. 실패는 피해 없는 취소다.
				EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
				return;
			}

			const UVBFinisherConfig* Config = Finisher->SelectFinisher(Target);
			if (!Config)
			{
				VB_LOG(Log, "%s: %s 에 걸 처형이 없다 - 지금 무기의 FinisherConfigs 에 이 접근 방향 것이 없다. "
				            "즉시 갈래로 간다", *GetName(), *GetNameSafe(Target));
			}

			if (Config)
			{
				// 장면 시작 «전» 에 끊는다. 뒤에 끊으면 처형 몽타주가 이미 깔린 위에서 취소가 돌아
				//   그 정리가 새 몽타주까지 건드릴 수 있다. 왜 끊어야 하는지는 CancelOwnAttacks 주석.
				CancelOwnAttacks();

				// 끝을 먼저 구독한다. 시작 직후 끝나는 경우(클립이 없거나 즉시 이탈)에도 놓치지 않는다.
				Finisher->OnFinisherEnded.AddUniqueDynamic(this, &UVBGA_Execute::HandleFinisherEnded);

				if (Finisher->StartFinisher(Config, Target))
				{
					bFinisherActive = true;
					FinisherTarget  = Target;
					FinisherConfig  = Config;
					VB_LOG(Log, "%s: 처형 연출 시작 (target=%s)", *GetName(), *GetNameSafe(Target));
					// 어빌리티를 끝내지 않는다. 장면이 끝날 때 HandleFinisherEnded 가 마무리한다.
					return;
				}

				// 시작 실패 - 구독을 되돌린다. 안 그러면 다음 처형의 끝에 이 어빌리티가 반응한다.
				Finisher->OnFinisherEnded.RemoveDynamic(this, &UVBGA_Execute::HandleFinisherEnded);
			}
		}
	}

	// 연출이 없거나 못 걸린 경우 - 부모의 즉시 처치 그대로다. 자산이 없는 무기에서 기능이 사라지지 않는다.
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UVBGA_Execute::CancelOwnAttacks()
{
	// 진행 중인 «내» 공격을 끊는다. 생성자의 BlockAbilitiesWithTag 는 신규 활성화만 막는다 -
	//   이미 도는 스윙의 몽타주는 그대로 살아서 처형 몽타주를 덮는다.
	// 증상이 비대칭이라 찾기 어렵다: 피해자는 처형 애니를 도는데 공격자만 안 돈다.
	//   그리고 공격 중에 F 를 눌렀을 때만 나므로 「어쩔 때는」으로 보인다(2026-09-13 user).
	// 2026-09-11 에 피해자 쪽(UVBGA_FinisherWindow)에 같은 취소를 넣었는데 공격자 쪽을 빠뜨렸다 -
	//   한쪽만 고친 편집이었다. 대조: 그쪽 CancelAbilities 1건 / 이쪽 0건이었다.
	// 경로가 둘이 되면서 함수로 뺐다(2026-09-19) - 같은 취소를 두 곳에 적으면 한쪽만 고치는 날이 온다.
	if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackTags;
		AttackTags.AddTag(VBGameplayTags::Ability_Attack);
		OwnerASC->CancelAbilities(&AttackTags, nullptr, this);
	}
}

void UVBGA_Execute::HandleInteractionVictimLanded(FVector LaunchVelocity)
{
	if (bFinisherDamageApplied)
	{
		return;
	}

	AActor* const Target = FinisherTarget.Get();
	if (!Target)
	{
		// 대상이 사라졌다. 피해를 줄 데가 없으니 조용히 넘어가되, 다음 단계가 «이미 줬다» 로
		//   읽지 않게 표시는 세우지 않는다 - HandleFinisherEnded 가 한 번 더 시도할 자격을 남긴다.
		VB_LOG(Warning, "%s: 피해자 착지 신호가 왔는데 대상이 없다", *GetName());
		return;
	}

	// 피해를 주기 «전» 에 표시를 세운다. 피해가 곧바로 사망 -> 래그돌 -> 정착 예약으로 이어지므로
	//   뒤에 세우면 이미 지워진 뒤다.
	// 왜 필요한가: 피해자 클립은 착지까지 안 담는다 - 끝나는 순간에도 날고 있고(실측 클립 끝 root 속도
	//   96~217 cm/s) 그 운동량이 나머지 비행이다. 사망 경로의 기본값은 그것을 지우는 것이라
	//   그대로 두면 공중에서 멈춘다(2026-09-19 user PIE).
	if (AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(Target))
	{
		Enemy->KeepRagdollMomentum(LaunchVelocity);
	}

	bFinisherDamageApplied = true;
	ApplyMedicalEffects(GetCurrentActorInfo(), Target);
	if (AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(Target))
	{
		const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
		if (!TargetASC || !UVBAbilitySystemStatics::IsDead(TargetASC))
		{
			// 불사 더미/피해 실패에는 물리 전환 예약이 다음 사망까지 남지 않는다.
			Enemy->ClearPendingInteractionRagdoll();
			// The authored interaction releases the victim mid-flight. Let physics carry
			// the remainder, then GA_GetUp takes over when the body settles.
			bFinisherVictimEnteredRecoverableRagdoll = Enemy->StartRecoverableRagdoll(LaunchVelocity, nullptr);
		}
	}

	VB_LOG(Log, "%s: 피해자 착지 - 피해 적용 (%s), 발사 속도 %.0f cm/s. 공격자 클립은 계속 돈다",
	       *GetName(), *GetNameSafe(Target), LaunchVelocity.Size());
}

void UVBGA_Execute::HandleFinisherEnded()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	const UVBFinisherComponent* Finisher = Avatar ? Avatar->FindComponentByClass<UVBFinisherComponent>() : nullptr;
	if (bFinisherUsedInteraction && (!Finisher || Finisher->WasLastFinisherCancelled() || !bFinisherDamageApplied))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, true);
		return;
	}

	// 피해는 여기서 준다. 장면이 끝난 뒤라 대상이 처형 자세를 다 마치고 쓰러진다.
	// 부모와 같은 «적용» 경로를 쓰므로 즉시 갈래와 연출 갈래의 피해·명성 규칙이 갈리지 않는다.
	// 대상은 다시 찾지 않는다 - 시작할 때 잡은 것을 쓴다(헤더 FinisherTarget 주석 참조).
	//
	// 상호작용 갈래는 이미 피해자 착지에서 줬다. 두 번 주면 명성도 두 번 깎이므로 한 번만 센다.
	//   옛 경로는 착지 신호가 없으므로 이 표시가 서지 않고 여기서 준다 - 분기를 늘리지 않아도 된다.
	AActor* const Target = FinisherTarget.Get();
	if (!bFinisherDamageApplied)
	{
		bFinisherDamageApplied = true;
		ApplyMedicalEffects(ActorInfo, Target);
	}

	// 캡슐을 «몸이 실제로 그려져 있는 자리» 로 데려온다. 장면이 끝난 «직후» 여야 한다 -
	//   지금 메시에 남아 있는 것이 처형의 마지막 자세이고, 다음 몽타주가 깔리는 순간 사라진다.
	//
	// 왜 필요한가: 처형 피해자 클립은 루트모션이 꺼져 있는데 루트가 로컬 Y 로 138cm 이동한다
	//   (실측 AM_Exec_SSH_02_B). 그 이동이 캡슐이 아니라 포즈에만 실리므로, 장면이 끝나면
	//   몸은 캡슐에서 1.38m 떨어진 곳에 있다. 다음 몽타주는 첫 프레임에 루트가 원점이라
	//   그 순간 몸이 캡슐 자리로 되돌아온다 - 그것이 화면에서 튀는 것으로 보인다.
	// 생존자의 위치·회전 보정은 새 기상 포즈가 평가될 때 함께 적용해야 한다.
	// 각도를 손으로 적지 않는 이유: 어긋난 양은 클립이 정하는 것이고 처형마다 다르다.
	//   재서 쓰면 여덟 벌에 숫자를 여덟 개 두지 않아도 된다.
	// ApplyMedicalEffects가 동기적으로 사망/래그돌을 시작할 수 있다. 물리 바디는 이미
	// 마지막 포즈의 월드 위치에 있으므로 그 뒤 캡슐을 TeleportPhysics로 옮기면 몸도 두 번 이동한다.
	// 사망·물리 대상의 좌표는 래그돌 추종 경로가 맡는다.
	// 새 경로에서는 하지 않는다 - 모션워핑이 장면 내내 캡슐째로 끌고 왔으므로 갚을 빚이 없다.
	//   그래도 밀면 두 번 민 것이 되어 끝에서 대상이 한 번 튄다(그 증상이 정확히 이 보정이 고치려던 것이라
	//   원인을 여기로 다시 찾아오게 된다).
	if (USkeletalMeshComponent* VictimMesh = (Target && !bFinisherUsedInteraction)
	                                             ? Target->FindComponentByClass<USkeletalMeshComponent>()
	                                             : nullptr)
	{
		const UAbilitySystemComponent* VictimASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
		const bool bVictimDead = VictimASC && UVBAbilitySystemStatics::IsDead(VictimASC);
		const bool bVictimSimulating = VictimMesh->IsAnySimulatingPhysics();
		const bool bGetUpOwnsHandoff = VictimASC && !bVictimDead &&
			FinisherConfig.IsValid() && FinisherConfig->GetUpMontage &&
			Cast<UVBEnemyAnimInstance>(VictimMesh->GetAnimInstance());
		// A surviving victim's GetUp ability corrects both position and rotation
		// with the first evaluated get-up pose. Moving the old pose here causes a pop.
		if (bVictimDead || bVictimSimulating)
		{
			VB_LOG(Log, "FinisherEnd: skip capsule rebase actor=%s dead=%d physics=%d; ragdoll owns world pose",
				*Target->GetName(), bVictimDead ? 1 : 0, bVictimSimulating ? 1 : 0);
		}
		else if (!bGetUpOwnsHandoff)
		{
			const FTransform MeshXf = VictimMesh->GetComponentTransform();
			const FTransform RootXf = VictimMesh->GetSocketTransform(TEXT("root"), RTS_World);

			// Z 는 뺀다. 캡슐은 땅에 붙어 있어야 하고, 누운 자세의 루트 높이를 그대로 주면 캡슐이 가라앉는다.
			const FVector Offset(RootXf.GetLocation().X - MeshXf.GetLocation().X,
			                     RootXf.GetLocation().Y - MeshXf.GetLocation().Y,
			                     0.0f);
			const float YawDelta = FRotator::NormalizeAxis(RootXf.Rotator().Yaw - MeshXf.Rotator().Yaw);

			Target->AddActorWorldOffset(Offset, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
			Target->AddActorWorldRotation(FRotator(0.0f, YawDelta, 0.0f), /*bSweep=*/false, nullptr,
			                              ETeleportType::TeleportPhysics);

			VB_LOG(Log, "%s: 장면 끝 - 캡슐을 몸 자리로 (%.0fcm, %.1f도)",
			       *GetName(), Offset.Size2D(), YawDelta);
		}
	}

	// 옛 처형 경로에서 버텼으면 일어난다. 새 상호작용 경로는 착지 notify 에서
	// 물리 래그돌을 시작하고 정착 뒤 같은 GA_GetUp 에 넘기므로 여기서 중복 발동하지 않는다.
	// 피해를 넣은 «뒤에» 묻는다 - 앞에서 물으면 이번 처형으로 죽는 경우에도
	//   살아 있다고 읽어 시체가 일어선다.
	// 왜 이벤트인가: 일어나는 것은 대상의 일이다. 여기서 대상의 몽타주를 직접 틀면
	//   그 재생이 어느 어빌리티에도 안 속해 취소·복제 규칙이 없어진다(스태거와 같은 이유).
	if (Target && FinisherConfig.IsValid() && !bFinisherUsedInteraction && !bFinisherVictimEnteredRecoverableRagdoll)
	{
		UAbilitySystemComponent* TargetASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
		if (TargetASC && !UVBAbilitySystemStatics::IsDead(TargetASC))
		{

			FGameplayEventData Payload;
			Payload.EventTag       = VBGameplayTags::Event_Combat_FinisherSurvived;
			Payload.Instigator     = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
			Payload.Target         = Target;
			// 기상 클립은 처형마다 짝이다. 대상이 처형 설정을 몰라도 되게 실어 보낸다.
			Payload.OptionalObject = FinisherConfig->GetUpMontage;
			// 쓰러진 방향과 클립이 가정하는 방향의 차. 이걸 안 실으면 기상 시작에 몸이 홱 돈다.
			Payload.EventMagnitude = FinisherConfig->GetUpYawOffset;

			TargetASC->HandleGameplayEvent(Payload.EventTag, &Payload);
			VB_LOG(Log, "%s: 대상이 처형을 버텼다 - 기상 이벤트 발송 (%s, 회전 보정 %.1f도)",
			       *GetName(), *GetNameSafe(FinisherConfig->GetUpMontage), FinisherConfig->GetUpYawOffset);
		}
	}

	FinisherTarget.Reset();
	FinisherConfig.Reset();

	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(),
	           /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}

void UVBGA_Execute::EndAbility(const FGameplayAbilitySpecHandle     Handle,
                               const FGameplayAbilityActorInfo*     ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo,
                               bool                                 bReplicateEndAbility,
                               bool                                 bWasCancelled)
{
	// 종료 경로가 셋이다 - 정상 종료 / 취소 / 조기 이탈. 셋 다 이리로 오므로 정리를 여기 한 곳에 둔다.
	// 컴포넌트 쪽과 여기가 각각 정리하면 「끝났는지」를 두 곳이 판단하게 된다.
	if (bFinisherActive)
	{
		bFinisherActive          = false;
		bFinisherUsedInteraction = false;
		bFinisherDamageApplied   = false;
		bFinisherVictimEnteredRecoverableRagdoll = false;
		FinisherTarget.Reset();
		FinisherConfig.Reset();

		if (AActor* Avatar = GetAvatarActorFromActorInfo())
		{
			if (UVBFinisherComponent* Finisher = Avatar->FindComponentByClass<UVBFinisherComponent>())
			{
				Finisher->OnFinisherEnded.RemoveDynamic(this, &UVBGA_Execute::HandleFinisherEnded);
				Finisher->OnInteractionVictimLanded.RemoveDynamic(
					this, &UVBGA_Execute::HandleInteractionVictimLanded);

				// 취소로 끝났는데 장면이 아직 돌고 있으면 세워야 한다. 안 그러면 어빌리티는 끝났는데
				// 두 몸이 계속 처형 애니를 돈다.
				if (bWasCancelled && Finisher->IsPlaying())
				{
					Finisher->EarlyOut();
				}
			}
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
