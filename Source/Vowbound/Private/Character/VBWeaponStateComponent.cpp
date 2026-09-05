// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Character/VBWeaponStateComponent.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBTargetable.h"
#include "Character/VBTargetLockComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBMontageDebugLibrary.h" // GetMontageSlotName — 상체/전신 슬롯 판정
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBWeaponStateConfig.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Vowbound/Vowbound.h"


UVBWeaponStateComponent::UVBWeaponStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

const UVBWeaponStateConfig* UVBWeaponStateComponent::GetWeaponStateConfig() const
{
	// 할당된 config 우선, 없으면 클래스 CDO 디폴트(절대 null 아님).
	return WeaponStateConfig ? WeaponStateConfig : GetDefault<UVBWeaponStateConfig>();
}

void UVBWeaponStateComponent::CancelGuardAbility()
{
	// 서버 권위에서만. 가드 상태(GuardStartServerTime)가 서버 진실이라 클라 호출은 무의미하다.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// ASC 해석은 캐시된 소유 캐릭터 경유 — 같은 파일의 기존 관용구(MulticastPlayWeaponChangeActing)와 통일.
	//  OwnerCharacter 는 TWeakObjectPtr 이라 bool 변환이 삭제돼 있다 → IsValid()/Get() 이 정석.
	UAbilitySystemComponent* ASC =
		OwnerCharacter.IsValid() ? OwnerCharacter->GetAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return;
	}

	// CancelAbilities 는 대상 GA 의 AssetTags 를 매칭한다(엔진 AbilitySystemComponent_Abilities.cpp::UAbilitySystemComponent::CancelAbilities).
	//  UVBGA_Guard 생성자의 SetAssetTags(Ability.Defense.Guard) 가 이 호출의 전제조건이다.
	FGameplayTagContainer GuardTags;
	GuardTags.AddTag(VBGameplayTags::Ability_Defense_Guard);
	ASC->CancelAbilities(&GuardTags);
}

void UVBWeaponStateComponent::SummonWeapon(EVBWeaponType InWeaponType)
{
	ServerSummonWeapon(InWeaponType);
}

void UVBWeaponStateComponent::SheatheWeapon()
{
	ServerSheatheWeapon();
}

void UVBWeaponStateComponent::EnterCombat()
{
	ServerEnterCombat();
}

void UVBWeaponStateComponent::ResetCombatTimer()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (IsInCombat())
	{
		GetWorld()->GetTimerManager().ClearTimer(CombatExitTimerHandle);
		ArmCombatExitTimer();
	}
}

void UVBWeaponStateComponent::ArmCombatExitTimer()
{
	GetWorld()->GetTimerManager().SetTimer(CombatExitTimerHandle,
	                                       this, &ThisClass::TryCombatExit, GetWeaponStateConfig()->CombatExitDelay, false);
}

void UVBWeaponStateComponent::StopCombatTimer()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (UWorld* World = GetWorld())
	{
		// ClearTimer 를 유지할 것. 파쿠르 중 사망하면 사망 애니가 트래버설 몽타주를 끊으면서
		// RecoverFromTraversal -> RestoreFromTempSheathe 가 뒤이어 UnPauseTimer 를 부른다.
		// ClearTimer 가 핸들을 무효화해 두었기에 그 UnPauseTimer 가 FindTimer 실패로 no-op 이 되고
		// (TimerManager.cpp::FTimerManager::UnPauseTimer) 타이머가 되살아나지 않는다.
		// 즉 이 한 줄을 플래그 방식 등으로 바꾸면 그 재점화 방어가 조용히 사라진다.
		World->GetTimerManager().ClearTimer(CombatExitTimerHandle);
	}
}

void UVBWeaponStateComponent::TempSheatheForParkour()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	// 이미 웨폰이 소환되어있지 않다면 return
	if (!IsWeaponSummoned()) return;

	// 임시수납도 "무기가 손에서 사라지는" 전이 — 가드가 살아남으면 파쿠르 중 패링 판정이 난다.
	CancelGuardAbility();

	bIsTempSheathed             = true;
	StateBeforeTempSheathe      = LocomotionState;
	WeaponTypeBeforeTempSheathe = CurrentWeaponType;

	// ABP에 임시수납을 알림
	OnLocomotionStateChanged.Broadcast(EVBLocomotionState::Unarmed, EVBWeaponType::None);

	// CombatTimer 일시 정지
	GetWorld()->GetTimerManager().PauseTimer(CombatExitTimerHandle);
}

void UVBWeaponStateComponent::RestoreFromTempSheathe()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	// 이미 수납이 되었다면 return
	if (!bIsTempSheathed) return;
	bIsTempSheathed = false;
	OnLocomotionStateChanged.Broadcast(StateBeforeTempSheathe, WeaponTypeBeforeTempSheathe);

	// CombatTimer 재개
	GetWorld()->GetTimerManager().UnPauseTimer(CombatExitTimerHandle);
}

void UVBWeaponStateComponent::EnsureWeaponSummoned(EVBWeaponType InWeaponType)
{
	if (IsWeaponSummoned()) return;
	SummonWeapon(InWeaponType);
}


void UVBWeaponStateComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<AVBCharacter>(GetOwner());
}

void UVBWeaponStateComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBWeaponStateComponent, LocomotionState, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBWeaponStateComponent, CurrentWeaponType, COND_None, REPNOTIFY_Always);
}

void UVBWeaponStateComponent::ServerSheatheWeapon_Implementation()
{
	// 무기On(ArmedCombat)이 아니면 납도할 것이 없다
	if (!IsWeaponSummoned())
	{
		return;
	}

	// 납도도 가드를 끊는다 (2026-07-26 2차 verify 적발 — FIND-063 의 상보 전이가 비어 있었다).
	// 도달 경로: 같은 슬롯키 재입력(VBCharacter::HandleWeaponSlot) / Tab(UVBGA_WeaponSummon).
	// 왜 특히 위험한가: ServerSheatheWeapon 은 CurrentWeaponType 을 의도적으로 보존하므로(아래 주석)
	//   FindGuardSet 이 계속 그 무기 세트를 돌려준다. 취소가 없으면 무기를 손에서 치운 채
	//   State.Combat.Guarding + GuardStartServerTime 이 살아남아 패링/블록 판정이 계속된다.
	CancelGuardAbility();

	// 납도 연기용 — 상태 변경 전 캡처 (From = 손에 들려 있던 무기, DEC-006 ⑥ 규약)
	const EVBWeaponType FromType = CurrentWeaponType;

	// 납도 = 무기만 Off, 전투는 유지 (DEC-006 ② user 확정: ArmedCombat → ArmedExploration).
	// CurrentWeaponType 은 "선택된 무기" 기억으로 유지 — 재발도/공격 자동발도가 같은 무기를 꺼낸다.
	// 태그 제거는 SetLocomotionState 의 AC 이탈 규칙이, 전투 이탈은 TryCombatExit 타이머가 담당.
	SetLocomotionState(EVBLocomotionState::ArmedExploration);

	// 납도 연기 (FIND-022) — SheatheMontage 콘텐츠 부재 무기는 소환 VFX만 발사됨
	MulticastPlayWeaponChangeActing(FromType, EVBWeaponType::None, /*bCombatStance=*/true);

	// 납도 후 경계 유지 (user 06-10: "Tab 누르면 기존 MM으로 돌아온다" FAIL 지적). 종전의 즉시 이탈 평가
	// (06-07 "무기 넣었는데 전투상태?")는 당시 상태2가 평상과 똑같이 보여 머무는 게 어색했기 때문 — DEC-007 ⑤로
	// 상태2에 경계 자세(Fighter 풀+전투 보폭)가 생기며 전제 폐기. 이제 납도 = 경계 자세로 전환이고,
	// 평상 복귀는 CombatExitDelay(10s) 타이머가 담당(정지+적X+락온X 시에만). 타이머를 새로 감아
	// 납도 시점부터 온전한 경계 시간을 보장한다 (잔여 1~2초짜리 기존 타이머에 잘리는 것 방지).
	ResetCombatTimer();
}

bool UVBWeaponStateComponent::ServerSheatheWeapon_Validate()
{
	// 무기On(ArmedCombat) 상태에서만 납도 RPC 허용
	return IsWeaponSummoned();
}

void UVBWeaponStateComponent::ServerSummonWeapon_Implementation(EVBWeaponType InWeaponType)
{
	if (CurrentWeaponType == InWeaponType && IsWeaponSummoned())
	{
		return;
	}
	
	VB_LOG(Log, "ServerSummonWeapon : RequestType=%d, CurrentType=%d",
		(int32)InWeaponType, (int32)CurrentWeaponType);

	// 무기 교체는 가드를 끊는다 (FIND-063 해소, user 확정 2026-07-26).
	// 왜 여기인가: 슬롯키(1/2/3)는 GA 를 거치지 않고 SummonWeapon 을 직접 부르고(VBCharacter::HandleWeaponSlot),
	//   Tab 발도(UVBGA_WeaponSummon)도 결국 여기로 모인다 — 발도/교체의 단일 관문이다.
	//   태그 게이트(ActivationBlockedTags)로는 못 막는다. 슬롯키 경로엔 활성화할 GA 자체가 없기 때문이다.
	// 알려진 잔여(user 인지·수용, FIND-065): 취소 → EndGuard 가 End 섹션(복귀 +20)으로 점프하지만, 아래
	//   MulticastPlayWeaponChangeActing 의 교체 몽타주가 같은 슬롯 그룹(UpperBody ∈ DefaultGroup, 2026-07-23 의도적 설계)
	//   이라 그 End 를 즉시 끊는다 → 위치 복귀는 대부분 유실된다. 판정/태그 누수는 해소되지만 드리프트는 별건.
	CancelGuardAbility();

	// 전환 연기용 — 변경 전 캡처. From 은 "손에 들려 있던 무기" (DEC-006 ⑥ 규약):
	// 상태2(전투·무기Off)에서의 발도는 손이 비어 있었으므로 From=None → SummonMontage(발도 연기) 경로를 탄다.
	const bool bWasWeaponOn = IsWeaponSummoned();
	const EVBWeaponType FromType = bWasWeaponOn ? CurrentWeaponType : EVBWeaponType::None;

	if (bWasWeaponOn)
	{
		// 무기On 유지 중 타입 스왑 — 상태 무변경이라 SetLocomotionState 태그 규칙이 안 돌므로 여기서 직접 스왑
		ApplyWeaponTags(CurrentWeaponType, false);
		CurrentWeaponType = InWeaponType;
		ApplyWeaponTags(InWeaponType, true);
		// 무기 교체도 전투 행위 — 이탈 타이머 리셋
		ResetCombatTimer();
	}
	else
	{
		// 발도 = 무기On = 전투 자세 직행 (DEC-006: 비전투+무기On 칸은 없다).
		// 태그 적용은 SetLocomotionState 의 AC 진입 규칙 — CurrentWeaponType 을 먼저 세팅해 둔다.
		CurrentWeaponType = InWeaponType;
		SetLocomotionState(EVBLocomotionState::ArmedCombat);
	}
	// 마지막 사용 무기 기록 (FIND-030) — 평상 복귀 후 Tab 재발도가 이 무기를 다시 꺼낸다.
	LastUsedWeaponType = InWeaponType;

	// 전환 연기 (FIND-022) — 페어 게이트 몽타주 + 소환 VFX.
	// bCombatStance(ver_B 선택)는 "변경 전 무기On이었나" — 구 IsInCombat 캡처와 동일 의미 보존.
	MulticastPlayWeaponChangeActing(FromType, InWeaponType, bWasWeaponOn);
}

bool UVBWeaponStateComponent::ServerSummonWeapon_Validate(EVBWeaponType InWeaponType)
{
	// 선언된 enum 값만 통과시킨다. None(0)도 허용 — 무기 해제 요청이 이 경로로 온다.
	//
	// 이전엔 상한을 Magic 리터럴로 박아 뒀는데, enum 에 값을 추가하거나 순서를 바꾸는 순간
	// 새 무기가 검증에서 거부된다. WithValidation 계약상 false 는 단순 거부가 아니라 연결 종료라,
	// 증상이 "소환이 안 된다"가 아니라 "게임이 끊긴다"로 나타난다.
	//
	// IsValidEnumValue 는 Names 배열을 전부 훑는다 — UHT 가 붙이는 _MAX 를 걸러 주지 않는다
	// (거르는 것은 IsValidEnumValueOrBitfield 의 Flags 분기뿐, Enum.cpp::UEnum::IsValidEnumValue / ::IsValidEnumValueOrBitfield 실측 2026-08-09).
	// 여기서 안전한 이유는 함수가 걸러 줘서가 아니라 UHT 가 EVBWeaponType 에 _MAX 를 만들지 않았기
	// 때문이다(생성 코드 실측: Enumerators = None/Katana/BigSword/Fighter/Magic 5개뿐).
	// _MAX 가 생기는 enum 에 이 패턴을 복사하면 뚫린다.
	const UEnum* WeaponEnum = StaticEnum<EVBWeaponType>();
	return WeaponEnum && WeaponEnum->IsValidEnumValue(static_cast<int64>(InWeaponType));
}

void UVBWeaponStateComponent::MulticastPlayWeaponChangeActing_Implementation(EVBWeaponType FromType, EVBWeaponType ToType, bool bCombatStance)
{
	// 코스메틱 전용 — 데디 서버는 스킵, 게임 상태는 일절 건드리지 않는다.
	if (GetNetMode() == NM_DedicatedServer) return;
	if (!OwnerCharacter.IsValid() || !OwnerCharacter->GetMesh()) return;

	// To=None(수납)이면 From 무기 데이터를 사용 — 수납 연기/VFX는 사라지는 무기 소속.
	const EVBWeaponType DataKey = (ToType != EVBWeaponType::None) ? ToType : FromType;
	const FVBWeaponSummonData* Data = WeaponDataMap.Find(DataKey);
	if (!Data) return; // WeaponDataMap 미등록 무기 — 연기 없음 (등록은 BP_VBCharacter 컴포넌트 디폴트)

	// ① 소환 VFX — 모든 전환 공통 (의료마법 소환 판타지). 손 소켓 1회성 어태치, 자동 소멸.
	if (Data->SummonVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAttached(
			Data->SummonVFX, OwnerCharacter->GetMesh(), Data->SummonVFXSocket,
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, /*bAutoDestroy=*/true);
	}

	// ② 전환 몽타주 선택 — 반드시 게이트보다 먼저(상체/전신 판정에 몽타주의 baked 슬롯이 필요).
	UAnimMontage* Montage = nullptr;
	if (ToType != EVBWeaponType::None)
	{
		// 전투 스탠스 변형(ver_B) 우선, 없으면 탐험(ver_A) 폴백 — 페어 키는 From 무기
		if (bCombatStance)
		{
			if (const TObjectPtr<UAnimMontage>* Found = Data->ChangeFromMontagesCombat.Find(FromType)) Montage = *Found;
		}
		if (!Montage)
		{
			if (const TObjectPtr<UAnimMontage>* Found = Data->ChangeFromMontages.Find(FromType)) Montage = *Found;
		}
		if (!Montage && FromType == EVBWeaponType::None)
		{
			Montage = Data->SummonMontage; // 비무장→무기 최초 소환 (콘텐츠 부재 시 null = VFX만)
		}
	}
	else
	{
		Montage = Data->SheatheMontage; // 수납 (콘텐츠 부재 시 null = VFX만)
	}
	if (!Montage) return; // 페어 콘텐츠 부재 무기 — 위 VFX 로 마무리

	// ③ 슬롯 판정 — 상체(UpperBody) 몽타주는 하체 로코모션을 안 덮으므로 이동/crouch 중에도 재생한다.
	//    상체/전신 판정 = 몽타주의 baked 슬롯 단일 진실원(GetMontageSlotName). 구조체 플래그 불요·per-montage 정확.
	//    UpperBody 로 슬롯이 재저작된 무기교체 몽타주는 LayeredBlendPerBone(spine_01) 로 상체만 오버레이 → 다리는 계속 로코모션.
	// allowlist(== "UpperBody")로 fail-safe 를 만든다: 슬롯 트랙 0개 몽타주는 GetMontageSlotName 이 NAME_None 을 주는데,
	// "!= DefaultSlot" 로 판정하면 malformed 몽타주가 상체로 오분류돼 게이트를 건너뛴다(fail-open). 명시 슬롯만 상체.
	const bool bUpperBody = UVBMontageDebugLibrary::GetMontageSlotName(Montage) == TEXT("UpperBody");
	if (!bUpperBody)
	{
		// 전신(DefaultSlot) 몽타주만 게이트 — idle 스탠스 전신 애님이라 이동 중엔 발 미끄러짐, crouch 중엔 일어섰다 앉는 깨짐.
		//    (BigSword 히어로 발도 등 전신 연출용. crouch 게이트 근거=user FEEL 06-11.)
		if (OwnerCharacter->GetVelocity().Size2D() > GetWeaponStateConfig()->ChangeActingMaxSpeed) return;
		if (OwnerCharacter->bIsCrouched) return;
	}

	if (UAnimInstance* AnimInst = OwnerCharacter->GetMesh()->GetAnimInstance())
	{
		// 슬롯은 에셋 baked(UpperBody or DefaultSlot). 둘 다 DefaultGroup 이라 공격/회피가 자연 인터럽트(dodge cancel 보존).
		AnimInst->Montage_Play(Montage);
		VB_LOG(Log, "WeaponChangeActing: montage=%s from=%d to=%d combat=%d upperBody=%d",
		       *Montage->GetName(), (int32)FromType, (int32)ToType, bCombatStance ? 1 : 0, bUpperBody ? 1 : 0);
	}
}

void UVBWeaponStateComponent::ServerEnterCombat_Implementation()
{
	VB_LOG(Log, "ServerEnterCombat : CurrentState=%d", (int32)LocomotionState);

	// 전투 자극 (DEC-006 ①): 자동 발도 없음 — 평상이면 전투·무기Off(상태2)로 직행.
	// 발도는 명시 행위(Tab/슬롯키) 또는 공격 GA 의 EnsureWeaponSummoned(③)만 한다.
	if (LocomotionState == EVBLocomotionState::Unarmed)
	{
		SetLocomotionState(EVBLocomotionState::ArmedExploration);
	}
	else
	{
		// 이미 전투(상태2·3) — 이탈 타이머만 리셋
		ResetCombatTimer();
	}
}

bool UVBWeaponStateComponent::ServerEnterCombat_Validate()
{
	// OwnerCharacter가 유효해야 후속 상태 전환/CMC 위임 경로가 안전하다
	return OwnerCharacter.IsValid();
}

void UVBWeaponStateComponent::OnRep_LocomotionState()
{
	// 클라에서 CMC 플래그를 서버와 동기화
	ApplyLocomotionCMCState(LocomotionState);
	
	OnLocomotionStateChanged.Broadcast(LocomotionState, CurrentWeaponType);
}

void UVBWeaponStateComponent::SetLocomotionState(EVBLocomotionState NewState)
{
	// 서버 전용 경로 강제. 모든 호출부가 Server RPC Implementation 경로지만
	// 타이머/파쿠르/GA 확장 시 실수로 클라에서 호출되는 것을 방어한다.
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (LocomotionState == NewState) return;
	VB_LOG(Log, "SetLocomotionState: %d -> %d", (int32)LocomotionState, (int32)NewState);
	EVBLocomotionState OldState = LocomotionState;
	LocomotionState             = NewState;

	// 태그 = 무기On(ArmedCombat) 진실 — AC 진입/이탈 전이는 여기서만 적용/제거 (단일 작성자, DEC-006).
	// AC 유지 중 무기 타입 스왑만 ServerSummonWeapon 이 직접 스왑한다 (상태 무변경이라 여기 안 옴).
	if (OldState == EVBLocomotionState::ArmedCombat && NewState != EVBLocomotionState::ArmedCombat)
	{
		ApplyWeaponTags(CurrentWeaponType, false);
	}
	else if (NewState == EVBLocomotionState::ArmedCombat && OldState != EVBLocomotionState::ArmedCombat)
	{
		// 호출자(ServerSummonWeapon)가 CurrentWeaponType 을 먼저 세팅해 두는 계약
		ApplyWeaponTags(CurrentWeaponType, true);
	}

	// "선택된 무기" 기억은 전투를 떠날 때만 비운다 — 상태2(전투·무기Off)는 재발도를 위해 유지 (DEC-006).
	if (NewState == EVBLocomotionState::Unarmed)
	{
		CurrentWeaponType = EVBWeaponType::None;
	}

	// 전투 타이머 — 전투(상태2·3) 진입/전이 시 이탈 타이머 (재)가동, 평상 복귀 시 해제.
	if (NewState != EVBLocomotionState::Unarmed)
	{
		ArmCombatExitTimer();
	}
	else
	{
		GetWorld()->GetTimerManager().ClearTimer(CombatExitTimerHandle);
	}

	ApplyLocomotionCMCState(NewState);

	// 델리게이트
	OnLocomotionStateChanged.Broadcast(NewState, CurrentWeaponType);
}

void UVBWeaponStateComponent::TryCombatExit()
{
	// 락온중이면 전투 유지 
	if (OwnerCharacter.IsValid())
	{
		if (UVBTargetLockComponent* LockComponent = OwnerCharacter->GetTargetLockComponent())
		{
			if (LockComponent->IsLockedOn())
			{
				ArmCombatExitTimer();
				return;
			}
		}
	}
	
	// 15M 이내에 적이 있으면 전투 유지
	if (AreEnemiesNearby())
	{
		ArmCombatExitTimer();
		return;
	}

	// 이동 중이면 '활동 중'으로 보고 무장을 유지한다 (user 2026-06-09: "움직이는 건 아무것도 안 함이 아니다"). 평가 시점에
	// 이동 중이면 납도 보류하고 타이머 재연장 — 정지(idle)한 뒤의 평가에서야 적·락온 없으면 평상 복귀한다.
	if (OwnerCharacter.IsValid() && OwnerCharacter->GetVelocity().Size2D() > GetWeaponStateConfig()->CombatExitIdleSpeedThreshold)
	{
		ArmCombatExitTimer();
		return;
	}

	// 조건 충족 — 전투 이탈 = 즉시 평상 복귀 (DEC-006 ④ user 확정: 상태3에서도 Unarmed 직행, 납도+이탈 동시)
	const bool bWasWeaponOn = IsWeaponSummoned();
	const EVBWeaponType FromType = CurrentWeaponType; // SetLocomotionState(Unarmed)가 클리어하므로 선캡처
	SetLocomotionState(EVBLocomotionState::Unarmed);

	// 무기를 든 채 이탈했다면 납도 연기 발사 (FIND-022 — SheatheMontage 부재 무기는 VFX만)
	if (bWasWeaponOn)
	{
		MulticastPlayWeaponChangeActing(FromType, EVBWeaponType::None, /*bCombatStance=*/true);
	}
}

bool UVBWeaponStateComponent::AreEnemiesNearby() const
{
	if (!OwnerCharacter.IsValid()) return false;
	
	FCollisionShape        Shape = FCollisionShape::MakeSphere(GetWeaponStateConfig()->EnemyProximityRadius);
	TArray<FOverlapResult> Overlaps;

	GetWorld()->OverlapMultiByChannel(Overlaps,
	                                  OwnerCharacter->GetActorLocation(),
	                                  FQuat::Identity,
	                                  EnemyTraceChannel,
	                                  Shape);

	// IVBTargetable 인터페이스로 필터를 거른다
	for (auto& Overlap : Overlaps)
	{
		if (IVBTargetable* Targetable = Cast<IVBTargetable>(Overlap.GetActor()))
		{
			if (Targetable->IsTargetable())
				return true;
		}
	}

	return false;
}

EVBWeaponType UVBWeaponStateComponent::GetMMPoolForState(EVBLocomotionState InState) const
{
	// 미등록 무기는 기본 풀이다. 경고하지 않는 이유: 맨손(None)이 정상 경로로 여기 들어오고,
	// 무기별 MM 풀은 콘텐츠가 갖춰진 무기만 갖는 선택 사항이라 미지정이 결함이 아니다.
	const FVBWeaponSummonData* Data = WeaponDataMap.Find(CurrentWeaponType);
	if (!Data)
	{
		return EVBWeaponType::None;
	}

	// 상태 세 개는 DEC-006 이 고정한 구조라 늘어나지 않는다 - 여기서 갈라도 무기 추가에 끌려오지 않는다.
	switch (InState)
	{
	case EVBLocomotionState::ArmedCombat:      return Data->ArmedMMPool;
	case EVBLocomotionState::ArmedExploration: return Data->SheathedMMPool;
	default:                                   return EVBWeaponType::None; // 평상(무기 없음)
	}
}

void UVBWeaponStateComponent::ApplyWeaponTags(EVBWeaponType InWeaponType, bool bAdd)
{
	if (!OwnerCharacter.IsValid()) return;

	UAbilitySystemComponent* ASC = OwnerCharacter->GetAbilitySystemComponent();

	if (!ASC) return;

	// 맨손은 소속 태그가 없는 것이 정상이다 - 무기 데이터 자체가 없으므로 아래 조회로 내려가면 경고가 오발한다.
	if (InWeaponType == EVBWeaponType::None)
	{
		return;
	}

	// 무기별 태그는 WeaponDataMap 이 소유한다 - 무기를 추가할 때 고칠 곳이 데이터 한 행으로 끝난다.
	// 종전 C++ switch 는 default 로 조용히 빠져나가 State.Armed 조차 안 붙었다(FIND-087).
	const FVBWeaponSummonData* Data = WeaponDataMap.Find(InWeaponType);
	if (!Data || !Data->ArmedTag.IsValid())
	{
		// 조용히 넘기지 않는다: 증상이 "무기를 들었는데 전투로 인식되지 않는다"로만 나타나
		// 어빌리티 게이트와 애니 분기가 통째로 어긋나고, 원인이 여기라는 단서가 화면에 없다.
		VB_LOG(Warning, "무기 %s 의 ArmedTag 미지정 - State.Armed 부여 불가. BP_VBCharacter 의 WeaponDataMap 확인",
		       *UEnum::GetValueAsString(InWeaponType));
		return;
	}

	FGameplayTagContainer Tags;
	Tags.AddTag(VBGameplayTags::State_Armed);
	Tags.AddTag(Data->ArmedTag);
	
	if (bAdd)
	{
		ASC->AddLooseGameplayTags(Tags, 1, EGameplayTagReplicationState::CountToOwner);
	}
	else
	{
		ASC->RemoveLooseGameplayTags(Tags, 1, EGameplayTagReplicationState::CountToOwner);
	}
}

void UVBWeaponStateComponent::ApplyLocomotionCMCState(EVBLocomotionState InState)
{
	if (!OwnerCharacter.IsValid()) return;
	
	// 회전 플래그의 단일 진실원은 VBCharacter::ApplyRotationMode (Sprint/LockOn/상태 우선순위 통합).
	// 여기서 CMC 플래그를 직접 쓰면 이중 작성자 — 06-07 PIE 플래그 배터리에서 직접 쓰기가
	// ApplyRotationMode 에 덮여 무효임을 실측하고 위임으로 정리. 상태별 분할 정책은 그쪽 주석 참조.
	OwnerCharacter->ApplyRotationMode();
}
