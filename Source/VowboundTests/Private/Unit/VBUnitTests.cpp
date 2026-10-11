// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Character/VBTargetLockComponent.h"
#include "Data/VBTargetLockConfig.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBWeaponStateConfig.h"
#include "Data/VBMedicalConfig.h"
#include "Data/VBFinisherWindowConfig.h"
#include "Data/VBMovementConfig.h"
#include "Data/VBCameraConfig.h"
#include "Data/VBEnemyConfig.h"
#include "Data/VBCombatSettings.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h" // 공격 GA 자산 태그 계약 시험
#include "UObject/UObjectIterator.h"                      // 파생 클래스 전수 열거
#include "Player/VBPlayerState.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"

// plain cpp(UHT 무관)라 가드 가능 — 펑셔널 트랙(UCLASS)과 달리 단위 트랙은 표준 가드를 쓴다.
#if WITH_AUTOMATION_TESTS

// FIND-006 회귀 가드: LockConfig DataAsset 미할당이어도 GetTargetLockConfig 는 CDO fallback 으로
// 절대 null 을 반환하지 않는다 (설계된 fallback — VerifyReport_2026-06-05_b 에서 라이브 확인된 동작).
// 왜 월드 불요: 구현이 LockConfig 분기 + GetDefault<UVBTargetLockConfig>() 뿐 — 컴포넌트 등록 불필요.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTargetLockConfigFallbackTest,
	"Vowbound.Unit.Config.TargetLockCDOFallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTargetLockConfigFallbackTest::RunTest(const FString& Parameters)
{
	UVBTargetLockComponent* Comp = NewObject<UVBTargetLockComponent>(GetTransientPackage());
	TestNotNull(TEXT("component"), Comp);
	if (!Comp) return false;

	const UVBTargetLockConfig* Config = Comp->GetTargetLockConfig();
	TestNotNull(TEXT("GetTargetLockConfig() must never return null (CDO fallback)"), Config);
	return true;
}

// FIND-005 회귀 가드: AttackConfig 의 적중 trace 채널 디폴트는 ECC_Pawn (현행 동작 보존 계약 — f0ba45a).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAttackConfigDefaultsTest,
	"Vowbound.Unit.Config.AttackConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAttackConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBAttackConfig* CDO = GetDefault<UVBAttackConfig>();
	TestNotNull(TEXT("UVBAttackConfig CDO"), CDO);
	if (!CDO) return false;

	TestEqual(TEXT("HitTraceChannel default must stay ECC_Pawn"),
		CDO->HitTraceChannel.GetValue(), ECC_Pawn);
	// 데이터화 P0: 흡수된 리터럴 기본값 보존 계약(현행 20 = 리팩터 전 MinStandoff).
	TestEqual(TEXT("MinWarpStandoff default must stay 20"), CDO->MinWarpStandoff, 20.0f);
	// 답 기본값(design §11): 비면 예고 큐가 답 행을 못 찾아 경고를 찍고, DodgeOnly 가 기본이면 모든 공격이 가드를 뚫는다.
	TestTrue(TEXT("AttackAnswer default must be Attack.Answer.Parryable"),
		CDO->AttackAnswer == VBGameplayTags::Attack_Answer_Parryable);
	return true;
}

// 그룹 전투 회귀 가드 (2026-08-20 감사 I6).
// 왜 필요한가: DA_Enemy_Default 는 AttackAbilityInputTag 하나만 직렬화한다 - 나머지 값은 CDO 와
// 델타가 없어 이 헤더의 숫자가 곧 전 적의 실효값이다. 헤더 한 줄을 무심코 고치면 자산 변경 없이
// 모든 적의 행동이 조용히 바뀐다. 특히 MaxApproachHoldSeconds 를 줄이면 접근 중 토큰 회수가 잦아져
// 감사 C1 이 다른 형태로 재발한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyConfigDefaultsTest,
	"Vowbound.Unit.Config.EnemyConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBEnemyConfig* CDO = GetDefault<UVBEnemyConfig>();
	TestNotNull(TEXT("UVBEnemyConfig CDO"), CDO);
	if (!CDO) return false;

	TestEqual(TEXT("AttackTokenCost default must stay 1"), CDO->AttackTokenCost, 1);
	TestEqual(TEXT("AttackCommitDistance default must stay 180"), CDO->AttackCommitDistance, 180.0f);
	TestEqual(TEXT("AttackReleaseDistance default must stay 200"), CDO->AttackReleaseDistance, 200.0f);
	TestEqual(TEXT("TelegraphDuration default must stay 0.6"), CDO->TelegraphDuration, 0.6f);
	TestEqual(TEXT("MaxApproachHoldSeconds default must stay 8"), CDO->MaxApproachHoldSeconds, 8.0f);

	// 걸음 계약. 값 자체보다 «둘이 갈린다» 가 요점이다 - 같아지면 둘러쌀 때와 쫓아올 때가 구분되지 않는다.
	TestTrue(TEXT("gait contract: WalkSpeed must be slower than RunSpeed"), CDO->WalkSpeed < CDO->RunSpeed);

	// 걸음 문턱은 쌍이라야 경계에서 안 떤다. 같거나 뒤집히면 매 주기 걸음이 왕복한다.
	TestTrue(TEXT("gait hysteresis: RunExitDistance must be shorter than RunEnterDistance"),
		CDO->RunExitDistance < CDO->RunEnterDistance);

	// 값 자체가 아니라 값들 사이의 계약이다. 뒤집히면 역할이 재계산 주기마다 왕복하는 발진기가 되고
	// 런타임 로그는 한 줄도 안 남는다. IsDataValid 가 자산 저작을 막지만, C++ 디폴트는 그 사정권 밖이다.
	TestTrue(TEXT("hysteresis contract: Release must exceed Commit"),
		CDO->AttackReleaseDistance > CDO->AttackCommitDistance);

	// 접근 상한이 정상 접근 시간보다 짧으면 모든 적이 붙기 전에 토큰을 뺏긴다.
	// 식은 UVBEnemyConfig::GetNormalApproachSeconds() 하나에만 산다 - 종전에는 이 테스트와 IsDataValid 가
	//   같은 4줄을 각자 들고 있었고, 그러면 걸음 모델이 바뀔 때 한쪽만 고쳐져 테스트가 통과하며 거짓을
	//   말한다(2026-09-07 /verify 지적). 이 테스트가 보는 것은 C++ 디폴트, IsDataValid 는 저작된 자산이다.
	const float NormalApproachSeconds = CDO->GetNormalApproachSeconds();
	TestTrue(TEXT("approach budget must cover a normal approach from max sight range"),
		CDO->MaxApproachHoldSeconds > NormalApproachSeconds);

	return true;
}

// 그룹 전투 전역 노브의 불변식 가드 (2026-08-20 감사 I6).
//
// 왜 '값이 N 이다'를 단언하지 않는가: UVBCombatSettings 는 config=Game 이라 GetDefault<>() 가
//   Config/DefaultGame.ini 를 병합한 값을 돌려준다. 즉 이 값들의 홈은 헤더가 아니라 ini 이고,
//   ini 는 튜닝하라고 있는 자리다. 정확한 값을 단언하면 정상적인 FEEL 튜닝이 빌드 게이트를 깨고,
//   그러면 이 테스트는 "고장을 알리는 장치"가 아니라 "고치기 귀찮은 장애물"이 된다.
//   (초판은 실제로 MaxAttackTokens==2 를 단언했다가 2 -> 1 튜닝에서 곧바로 걸렸다.)
//   대신 어떤 값을 넣어도 성립해야 하는 관계만 지킨다.
//
// UVBEnemyConfig 쪽에서 정확한 값을 단언하는 것은 반대 이유다 - 그쪽은 DataAsset 이라
//   GetDefault<>() 가 순수 C++ 디폴트이고, DA_Enemy_Default 가 대부분을 저작하지 않아
//   헤더 숫자가 곧 실효값이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatSettingsInvariantsTest,
	"Vowbound.Unit.Config.CombatSettingsInvariants",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatSettingsInvariantsTest::RunTest(const FString& Parameters)
{
	const UVBCombatSettings* Settings = GetDefault<UVBCombatSettings>();
	TestNotNull(TEXT("UVBCombatSettings CDO"), Settings);
	if (!Settings) return false;

	// 0 이면 아무도 영원히 공격하지 않는다. ClampMin 이 0 이라 에디터가 막지 못하는 값이다.
	TestTrue(TEXT("MaxAttackTokens must be positive or nobody ever attacks"),
		Settings->MaxAttackTokens > 0);

	// 0 이면 타이머가 걸리지 않아 그룹이 영원히 정지한다.
	TestTrue(TEXT("SlotRecomputeInterval must be positive or groups never recompute"),
		Settings->SlotRecomputeInterval > 0.0f);

	// 공격 행동 상한이 재계산 주기보다 짧으면 정상 공격이 매 주기 만료로 잡힌다.
	TestTrue(TEXT("MaxTokenHoldSeconds must exceed the recompute interval"),
		Settings->MaxTokenHoldSeconds > Settings->SlotRecomputeInterval);

	// 슬롯이 0 이면 대기자 배정 자체가 성립하지 않는다.
	TestTrue(TEXT("SurroundSlotCount must be positive"), Settings->SurroundSlotCount > 0);

	return true;
}

// 데이터화 P0 회귀 가드: 흡수된 리터럴 기본값이 현행값과 동일해야 한다(FEEL 드리프트 방지).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponStateConfigDefaultsTest,
	"Vowbound.Unit.Config.WeaponStateConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponStateConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBWeaponStateConfig* CDO = GetDefault<UVBWeaponStateConfig>();
	TestNotNull(TEXT("UVBWeaponStateConfig CDO"), CDO);
	if (!CDO) return false;
	TestEqual(TEXT("CombatExitIdleSpeedThreshold default must stay 10"), CDO->CombatExitIdleSpeedThreshold, 10.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTargetLockConfigDefaultsTest,
	"Vowbound.Unit.Config.TargetLockConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTargetLockConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBTargetLockConfig* CDO = GetDefault<UVBTargetLockConfig>();
	TestNotNull(TEXT("UVBTargetLockConfig CDO"), CDO);
	if (!CDO) return false;
	// VBCharacter 에서 이동된 값 — 드리프트 시 타겟 전환 FEEL 이 바뀐다.
	TestEqual(TEXT("SwitchTargetMouseThreshold default must stay 1.5"), CDO->SwitchTargetMouseThreshold, 1.5f);
	TestEqual(TEXT("SwitchTargetCooldown default must stay 0.3"), CDO->SwitchTargetCooldown, 0.3f);
	TestEqual(TEXT("SearchTraceChannel default must stay ECC_Pawn"), CDO->SearchTraceChannel.GetValue(), ECC_Pawn);
	return true;
}

// 데이터화 P1.2 회귀 가드: VBCharacter 에서 이동된 이동/속도 값이 현행값과 동일해야 한다(FEEL 드리프트 방지).
// CDO fallback + ResolveProfile 3분기(Unarmed/ArmedDefault/BigSword override) + sprint 램프 플래그를 검증.
// P1.1 카메라 데이터화 — CDO 폴백이 데이터화 이전 in-class 값과 동일해야 한다.
// 왜 필요한가: DA 를 안 만들거나 BP 배선을 잊으면 조용히 CDO 로 돌아간다(FIND-045 1차 리스크 = 미배선 트랩).
// 그때도 카메라가 이전과 똑같이 동작함을 여기서 못 박는다 — 값이 바뀌면 이 테스트가 먼저 깨진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCameraConfigDefaultsTest,
	"Vowbound.Unit.Config.CameraConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCameraConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBCameraConfig* CDO = GetDefault<UVBCameraConfig>();
	TestNotNull(TEXT("UVBCameraConfig CDO (fallback 원천)"), CDO);
	if (!CDO) return false;

	TestEqual(TEXT("TargetArmLength default 400"), CDO->TargetArmLength, 400.0f);
	TestEqual(TEXT("CameraLagSpeed default 10"), CDO->CameraLagSpeed, 10.0f);
	TestEqual(TEXT("CameraLagMaxDistance default 200 (GASP 정렬)"), CDO->CameraLagMaxDistance, 200.0f);
	TestEqual(TEXT("FieldOfView default 75"), CDO->FieldOfView, 75.0f);
	// FVector 성분은 UE5 에서 double — double 리터럴로 비교(TestEqual(double,float) 는 C2666 모호).
	TestEqual(TEXT("SocketOffset.X default 0"), CDO->SocketOffset.X, 0.0);
	TestEqual(TEXT("SocketOffset.Y default 50 (어깨 너머)"), CDO->SocketOffset.Y, 50.0);
	TestEqual(TEXT("SocketOffset.Z default 80"), CDO->SocketOffset.Z, 80.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMovementConfigDefaultsTest,
	"Vowbound.Unit.Config.MovementConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMovementConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBMovementConfig* CDO = GetDefault<UVBMovementConfig>();
	TestNotNull(TEXT("UVBMovementConfig CDO (fallback 원천)"), CDO);
	if (!CDO) return false;

	// 전역 CMC 물리 — 현행값 보존.
	TestEqual(TEXT("BrakingDecelerationWalking default 750"), CDO->BrakingDecelerationWalking, 750.0f);
	TestEqual(TEXT("MaxAcceleration default 800"), CDO->MaxAcceleration, 800.0f);
	TestEqual(TEXT("GroundFriction default 5"), CDO->GroundFriction, 5.0f);
	TestEqual(TEXT("SprintRampDuration default 0.6"), CDO->SprintRampDuration, 0.6f);
	TestEqual(TEXT("Original GASP slide entry threshold"), CDO->SlideStartSpeed, 380.0f);
	TestTrue(TEXT("SlideEndSpeed below start"), CDO->SlideEndSpeed < CDO->SlideStartSpeed);
	TestEqual(TEXT("Slope slide has no forced duration limit"), CDO->SlideMaxDuration, 0.0f);
	TestEqual(TEXT("NormalCrouchMaxWalkSpeed default 150"), CDO->NormalCrouchMaxWalkSpeed, 150.0f);
	// 회전율 — BUG-010 라이브 튜닝 대상. 기본값 보존. (FRotator.Yaw 는 UE5 에서 double → double 리터럴)
	TestEqual(TEXT("DefaultRotationRate.Yaw -1 (즉시 snap)"), CDO->DefaultRotationRate.Yaw, -1.0);
	TestEqual(TEXT("AirborneRotationRate.Yaw 200"), CDO->AirborneRotationRate.Yaw, 200.0);
	TestEqual(TEXT("ArmedGroundedRotationRate.Yaw 360"), CDO->ArmedGroundedRotationRate.Yaw, 360.0);

	// ResolveProfile — 저작된 무장 루프 속도와 CMC 프로파일이 함께 바뀌어야 한다.
	// (FVector 성분은 UE5 에서 double → double 리터럴. CrouchSpeed 는 float → f 리터럴.)
	const FVBMovementProfile& Unarmed = CDO->ResolveProfile(EVBWeaponType::None, /*bArmed=*/false);
	TestEqual(TEXT("Unarmed Run.X 500"), Unarmed.RunSpeeds.X, 500.0);
	TestEqual(TEXT("Unarmed Armed-diagonal 보정 미사용"), Unarmed.RunDiagonalSpeed, 0.0f);
	TestEqual(TEXT("Unarmed Walk.X 200"), Unarmed.WalkSpeeds.X, 200.0);
	TestEqual(TEXT("Unarmed CrouchSpeed 150"), Unarmed.CrouchSpeed, 150.0f);
	TestFalse(TEXT("Unarmed sprint 램프 off(단일 650)"), Unarmed.bSprintRampEnabled);
	TestEqual(TEXT("Unarmed SprintRun.X 650"), Unarmed.SprintRunSpeeds.X, 650.0);

	// Armed 기본(Fighter/Magic) — override 미등록 무기는 이 base 로 폴백.
	const FVBMovementProfile& Armed = CDO->ResolveProfile(EVBWeaponType::Fighter, /*bArmed=*/true);
	TestEqual(TEXT("Fighter Run.X 250"), Armed.RunSpeeds.X, 250.0);
	TestEqual(TEXT("Fighter RunDiagonal 225"), Armed.RunDiagonalSpeed, 225.0f);
	TestEqual(TEXT("Armed Walk.X 165"), Armed.WalkSpeeds.X, 165.0);
	TestEqual(TEXT("Armed CrouchSpeed 165"), Armed.CrouchSpeed, 165.0f);
	TestTrue(TEXT("Armed sprint 램프 on"), Armed.bSprintRampEnabled);
	TestEqual(TEXT("Armed SprintRun.X 550"), Armed.SprintRunSpeeds.X, 550.0);
	TestEqual(TEXT("Armed Sprint.X 640"), Armed.SprintSpeeds.X, 640.0);
	const FVBMovementProfile& Katana = CDO->ResolveProfile(EVBWeaponType::Katana, /*bArmed=*/true);
	TestEqual(TEXT("Katana RunDiagonal 240"), Katana.RunDiagonalSpeed, 240.0f);
	TestEqual(TEXT("Katana Sprint.X 680"), Katana.SprintSpeeds.X, 680.0);
	// Magic(override 미등록) 도 ArmedDefault 로 폴백.
	const FVBMovementProfile& Magic = CDO->ResolveProfile(EVBWeaponType::Magic, /*bArmed=*/true);
	TestEqual(TEXT("Magic(override 미등록) Run.X 250 폴백"), Magic.RunSpeeds.X, 250.0);

	// BigSword override — jog 대각선 240, Fast 485. 고속 2단 램프는 없다.
	const FVBMovementProfile& Big = CDO->ResolveProfile(EVBWeaponType::BigSword, /*bArmed=*/true);
	TestEqual(TEXT("BigSword Run.X 255"), Big.RunSpeeds.X, 255.0);
	TestEqual(TEXT("BigSword RunDiagonal 240"), Big.RunDiagonalSpeed, 240.0f);
	TestFalse(TEXT("BigSword sprint 램프 off(485 캡)"), Big.bSprintRampEnabled);
	TestEqual(TEXT("BigSword SprintRun.X 485"), Big.SprintRunSpeeds.X, 485.0);
	TestEqual(TEXT("BigSword Walk.X 165(Armed 동일)"), Big.WalkSpeeds.X, 165.0);
	return true;
}

// 처형 창 설정의 디폴트. 종전에는 이 자리가 UVBMedicalConfig::HitTraceChannel(ECC_Pawn) 을 지켰는데,
// 2026-09-11 에 대상 탐색이 라인 트레이스에서 「락온 우선 + 전방 원뿔」로 바뀌며 그 필드가 사라졌다.
// 지키던 뜻은 같다 - 「전투 규칙을 정하는 숫자가 조용히 바뀌지 않게」. 그 숫자의 홈이 옮겨 갔으므로
// 검사도 따라 옮긴다. 0.30 은 옮겨 오기 전 DA_Medical 자산의 실측값이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherWindowConfigDefaultsTest,
	"Vowbound.Unit.Config.FinisherWindowConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFinisherWindowConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBFinisherWindowConfig* CDO = GetDefault<UVBFinisherWindowConfig>();
	TestNotNull(TEXT("UVBFinisherWindowConfig CDO"), CDO);
	if (!CDO) return false;
	TestEqual(TEXT("처형 창 체력 문턱 디폴트 0.30"), CDO->HealthThreshold, 0.30f);
	TestTrue(TEXT("처형 창 길이는 양수여야 한다 - 0 이면 열리자마자 닫혀 처형이 영영 안 걸린다"),
	         CDO->WindowDuration > 0.0f);
	return true;
}

// 태그 레지스트리 무결성: 핵심 네이티브 태그가 모듈 로드 시점에 정상 등록되는지.
// 왜 extern 직접 검사: RequestGameplayTag(name) 은 오타도 같이 검증해야 하지만, 네이티브 태그는
// UE_DEFINE_GAMEPLAY_TAG 가 등록을 보장 — extern 심볼의 GetTag().IsValid() 가 등록 성공의 직접 증거.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGameplayTagsRegisteredTest,
	"Vowbound.Unit.Tags.CoreTagsRegistered",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGameplayTagsRegisteredTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Ability_Attack_Light"),   VBGameplayTags::Ability_Attack_Light.GetTag().IsValid());
	// Ability_Attack_Heavy 는 DEC-009(강공격 폐지, RMB→가드)로 제거됨 — 가드 태그로 교체.
	TestTrue(TEXT("Input_Combat_Guard"),     VBGameplayTags::Input_Combat_Guard.GetTag().IsValid());
	TestTrue(TEXT("State_Combat_Guarding"),  VBGameplayTags::State_Combat_Guarding.GetTag().IsValid());
	TestTrue(TEXT("Ability_Defense_Dodge"),  VBGameplayTags::Ability_Defense_Dodge.GetTag().IsValid());
	TestTrue(TEXT("Ability_Medical_Purify"), VBGameplayTags::Ability_Medical_Purify.GetTag().IsValid());
	TestTrue(TEXT("State_Dead"),             VBGameplayTags::State_Dead.GetTag().IsValid());
	TestTrue(TEXT("Ability_Attack_Riposte"), VBGameplayTags::Ability_Attack_Riposte.GetTag().IsValid());
	return true;
}

// 공격 GA 는 전부 Ability.Attack 아래 잎을 들어야 한다.
//
// 왜 이 시험이 필요한가: 경직(UVBGA_Stagger)은 `BlockAbilitiesWithTag(Ability.Attack)` 하나로 공격을 막는데,
//   그 매칭은 GA 의 «자산 태그» 를 본다. 태그를 안 단 공격 GA 는 그 차단을 조용히 비껴간다 -
//   로그도 없고 컴파일도 통과하며, 증상은 「경직 중에도 그 공격만 나간다」로만 나타난다.
//   2026-09-07 /verify 가 `UVBGA_Riposte` 에서 실제로 그것을 찾았다. 그때까지 차단하는 쪽 주석은
//   「부모 하나로 Ability.Attack.* 전부가 걸린다」고 적고 있었다 - 사람이 읽어서는 안 갈리는 거짓이었다.
// 왜 이름 나열이 아니라 전수인가: 나열은 다음에 추가되는 공격 GA 를 못 본다. 그 «다음» 이 정확히
//   이 결함이 생기는 자리다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAttackAbilityAssetTagsTest,
	"Vowbound.Unit.Tags.AttackAbilitiesCarryAttackTag",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAttackAbilityAssetTagsTest::RunTest(const FString& Parameters)
{
	int32 Checked = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Cls = *It;
		if (!Cls->IsChildOf(UVBGA_MeleeAttackBase::StaticClass()))
		{
			continue;
		}
		// 추상 베이스와 BP 생성 클래스는 제외한다 - 베이스는 태그를 들 이유가 없고,
		//   BP 클래스는 이 테스트 프로세스에 로드돼 있지 않을 수 있어 결과가 실행마다 흔들린다.
		if (Cls->HasAnyClassFlags(CLASS_Abstract) || Cls->ClassGeneratedBy != nullptr)
		{
			continue;
		}
		const UGameplayAbility* CDO = Cls->GetDefaultObject<UGameplayAbility>();
		if (!CDO)
		{
			continue;
		}
		++Checked;
		TestTrue(*FString::Printf(TEXT("%s 는 Ability.Attack 아래 자산 태그를 들어야 한다"), *Cls->GetName()),
			CDO->GetAssetTags().HasTag(VBGameplayTags::Ability_Attack));
	}
	// 열거가 0 이면 시험이 조용히 아무것도 안 본 것이다 - 그 침묵을 통과로 읽지 않는다.
	TestTrue(TEXT("공격 GA 를 하나 이상 검사했다"), Checked > 0);
	return true;
}

// 세이브 키 계약: SavedAttributes 는 저장/복원이 공유하는 유일한 목록이고, 그 키는 .sav 안에 문자열로 굳는다.
// 이 테스트가 막는 실패 셋 — 전부 런타임에는 조용하다:
//   1) 어트리뷰트 rename → 키가 바뀌어 구 세이브 값이 유실(FieldPath 리디렉트는 .sav 문자열을 못 살린다)
//   2) 목록 누락 → 그 값만 저장되지 않는데 로드는 성공한 것처럼 보인다
//   3) 상한(Max*) 재유입 → 세이브가 GE 튜닝을 되돌린다(FIND-078). 아래에서 부재를 못박는다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveAttributeKeysTest,
	"Vowbound.Unit.Save.AttributeKeys",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveAttributeKeysTest::RunTest(const FString& Parameters)
{
	// 배열의 홈은 AVBPlayerState 생성자라 CDO 로 읽는다(월드/ASC 등록 불요).
	const AVBPlayerState* CDO = GetDefault<AVBPlayerState>();
	TestNotNull(TEXT("AVBPlayerState CDO"), CDO);
	if (!CDO) return false;

	const TArray<FGameplayAttribute>& Saved = CDO->GetSavedAttributes();
	TestTrue(TEXT("SavedAttributes 가 비어 있지 않다"), Saved.Num() > 0);

	TArray<FName> Keys;
	Keys.Reserve(Saved.Num());
	for (const FGameplayAttribute& Attribute : Saved)
	{
		TestTrue(TEXT("SavedAttributes 원소가 전부 유효하다"), Attribute.IsValid());
		if (Attribute.IsValid())
		{
			Keys.Add(AVBPlayerState::MakeAttributeSaveKey(Attribute));
		}
	}

	// 구 세이브 호환 계약 — 이 세 키는 2026-08-09 이전 .sav 에 이미 들어 있다. 바뀌면 값이 유실된다.
	TestTrue(TEXT("키 'Health' 보존"),     Keys.Contains(FName(TEXT("Health"))));
	TestTrue(TEXT("키 'Shield' 보존"),     Keys.Contains(FName(TEXT("Shield"))));
	TestTrue(TEXT("키 'Reputation' 포함"), Keys.Contains(FName(TEXT("Reputation"))));

	// 상한은 세이브가 아니라 GE 가 소유한다(FIND-078). 다시 넣으면 세이브가 GE 튜닝을 되돌린다 —
	// 로드가 GE 적용보다 나중에 돌기 때문이고, 그 실패는 런타임에 조용하다("GE 를 고쳤는데 안 변한다").
	// 업그레이드로 상한이 오르게 되면 절대값이 아니라 증가분을 저장해야 하며, 그때 이 가드를 함께 고친다.
	TestFalse(TEXT("키 'MaxHealth' 부재"), Keys.Contains(FName(TEXT("MaxHealth"))));
	TestFalse(TEXT("키 'MaxShield' 부재"), Keys.Contains(FName(TEXT("MaxShield"))));

	// 상한이 있는 어트리뷰트는 복원 시 자를 대상이 지정돼 있어야 한다 - 빠지면 상한을 내렸을 때
	// base 가 상한을 넘은 채 남아 화면값과 다음 저장값이 갈린다.
	const TMap<FGameplayAttribute, FGameplayAttribute>& Caps = CDO->GetSavedAttributeCaps();
	TestEqual(TEXT("Health 의 상한은 MaxHealth"),
		Caps.FindRef(UVBHealthAttributeSet::GetHealthAttribute()), UVBHealthAttributeSet::GetMaxHealthAttribute());
	TestEqual(TEXT("Shield 의 상한은 MaxShield"),
		Caps.FindRef(UVBHealthAttributeSet::GetShieldAttribute()), UVBHealthAttributeSet::GetMaxShieldAttribute());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
