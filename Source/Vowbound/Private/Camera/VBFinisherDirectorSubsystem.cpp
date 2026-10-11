// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Camera/VBFinisherDirectorSubsystem.h"

#include "Camera/VBFinisherCamera.h"
#include "ContextualAnimSceneActorComponent.h"
#include "ContextualAnimTypes.h"
#include "Character/VBFinisherComponent.h"
#include "Data/VBFinisherConfig.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimeControl/VBTimeDilationSubsystem.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 시간 배율 요청의 이름. 처형은 한 번에 하나만 도므로(StartFinisher 가 IsPlaying 으로 막는다)
	// 실행별 고유값을 만들 이유가 없다. 고유값은 그것을 들고 있을 상태를 늘린다.
	// 대신 시작할 때 먼저 Pop 을 한 번 불러 앞 처형이 비정상 종료로 남긴 요청을 지운다.
	const FName FinisherTimeDilationHandle(TEXT("Finisher"));
}

bool UVBFinisherDirectorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVBFinisherDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 클래스 델리게이트라 월드마다 같은 것을 듣는다. PIE 는 월드가 여럿이므로
	// 핸들러가 「내 월드인가」를 스스로 가른다.
	UVBFinisherComponent::OnConfigArrived.AddUObject(
		this, &UVBFinisherDirectorSubsystem::HandleConfigArrived);
}

void UVBFinisherDirectorSubsystem::Deinitialize()
{
	// 월드가 먼저 죽는 경로가 있다. 남겨 두면 죽은 서브시스템으로 호출이 간다.
	UVBFinisherComponent::OnConfigArrived.RemoveAll(this);
	End();

	Super::Deinitialize();
}

void UVBFinisherDirectorSubsystem::HandleConfigArrived(AActor* Attacker, const UVBFinisherConfig* InConfig)
{
	// 남의 월드 일이면 무시한다. 클래스 델리게이트의 값은 여기서 치른다.
	if (!Attacker || Attacker->GetWorld() != GetWorld())
	{
		return;
	}

	// 설정이 비워졌다 = 서버에서 장면이 끝났다. 우리 것이면 거둔다.
	if (!InConfig)
	{
		if (PendingAttacker.Get() == Attacker)
		{
			End();
		}
		return;
	}
	if (PendingAttacker.Get() == Attacker && PendingConfig == InConfig)
	{
		TryBegin(); // MMI 재생 준비/설정 복제의 순서가 달라도 카메라는 한 번만 시작한다.
		return;
	}

	// 앞 판이 안 거둬졌으면 먼저 거둔다. 겹치는 일은 없어야 하지만, 있으면 카메라가 둘 남는다.
	if (bRunning || PendingAttacker.IsValid())
	{
		End();
	}

	PendingAttacker = Attacker;
	PendingConfig = InConfig;

	// 장면이 벌써 서 있을 수도, 아직일 수도 있다. 두 복제의 도착 순서가 안 정해져 있다.
	// 아직이면 구독해 두고 기다린다 - 늦게 오는 쪽이 방아쇠가 된다.
	if (UContextualAnimSceneActorComponent* Scene = UVBFinisherComponent::FindSceneActorComponent(Attacker))
	{
		WatchedScene = Scene;
		Scene->OnJoinedSceneDelegate.AddUniqueDynamic(this, &UVBFinisherDirectorSubsystem::HandleJoinedScene);
		Scene->OnLeftSceneDelegate.AddUniqueDynamic(this, &UVBFinisherDirectorSubsystem::HandleLeftScene);
	}
	else
	{
		// 두 기초 클래스의 생성자가 이것을 보장한다. 여기 걸렸다면 그 둘을 안 거치는 액터다.
		VB_LOG(Warning, "처형 카메라: %s 에 씬 컴포넌트가 없다 - 카메라를 세우지 않는다",
		       *GetNameSafe(Attacker));
		PendingAttacker = nullptr;
		PendingConfig = nullptr;
		return;
	}

	TryBegin();
}

void UVBFinisherDirectorSubsystem::HandleJoinedScene(UContextualAnimSceneActorComponent* /*SceneActorComponent*/)
{
	TryBegin();
}

void UVBFinisherDirectorSubsystem::HandleLeftScene(UContextualAnimSceneActorComponent* /*SceneActorComponent*/)
{
	End();
}

AActor* UVBFinisherDirectorSubsystem::ResolveAnchor(const UVBFinisherComponent* Finisher) const
{
	if (!Finisher || !PendingConfig)
	{
		return nullptr;
	}

	// 역할 이름은 설정 자산이 든다. 코드가 「피해자」를 아는 순간 역할 구성이 다른 처형에서 깨진다.
	return Finisher->GetParticipant(PendingConfig->CameraAnchorRole);
}

void UVBFinisherDirectorSubsystem::CollectViewingControllers(const TArray<AActor*>& Participants,
                                                             TArray<APlayerController*>& OutControllers) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 로컬 플레이어만 훑는다. 서버가 원격 플레이어의 카메라를 정하지 않는다 -
	// 그것은 그 기계의 이 서브시스템이 자기 뷰타깃을 보고 정한다.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}

		// 여기가 이 설계의 한 줄이다. 「누가 조종하나」가 아니라 「무엇을 보고 있나」를 묻는다.
		// 그래서 관전자와 리플레이 뷰어가 같은 판정으로 걸린다 - 그들에게도 뷰타깃은 있다.
		if (Participants.Contains(PC->GetViewTarget()))
		{
			OutControllers.Add(PC);
		}
	}
}

void UVBFinisherDirectorSubsystem::TryBegin()
{
	if (bRunning)
	{
		return;
	}

	AActor* Attacker = PendingAttacker.Get();
	const UVBFinisherComponent* Finisher = Attacker ? Attacker->FindComponentByClass<UVBFinisherComponent>() : nullptr;
	if (!Attacker || !PendingConfig || !Finisher || !Finisher->IsPlaying())
	{
		// 아직 둘이 다 안 모였다. 나머지 신호가 오면 다시 불린다.
		return;
	}

	AActor* AnchorActor = ResolveAnchor(Finisher);
	if (!AnchorActor)
	{
		// 막을 수 있는 자리가 아니다(저작 오류는 런타임에야 드러난다) - 말한다(규칙 27).
		// 기본값으로 아무 액터에나 붙이면 카메라가 엉뚱한 곳을 비추고 아무도 원인을 못 찾는다.
		VB_LOG(Warning, "처형 카메라: 앵커 역할 '%s' 를 씬 바인딩에서 못 찾았다 (config=%s) - 카메라를 세우지 않는다",
		       *PendingConfig->CameraAnchorRole.ToString(), *GetNameSafe(PendingConfig));
		return;
	}

	// 키가 없는 설정은 「연출 없는 처형」이다. 기능상 정상 경로다.
	//
	// 그래도 한 줄 남긴다. 조용히 빠지면 「키가 없어서 안 나온다」와 「신호가 여기까지 못 왔다」가
	// 화면에서 구별이 안 된다. 2026-09-13 에 정확히 그 둘을 못 갈라 진단이 한 바퀴 돌았다 -
	// 그때 진짜 원인은 호스트에서 OnRep 이 안 불려 신호 자체가 안 온 것이었다.
	if (PendingConfig->CameraKeys.IsEmpty())
	{
		VB_LOG(Log, "처형 카메라: %s 에 CameraKeys 가 없다 - 연출 없는 처형으로 넘어간다",
		       *GetNameSafe(PendingConfig));
		return;
	}

	TArray<AActor*> Participants;
	Finisher->GetParticipants(Participants);

	TArray<APlayerController*> Viewers;
	CollectViewingControllers(Participants, Viewers);
	if (Viewers.IsEmpty())
	{
		// 이 기계의 아무도 그 장면을 보고 있지 않다. 데디 서버가 여기 온다 - 로컬 플레이어가 없으니
		// 정상이고 조용하다.
		//
		// 그런데 로컬 플레이어가 «있는데도» 아무도 안 보고 있다면 그것은 조용하면 안 된다.
		// 싱글에서 그 상태는 곧 「처형은 걸렸는데 카메라만 안 나온다」이고, 로그가 없으면 원인을
		// 카메라 코드에서 찾게 된다 - 실제 원인은 뷰타깃이 폰이 아닌 것(다른 연출·사망 카메라 등)이다.
		// 막을 수 있는 자리가 아니므로 말한다(규칙 27).
		int32 LocalCount = 0;
		if (const UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (const APlayerController* PC = It->Get())
				{
					LocalCount += PC->IsLocalController() ? 1 : 0;
				}
			}
		}

		if (LocalCount > 0)
		{
			VB_LOG(Warning, "처형 카메라: 로컬 플레이어 %d명이 있는데 아무도 참가자를 보고 있지 않다 "
			                "- 카메라를 세우지 않는다 (참가자 %d명, 앵커=%s)",
			       LocalCount, Participants.Num(), *GetNameSafe(AnchorActor));
		}
		return;
	}

	bRunning = true;

	// 사람마다 하나씩 세운다. 분할 화면에서 한 대를 나눠 쓰면 둘째 화면이 첫째를 따라간다.
	for (APlayerController* PC : Viewers)
	{
		AVBFinisherCamera* Camera = GetWorld()->SpawnActor<AVBFinisherCamera>(
			AVBFinisherCamera::StaticClass(), AnchorActor->GetActorTransform());
		if (!Camera)
		{
			continue;
		}

		Camera->Setup(PendingConfig, AnchorActor, Attacker);

		// 첫 키의 BlendTime 으로 들어간다. 자르지 말고 블렌드하라는 것이 이 갈래의 결정이다.
		PC->SetViewTargetWithBlend(Camera, PendingConfig->CameraKeys[0].BlendTime);

		ActiveCameras.Add(Camera);
		ActiveControllers.Add(PC);
	}

	// 슬로우는 로컬에서만 건다. 서버에서 걸면 모두가 느려진다.
	// 지금은 첫 구간 하나만 쓴다 - 여러 구간을 시간축으로 흘리는 것은 이 판의 범위가 아니고,
	// 자산에 구간이 하나뿐인 상태에서 그 기계를 먼저 지으면 안 잰 원료로 짓는 것이다(규칙 26).
	// 구간이 둘 이상인 자산이 생기면 그때 시간축 소비로 넓힌다.
	if (!PendingConfig->TimeDilationSegments.IsEmpty())
	{
		if (UVBTimeDilationSubsystem* TimeDilation = UVBTimeDilationSubsystem::Get(this))
		{
			// 앞 판이 비정상 종료로 남긴 요청을 먼저 지운다. 없으면 무동작이다.
			TimeDilation->Pop(FinisherTimeDilationHandle);

			const FVBFinisherTimeDilationSegment& Segment = PendingConfig->TimeDilationSegments[0];
			TimeDilation->PushTimed(FinisherTimeDilationHandle, Segment.Scale, Segment.RealDuration);
			bPushedTimeDilation = true;
		}
	}

	// 「처형 연출 시작」은 UVBGA_Execute 가 이미 쓰는 말이다. 같은 말이면 로그로 둘을 못 가른다.
	VB_LOG(Log, "처형 카메라 시작: 뷰어 %d명, 앵커=%s, config=%s",
	       Viewers.Num(), *GetNameSafe(AnchorActor), *GetNameSafe(PendingConfig));
}

void UVBFinisherDirectorSubsystem::End()
{
	// 구독부터 끊는다. 아래에서 뷰타깃을 되돌리는 동안 다시 불려 들어오면 안 된다.
	if (UContextualAnimSceneActorComponent* Scene = WatchedScene.Get())
	{
		Scene->OnJoinedSceneDelegate.RemoveDynamic(this, &UVBFinisherDirectorSubsystem::HandleJoinedScene);
		Scene->OnLeftSceneDelegate.RemoveDynamic(this, &UVBFinisherDirectorSubsystem::HandleLeftScene);
	}
	WatchedScene = nullptr;

	const float ReturnBlend = PendingConfig ? PendingConfig->CameraReturnBlend : 0.0f;

	for (int32 i = 0; i < ActiveControllers.Num(); ++i)
	{
		APlayerController* PC = ActiveControllers[i].Get();
		if (!PC)
		{
			continue;
		}

		// 복귀 직전에 컨트롤 회전을 카메라의 현재 회전으로 맞춘다. yaw 만이 아니라 pitch 도.
		// 안 맞추면 뷰타깃이 폰으로 돌아가는 순간 스프링암이 옛 회전으로 스냅해 화면이 튄다.
		if (ActiveCameras.IsValidIndex(i) && ActiveCameras[i])
		{
			PC->SetControlRotation(ActiveCameras[i]->GetViewRotation());
		}

		PC->SetViewTargetWithBlend(PC->GetPawn(), ReturnBlend);
	}

	// 블렌드가 끝난 뒤에 파괴한다. 즉시 파괴하면 복귀 블렌드의 출발점이 사라져 화면이 튄다.
	// 타이머는 실시간으로 잰다 - 처형 슬로우가 남아 있으면 게임시간 타이머가 늦게 온다.
	for (AVBFinisherCamera* Camera : ActiveCameras)
	{
		if (!Camera)
		{
			continue;
		}
		if (ReturnBlend > KINDA_SMALL_NUMBER)
		{
			Camera->SetLifeSpan(ReturnBlend + 0.1f);
		}
		else
		{
			Camera->Destroy();
		}
	}

	ActiveCameras.Reset();
	ActiveControllers.Reset();

	if (bPushedTimeDilation)
	{
		if (UVBTimeDilationSubsystem* TimeDilation = UVBTimeDilationSubsystem::Get(this))
		{
			TimeDilation->Pop(FinisherTimeDilationHandle);
		}
		bPushedTimeDilation = false;
	}

	PendingAttacker = nullptr;
	PendingConfig = nullptr;
	bRunning = false;
}
