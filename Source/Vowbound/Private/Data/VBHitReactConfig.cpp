// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBHitReactConfig.h"

#include "Animation/AnimMontage.h"
#include "GameFramework/Controller.h"  // VBComputeHitDirection - 공격자를 폰으로 푼다
#include "GameFramework/PlayerState.h"

UAnimMontage* UVBHitReactConfig::ResolveMontage(EVBWeaponType Stance) const
{
	// 등록된 스탠스의 몽타주 우선. 스탠스가 등록됐어도 몽타주가 비어 있으면 DefaultSet 으로 폴백한다
	// (부분 할당된 엔트리가 무동작을 유발하지 않도록 — UVBWeaponMovesetConfig::GetAttackConfig 폴백 관례 확장).
	if (const FVBHitReactSet* Set = StanceOverrides.Find(Stance))
	{
		if (Set->Montage)
		{
			return Set->Montage;
		}
	}
	return DefaultSet.Montage;
}

UAnimMontage* FVBHitReactSet::ResolveByDirection(EVBHitDirection Direction) const
{
	UAnimMontage* Directional = nullptr;
	switch (Direction)
	{
	case EVBHitDirection::Front: Directional = MontageFront; break;
	case EVBHitDirection::Back:  Directional = MontageBack;  break;
	case EVBHitDirection::Left:  Directional = MontageLeft;  break;
	case EVBHitDirection::Right: Directional = MontageRight; break;
	}
	return Directional ? Directional : Montage.Get();
}

UAnimMontage* UVBHitReactConfig::ResolveMontage(EVBWeaponType Stance, EVBHitDirection Direction) const
{
	// 스탠스 세트가 있고 거기서 무엇이든 나오면 그것. 세트가 통째로 비었으면 DefaultSet 으로 - 부분 할당이 무동작을 만들지 않게.
	if (const FVBHitReactSet* Set = StanceOverrides.Find(Stance))
	{
		if (UAnimMontage* FromStance = Set->ResolveByDirection(Direction))
		{
			return FromStance;
		}
	}
	return DefaultSet.ResolveByDirection(Direction);
}

const AActor* VBResolveInstigatorBody(const AActor* Instigator)
{
	// 플레이어의 ASC 소유자는 PlayerState 라(VBCharacter.cpp 의 InitAbilityActorInfo(PS, this)) 위치가 원점이다 - 그대로 쓰면
	//   어디서 때려도 방향이 한 분면으로 고정된다(2026-09-29, user 「옆을 바꿔 때려도 왼쪽 히트만 나온다」). 적은 소유자가 곧 몸이라 그대로다.
	//   컨트롤러가 오는 경로도 같은 이유로 폰으로 푼다.
	if (const APlayerState* PlayerState = Cast<APlayerState>(Instigator))
	{
		return PlayerState->GetPawn();
	}
	if (const AController* Controller = Cast<AController>(Instigator))
	{
		return Controller->GetPawn();
	}
	return Instigator;
}

EVBHitDirection VBComputeHitDirection(const AActor* Victim, const AActor* InInstigator)
{
	const AActor* Instigator = VBResolveInstigatorBody(InInstigator);
	if (!Victim || !Instigator)
	{
		return EVBHitDirection::Front;
	}
	const FVector Local = Victim->GetActorTransform().InverseTransformVectorNoScale(
		Instigator->GetActorLocation() - Victim->GetActorLocation());
	if (FMath::Abs(Local.X) >= FMath::Abs(Local.Y))
	{
		return Local.X >= 0.0f ? EVBHitDirection::Front : EVBHitDirection::Back;
	}
	return Local.Y >= 0.0f ? EVBHitDirection::Right : EVBHitDirection::Left;
}

UAnimMontage* UVBHitReactConfig::ResolveStaggerMontage(EVBHitDirection Direction) const
{
	// 방향 슬롯이 비면 무방향으로 떨어진다 - 보스는 왼쪽 클립이 팩에 없어 그 자리가 비어 있다(design §13.3 결정 ③).
	switch (Direction)
	{
		case EVBHitDirection::Front: if (StaggerSet.MontageFront) return StaggerSet.MontageFront; break;
		case EVBHitDirection::Back:  if (StaggerSet.MontageBack)  return StaggerSet.MontageBack;  break;
		case EVBHitDirection::Left:  if (StaggerSet.MontageLeft)  return StaggerSet.MontageLeft;  break;
		case EVBHitDirection::Right: if (StaggerSet.MontageRight) return StaggerSet.MontageRight; break;
		default: break;
	}
	return StaggerSet.Montage;
}

UAnimMontage* UVBHitReactConfig::ResolveHeavyMontage(EVBHitDirection Direction) const
{
	// FVBHitReactSet::ResolveByDirection 이 «방향 -> 무방향» 폴백을 이미 한다. 그 뒤(가벼운 세트)는 호출자 몫이다.
	return HeavySet.ResolveByDirection(Direction);
}

UAnimMontage* UVBHitReactConfig::ResolveStaggerMontage() const
{
	// StaggerSet 은 단일 세트다 - 스탠스로 갈리지 않으므로 인자를 받지 않는다(헤더 주석 참조).
	// 히트리액트로 폴백하지 않는다 - 폴백하면 미저작이 "가벼운 플린치"로 보여 눈에 안 띈다.
	return StaggerSet.Montage;
}
