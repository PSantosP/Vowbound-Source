// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBToggleProp.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractorComponent.h"
#include "Vowbound/Vowbound.h"

AVBToggleProp::AVBToggleProp()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	// 대리 메시가 뼈를 따라가는 동안만 틱한다.
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// 팩 소품 메시는 소품 원점에 그대로 놓인다(캐릭터와 같은 원점에서 저작됐다) - 상대 변환을 두지 않는다.
	PropMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PropMesh"));
	PropMesh->SetupAttachment(RootComponent);

	Interactable = CreateDefaultSubobject<UVBInteractableComponent>(TEXT("Interactable"));
	Interactable->SetupAttachment(RootComponent);
}

void AVBToggleProp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// 에디터에서 목록을 고치면 바로 보여야 대충 맞추기가 된다.
	BuildProxyParts();
}

void AVBToggleProp::BuildProxyParts()
{
	// 조각은 Transient 라 레벨에 저장되지도, PIE 로 복제되지도 않는다 - 게임에서는 BeginPlay 가 다시 부른다.
	for (UStaticMeshComponent* Old : ProxyComponents)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	ProxyComponents.Reset();

	for (int32 Index = 0; Index < ProxyParts.Num(); ++Index)
	{
		const FVBPropProxyPart& Part = ProxyParts[Index];
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(
			this, FName(*FString::Printf(TEXT("ProxyPart_%d"), Index)), RF_Transient);
		Component->SetupAttachment(RootComponent);
		Component->SetStaticMesh(Part.Mesh);
		Component->SetRelativeTransform(Part.Transform);
		// 움직이는 조각은 무버블이어야 한다. 고정 조각도 같이 둔다 - 조각은 몇 개뿐이라 비용이 없다.
		Component->SetMobility(EComponentMobility::Movable);
		Component->RegisterComponent();
		ProxyComponents.Add(Component);
	}
}

void AVBToggleProp::BeginPlay()
{
	Super::BeginPlay();

	if (ProxyComponents.Num() != ProxyParts.Num())
	{
		BuildProxyParts();
	}

	// 뼈를 따라가는 조각: 닫힌 자세(0초)에서 조각이 뼈에 대해 어디 있는지 잡아 둔다.
	const UAnimSequence* ReferenceAnim = Interactable->GetConfig()->GetFrontMotion(true).PropAnimation;
	for (int32 Index = 0; Index < ProxyParts.Num() && Index < ProxyComponents.Num(); ++Index)
	{
		const FVBPropProxyPart& Part = ProxyParts[Index];
		if (Part.FollowBone.IsNone())
		{
			continue;
		}
		if (!ReferenceAnim)
		{
			// 설정 사고다. 그 조각이 안 움직이는 것으로만 보이므로 말한다.
			VB_LOG(Warning, "%s: 조각 %d 가 뼈 %s 를 따라가야 하는데 설정에 소품 애니가 없다",
			       *GetName(), Index, *Part.FollowBone.ToString());
			continue;
		}
		BonePartAnimation = const_cast<UAnimSequence*>(ReferenceAnim);
		if (BonePartAnimation->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Part.FollowBone) == INDEX_NONE)
		{
			VB_LOG(Warning, "%s: 조각 %d 의 뼈 %s 가 소품 애니 스켈레톤에 없다", *GetName(), Index, *Part.FollowBone.ToString());
			continue;
		}
		const FTransform BoneAtRest = SampleBoneComponentSpace(Part.FollowBone, 0.0f);
		ResolvedParts.Add({ProxyComponents[Index].Get(), Part.FollowBone, Part.Transform.GetRelativeTransform(BoneAtRest)});
	}

	Interactable->OnActivationChanged.AddUniqueDynamic(this, &AVBToggleProp::HandleActivationChanged);
	if (HasAuthority())
	{
		Interactable->OnInteracted.AddUniqueDynamic(this, &AVBToggleProp::HandleInteracted);
		HandleActivationChanged(Interactable->IsActivated(), /*bInstant=*/true);
	}
	else if (Playback.Animation)
	{
		OnRep_Playback();
	}
}

void AVBToggleProp::HandleInteracted(AActor* InteractingActor)
{
	// 규칙은 GA 와 같다 - 이번 상호작용은 지금 상태를 뒤집는다(UVBGA_Interact 가 같은 규칙으로 몸 동작을 골랐다).
	LastInteractor = InteractingActor;
	Interactable->SetActivated(!Interactable->IsActivated());
}

void AVBToggleProp::HandleActivationChanged(bool bActivated, bool bInstant)
{
	// 컴포넌트 상태 OnRep과 액터 재생 OnRep의 도착 순서에 기대지 않는다.
	if (!HasAuthority()) return;
	ApplyServerSideEffects(bActivated, bInstant);
	Playback.Animation = ResolvePropAnimation(bActivated, LastInteractor.Get());
	Playback.StartPosition = 0.0f;
	Playback.PlayRate = 1.0f;
	if (const AActor* Actor = LastInteractor.Get())
	{
		const UVBInteractorComponent* Component = Actor->FindComponentByClass<UVBInteractorComponent>();
		if (Component && Component->GetMotionTarget() == Interactable)
		{
			if (const FVBInteractionMotion* Motion = Component->GetActiveMotion()) Playback.StartPosition = Motion->PropStartTime;
		}
	}
	Playback.ServerStartTime = GetWorld()->GetTimeSeconds();
	Playback.bSnapToEnd = bInstant;
	OnRep_Playback();
	ForceNetUpdate();
	LastInteractor.Reset();
}

void AVBToggleProp::OnRep_Playback()
{
	if (!HasActorBegunPlay()) return;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float Elapsed = FMath::Max(0.0, ServerNow - Playback.ServerStartTime);
	PlayPropAnimation(Playback.Animation, Playback.bSnapToEnd, Playback.StartPosition + Elapsed * Playback.PlayRate);
}

void AVBToggleProp::SetInteractionPlayRate(float Rate)
{
	if (!HasAuthority() || !Playback.Animation || Playback.bSnapToEnd || !FMath::IsFinite(Rate)) return;
	const double Now = GetWorld()->GetTimeSeconds();
	Playback.StartPosition = FMath::Clamp(Playback.StartPosition + static_cast<float>(Now - Playback.ServerStartTime) * Playback.PlayRate,
		0.0f, Playback.Animation->GetPlayLength());
	Playback.ServerStartTime = Now;
	Playback.PlayRate = FMath::Clamp(Rate, 1.0f, 3.0f);
	OnRep_Playback();
	ForceNetUpdate();
}

void AVBToggleProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVBToggleProp, Playback);
}

void AVBToggleProp::ApplyServerSideEffects(bool bActivated, bool bInstant)
{
	Interactable->SetActionTextOverride(bActivated ? DeactivateActionText : FText::GetEmpty());
	RefreshOneShotLock();
	for (AVBToggleProp* Linked : LinkedProps)
	{
		if (!Linked)
		{
			// 레벨에서 연결이 비었다(소품을 지웠는데 목록에 남았다). 막을 수 없으니 말한다.
			VB_LOG(Warning, "%s 의 LinkedProps 에 빈 칸이 있다", *GetName());
			continue;
		}
		Linked->Interactable->SetActivated(bActivated, bInstant);
	}
}

void AVBToggleProp::RefreshOneShotLock()
{
	if (HasAuthority() && bOneShot && Interactable->IsActivated() && IsOneShotSpent())
	{
		Interactable->SetInteractionEnabled(false);
	}
}

UAnimSequence* AVBToggleProp::ResolvePropAnimation(bool bActivated, const AActor* Interactor) const
{
	const UVBInteractableConfig* Config = Interactable->GetConfig();
	if (Interactor)
	{
		const UVBInteractorComponent* Component = Interactor->FindComponentByClass<UVBInteractorComponent>();
		if (Component && Component->GetMotionTarget() == Interactable)
		{
			if (const FVBInteractionMotion* Motion = Component->GetActiveMotion()) return Motion->PropAnimation;
		}
		// 활성 동작이 없는 외부 호출은 동일한 앞뒤 판정으로 복구한다.
		if (const FVBInteractionMotion* Motion =
				Config->SelectMotion(bActivated, GetActorTransform(), Interactor->GetActorLocation()))
		{
			return Motion->PropAnimation;
		}
	}
	return Config->GetFrontMotion(bActivated).PropAnimation;
}

bool AVBToggleProp::GetInteractionBoneTransform(FName BoneName, FTransform& OutWorldTransform) const
{
	if (BoneName.IsNone()) return false;
	if (PropMesh->GetSkeletalMeshAsset())
	{
		if (!PropMesh->DoesSocketExist(BoneName)) return false;
		OutWorldTransform = PropMesh->GetSocketTransform(BoneName, RTS_World);
		return true;
	}
	const USkeleton* Skeleton = BonePartAnimation ? BonePartAnimation->GetSkeleton() : nullptr;
	if (!Skeleton || Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName) == INDEX_NONE) return false;
	OutWorldTransform = SampleBoneComponentSpace(BoneName, BonePartTime) * GetActorTransform();
	return true;
}

void AVBToggleProp::PlayPropAnimation(UAnimSequence* Animation, bool bSnapToEnd, float StartPosition)
{
	if (!Animation)
	{
		return;
	}

	const float Position = bSnapToEnd ? Animation->GetPlayLength()
		: FMath::Clamp(StartPosition, 0.0f, Animation->GetPlayLength());
	if (PropMesh->GetSkeletalMeshAsset())
	{
		PropMesh->PlayAnimation(Animation, /*bLooping=*/false);
		PropMesh->SetPosition(Position, /*bFireNotifies=*/false);
		PropMesh->SetPlayRate(Playback.PlayRate);
		// 비반복 단일 노드 재생은 끝에서 멈춰 그 자세를 든다 - 따로 붙잡지 않아도 된다.
		return;
	}

	if (ResolvedParts.Num() == 0)
	{
		return;
	}
	BonePartAnimation = Animation;
	BonePartTime = Position;
	ApplyBoneParts(BonePartTime);
	SetActorTickEnabled(Position < Animation->GetPlayLength());
}

void AVBToggleProp::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!BonePartAnimation)
	{
		SetActorTickEnabled(false);
		return;
	}
	const float Length = BonePartAnimation->GetPlayLength();
	BonePartTime = FMath::Min(BonePartTime + DeltaSeconds * Playback.PlayRate, Length);
	ApplyBoneParts(BonePartTime);
	if (BonePartTime >= Length)
	{
		SetActorTickEnabled(false);
	}
}

void AVBToggleProp::ApplyBoneParts(float Time)
{
	for (const FResolvedPart& Part : ResolvedParts)
	{
		if (USceneComponent* Component = Part.Component.Get())
		{
			Component->SetRelativeTransform(Part.BoneToComponent * SampleBoneComponentSpace(Part.BoneName, Time));
		}
	}
}

FTransform AVBToggleProp::SampleBoneComponentSpace(FName BoneName, float Time) const
{
	const USkeleton* Skeleton = BonePartAnimation ? BonePartAnimation->GetSkeleton() : nullptr;
	if (!Skeleton)
	{
		return FTransform::Identity;
	}
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);

	// 부모를 따라 올라가며 곱한다. 팩 소품은 뼈가 1~4개라 매 틱 해도 싸다.
	const FAnimExtractContext Context(static_cast<double>(Time));
	FTransform ComponentSpace = FTransform::Identity;
	while (BoneIndex != INDEX_NONE)
	{
		FTransform Local;
		BonePartAnimation->GetBoneTransform(Local, FSkeletonPoseBoneIndex(BoneIndex), Context, /*bUseRawData=*/false);
		ComponentSpace = ComponentSpace * Local;
		BoneIndex = RefSkeleton.GetParentIndex(BoneIndex);
	}
	return ComponentSpace;
}
