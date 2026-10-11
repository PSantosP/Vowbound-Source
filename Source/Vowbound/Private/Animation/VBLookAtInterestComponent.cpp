#include "Animation/VBLookAtInterestComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

void UVBLookAtInterestSubsystem::Register(UVBLookAtInterestComponent* Interest)
{
	if (IsValid(Interest)) Interests.AddUnique(Interest);
}

void UVBLookAtInterestSubsystem::Unregister(UVBLookAtInterestComponent* Interest)
{
	Interests.RemoveAll([Interest](const auto& Entry) { return !Entry.IsValid() || Entry.Get() == Interest; });
}

const TArray<TWeakObjectPtr<UVBLookAtInterestComponent>>& UVBLookAtInterestSubsystem::GetInterests()
{
	Interests.RemoveAll([](const auto& Entry) { return !Entry.IsValid(); });
	return Interests;
}

void UVBLookAtInterestComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Registry = GetWorld()->GetSubsystem<UVBLookAtInterestSubsystem>()) Registry->Register(this);
}

void UVBLookAtInterestComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (auto* Registry = GetWorld()->GetSubsystem<UVBLookAtInterestSubsystem>()) Registry->Unregister(this);
	Super::EndPlay(EndPlayReason);
}

FVector UVBLookAtInterestComponent::GetInterestLocation() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!TargetSocket.IsNone() && Character && Character->GetMesh() && Character->GetMesh()->DoesSocketExist(TargetSocket))
		return Character->GetMesh()->GetSocketLocation(TargetSocket);
	return GetComponentLocation();
}
