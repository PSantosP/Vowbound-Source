#include "Animation/VBPlayerAttentionComponent.h"
#include "Animation/VBLookAtInterestComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "Net/UnrealNetwork.h"
#include "Traversal/VBTraversalComponent.h"

UVBPlayerAttentionComponent::UVBPlayerAttentionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.2f;
	SetIsReplicatedByDefault(true);
}

void UVBPlayerAttentionComponent::BeginPlay()
{
	Super::BeginPlay();
	// Possession may follow BeginPlay. Keep the server tick, and check possession on each update.
	SetComponentTickEnabled(GetOwner() && GetOwner()->HasAuthority());
}

void UVBPlayerAttentionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetOwner() && GetOwner()->HasAuthority()) UpdateAttention();
}

bool UVBPlayerAttentionComponent::IsUsableActor(const AActor* Actor) const
{
	return IsValid(Actor) && Actor != GetOwner() && Actor->GetWorld() == GetWorld()
		&& !UVBAbilitySystemStatics::IsActorDead(Actor)
		&& (Actor->GetIsReplicated() || Actor->IsFullNameStableForNetworking());
}

bool UVBPlayerAttentionComponent::SetConversationTarget(AActor* Actor)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->HasAuthority() || !Pawn->IsPlayerControlled() || !IsUsableActor(Actor)) return false;
	Conversation = Actor;
	UpdateAttention();
	return true;
}

void UVBPlayerAttentionComponent::ClearConversationTarget(AActor* ExpectedActor)
{
	if (GetOwner() && GetOwner()->HasAuthority() && Conversation.Get() == ExpectedActor)
	{
		Conversation.Reset();
		UpdateAttention();
	}
}

bool UVBPlayerAttentionComponent::IsPoseSuppressed() const
{
	// 소유자가 없으면 사실을 모을 수 없다 - 수집의 조기 실패라 정책의 «사실 false» 와 섞지 않고 여기서 막는다.
	if (!GetOwner()) return true;
	// 무엇이 주시 자세를 막는지는 몸 소유 정책 한 곳이 정한다(구조 정리 2단계 1항). 여기는 사실만 모은다 -
	//   컴포넌트·ASC 가 없으면 그 칸은 false 다(종전과 같다).
	VBBodyControlPolicy::FAttentionPoseFacts Facts;
	Facts.bDead = UVBAbilitySystemStatics::IsActorDead(GetOwner());
	if (const auto* Traversal = GetOwner()->FindComponentByClass<UVBTraversalComponent>())
	{
		Facts.bTraversalAction = Traversal->IsDoingTraversal();
		Facts.bTraversalFacing = Traversal->OwnsCapsuleFacing();
	}
	if (const auto* Interactor = GetOwner()->FindComponentByClass<UVBInteractorComponent>())
	{
		Facts.bEnvironmentMotionFacing = Interactor->OwnsCapsuleFacing();
	}
	if (const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))
	{
		Facts.bAttacking = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking);
		Facts.bDodgeCommit = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_DodgeCommit);
		Facts.bHitReacting = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_HitReacting);
		Facts.bStaggered = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Staggered);
		Facts.bDowned = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Downed);
		Facts.bInteracting = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Interacting);
	}
	return VBBodyControlPolicy::IsAttentionPoseSuppressed(Facts);
}

bool UVBPlayerAttentionComponent::IsInView(const FVector& Location, float Distance, float HalfAngle) const
{
	if (Location.ContainsNaN() || !FMath::IsFinite(Distance) || !FMath::IsFinite(HalfAngle) || Distance <= 0.f) return false;
	const FVector Delta = Location - GetOwner()->GetActorLocation();
	if (Delta.SizeSquared() > FMath::Square(Distance)) return false;
	const FVector Forward = GetOwner()->GetActorForwardVector().GetSafeNormal2D();
	const FVector Direction = Delta.GetSafeNormal2D();
	return Direction.IsNearlyZero() || FVector::DotProduct(Forward, Direction) >= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngle, 0.f, 89.f)));
}

bool UVBPlayerAttentionComponent::IsVisible(const AActor* Actor, const FVector& Location) const
{
	if (!GetWorld() || Location.ContainsNaN()) return false;
	FVector EyeLocation;
	FRotator EyeRotation;
	GetOwner()->GetActorEyesViewPoint(EyeLocation, EyeRotation);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBPlayerAttention), false, GetOwner());
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, EyeLocation, Location, ECC_Visibility, Params) || Hit.GetActor() == Actor;
}

bool UVBPlayerAttentionComponent::IsEligibleInterest(const UVBLookAtInterestComponent* Interest, bool bRetain) const
{
	if (!IsValid(Interest) || !Interest->IsRegistered() || !Interest->bInterestEnabled || !IsUsableActor(Interest->GetOwner())) return false;
	const FVector Point = Interest->GetInterestLocation();
	return IsInView(Point, bRetain ? FMath::Max(AcquireDistance, ReleaseDistance) : AcquireDistance,
		bRetain ? FMath::Max(AcquireHalfAngle, ReleaseHalfAngle) : AcquireHalfAngle) && IsVisible(Interest->GetOwner(), Point);
}

void UVBPlayerAttentionComponent::Select(AActor* Actor, const FVector& WorldPoint, FName Socket, EVBPlayerAttentionSource Source)
{
	Selected.Actor = Actor;
	Selected.Source = Actor ? Source : EVBPlayerAttentionSource::None;
	Selected.Socket = Actor ? Socket : NAME_None;
	// A socket is evaluated from the current pose on the receiving machine, not sampled at 5 Hz.
	Selected.LocalPoint = Actor && Socket.IsNone() ? Actor->GetActorTransform().InverseTransformPosition(WorldPoint) : FVector::ZeroVector;
}

void UVBPlayerAttentionComponent::UpdateAttention()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		Conversation.Reset();
		CurrentInterest.Reset();
		Selected = {};
		return;
	}
	if (!IsUsableActor(Conversation.Get()) || UVBAbilitySystemStatics::IsActorDead(Pawn)) Conversation.Reset();
	const auto* Interactor = Pawn->FindComponentByClass<UVBInteractorComponent>();
	const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	const bool bInteractionOwnsPose = (Interactor && Interactor->OwnsCapsuleFacing())
		|| (ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Interacting));
	if (bInteractionOwnsPose)
		InteractionGazeResumeTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, InteractionGazeResumeDelay);
	if (IsPoseSuppressed()) { Selected = {}; CurrentInterest.Reset(); return; }
	if (AActor* Actor = Conversation.Get(); IsUsableActor(Actor))
	{
		FVector Point = Actor->GetActorLocation();
		FName Socket = NAME_None;
		if (const ACharacter* Character = Cast<ACharacter>(Actor); Character && Character->GetMesh() && Character->GetMesh()->DoesSocketExist(TEXT("head")))
		{
			Socket = TEXT("head");
			Point = Character->GetMesh()->GetSocketLocation(Socket);
		}
		if (IsInView(Point, ConversationMaxDistance, ReleaseHalfAngle) && IsVisible(Actor, Point))
		{
			CurrentInterest.Reset();
			Select(Actor, Point, Socket, EVBPlayerAttentionSource::Conversation);
			return;
		}
	}
	if (!bAutomaticAttention || GetWorld()->GetTimeSeconds() < InteractionGazeResumeTime
		|| !FMath::IsFinite(AcquireDistance) || AcquireDistance <= 0.f) { Selected = {}; CurrentInterest.Reset(); return; }
	if (Interactor)
	{
		if (auto* Focus = Interactor->FindBestInteractable(); IsValid(Focus) && IsUsableActor(Focus->GetOwner()))
		{
			const FVector Anchor = Focus->GetComponentLocation();
			FVector Head = Pawn->GetActorLocation();
			if (const ACharacter* Character = Cast<ACharacter>(Pawn); Character && Character->GetMesh()
				&& Character->GetMesh()->DoesSocketExist(TEXT("head")))
				Head = Character->GetMesh()->GetSocketLocation(TEXT("head"));
			const FVector GazePoint(Anchor.X, Anchor.Y,
				FMath::Max(Anchor.Z, Head.Z - FMath::Max(0.f, InteractionGazeMaxDropFromHead)));
			if (FVector::DistSquared2D(Head, Anchor) >= FMath::Square(FMath::Max(0.f, InteractionGazeMinDistance))
				&& IsInView(Anchor, FMath::Max(AcquireDistance, ReleaseDistance), ReleaseHalfAngle)
				&& IsVisible(Focus->GetOwner(), GazePoint))
			{
				CurrentInterest.Reset();
				Select(Focus->GetOwner(), GazePoint, NAME_None, EVBPlayerAttentionSource::Interaction);
				return;
			}
		}
	}
	UVBLookAtInterestComponent* Best = CurrentInterest.Get();
	if (!IsEligibleInterest(Best, true)) Best = nullptr;
	if (auto* Registry = GetWorld()->GetSubsystem<UVBLookAtInterestSubsystem>())
	{
		for (const auto& Entry : Registry->GetInterests())
		{
			auto* Candidate = Entry.Get();
			if (!Candidate || Candidate == Best || (Best && Candidate->Priority < Best->Priority)) continue;
			if (Best && Candidate->Priority == Best->Priority && (Best == CurrentInterest.Get() ||
				FVector::DistSquared(Pawn->GetActorLocation(), Candidate->GetInterestLocation()) >= FVector::DistSquared(Pawn->GetActorLocation(), Best->GetInterestLocation()))) continue;
			if (IsEligibleInterest(Candidate, false)) Best = Candidate;
		}
	}
	CurrentInterest = Best;
	if (Best)
	{
		FName Socket = Best->TargetSocket;
		const ACharacter* Character = Cast<ACharacter>(Best->GetOwner());
		if (!Character || !Character->GetMesh() || !Character->GetMesh()->DoesSocketExist(Socket)) Socket = NAME_None;
		Select(Best->GetOwner(), Best->GetInterestLocation(), Socket, EVBPlayerAttentionSource::Interest);
	}
	else Selected = {};
}

bool UVBPlayerAttentionComponent::ResolveAttention(FVector& OutLocation, bool bConversationOnly) const
{
	if (Selected.Source == EVBPlayerAttentionSource::None || (bConversationOnly && Selected.Source != EVBPlayerAttentionSource::Conversation)
		|| !IsUsableActor(Selected.Actor) || IsPoseSuppressed()) return false;
	if (!Selected.Socket.IsNone())
	{
		const ACharacter* Character = Cast<ACharacter>(Selected.Actor);
		if (!Character || !Character->GetMesh() || !Character->GetMesh()->DoesSocketExist(Selected.Socket)) return false;
		OutLocation = Character->GetMesh()->GetSocketLocation(Selected.Socket);
	}
	else OutLocation = Selected.Actor->GetActorTransform().TransformPosition(Selected.LocalPoint);
	return !OutLocation.ContainsNaN();
}

void UVBPlayerAttentionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBPlayerAttentionComponent, Selected);
}
