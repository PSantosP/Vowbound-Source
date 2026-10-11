// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h" // AttackAbilityInputTag - FGameplayTag 를 값으로 보관
#include "StructUtils/InstancedStruct.h" // OnKilledEffects
#include "Types/VBTeamTypes.h"
#include "VBEnemyConfig.generated.h"

class UAnimMontage;
class UVBEnemyAnimSet;
class UVBDeathDissolveConfig;
class UVBRagdollConfig;
class FDataValidationContext; // IsDataValid 인자 — 헤더에서 Misc/DataValidation.h 를 끌지 않는다

/**
 * 적(Enemy) 공통 튜닝 DataAsset.
 * 무엇: VBEnemyBase 의 이동/사망 수치를 외부화 — 여러 적 아키타입이 하나의 에셋을 공유해 중앙에서 튜닝.
 * 왜: 기존엔 값이 VBEnemyBase 클래스 디폴트에 박혀 적 BP 마다 복제됐다(DRY 위반).
 *     DataAsset 로 빼면 에디터에서 즉시 튜닝 가능 — 헤더 디폴트 변경은 Live Coding 으로 BP CDO 에
 *     반영되지 않는 한계를 회피(빌드/재시작 불필요).
 * 가정: VBEnemyBase 가 TObjectPtr<UVBEnemyConfig> 로 참조. 미할당 시 이 클래스의 C++ 디폴트(CDO)가 fallback.
 * 부작용: 없음(순수 데이터).
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBEnemyConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 소속 세력. 적 종류마다 다를 수 있으므로 코드가 아니라 여기가 홈이다 -
	// 새 세력의 적을 추가할 때 C++ 을 고치지 않기 위함이다(세력 목록 자체는 EVBTeam).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Team")
	EVBTeam Team = EVBTeam::Infected;

	// 이 적이 «무엇인가»(Enemy.Soldier, Enemy.Boss.Troll ...). 처치 사건(Event.Enemy.Killed)의 대상 태그로 실려
	//   퀘스트의 «병사 n 처치» 목표가 이것에 질의를 건다(.sw/narrative 12.4). 세력(Team)과 다른 축이다 - 같은 세력에 종류가 여럿이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(Categories="Enemy"))
	FGameplayTagContainer EnemyTags;

	// 플레이어가 죽였을 때 적용할 효과(서버, 처치 사건 뒤) - 적 드롭(«보상표 주기»), 이 적을 죽이면 켜지는 사실 같은 것(.sw/narrative 14.5).
	//   부른 플레이어 = 죽인 플레이어, 상대 = 죽은 적(보상표 주기가 넘친 몫을 그 발밑에 떨군다). 적끼리·환경에 죽은 것은 적용하지 않는다(처치 사건과 같은 규칙).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Quest", meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnKilledEffects;

	// === 걸음 (design .sw/enemy-gait/design.md) ===
	// 왜 둘인가: 둘러싸는 적과 쫓아오는 적이 같은 속도로 움직이면 군중이 한 덩어리로 읽힌다.
	//   걸음을 가르는 판단은 `UVBEnemyCombatComponent::UpdateGait` 가 하고(BTService 는 매 주기 부르기만
	//   한다), 그 두 속도의 홈은 여기다.
	// 왜 아키타입마다 다른가: 값이 맞아야 하는 상대가 이 적의 클립이기 때문이다. 병사 팩은 걷기 92
	//   조깅 306, UE5 템플릿 Unarmed 는 걷기 300 조깅 600 이다(2026-09-07 실측). 같은 숫자를 두 아키타입에
	//   쓰면 한쪽은 걷고 한쪽은 뛴다 - 실제로 종전 단일 300 이 그 상태였다.
	// 계약: WalkSpeed < RunSpeed 이고, RunSpeed 는 AnimSet->Locomotion 의 Speed 축 최대를 넘지 않는다.
	//   넘으면 블렌드스페이스가 축에서 잘려 다리는 그대로인 채 몸만 빨라진다(발 미끄러짐). IsDataValid 가 본다.

	// 걸을 때 속도(cm/s). 링 슬롯 근처에서 자리를 고르는 적, 사거리 직전에서 감속한 적이 이 속도다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement", meta=(ClampMin="0.0"))
	float WalkSpeed = 150.0f;

	// 뛸 때 속도(cm/s). 사거리 밖에서 쫓아올 때, 먼 링 슬롯으로 자리를 옮길 때가 이 속도다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement", meta=(ClampMin="0.0"))
	float RunSpeed = 300.0f;

	// 걸음 문턱은 쌍이다 - 진입과 이탈 거리를 다르게 둔다.
	// 왜 하나가 아닌가: 단일 문턱이면 경계에서 매 주기 걸음이 뒤집힌다. 이 시스템의 다른 거리 판정이
	//   전부 쌍인 것과 같은 이유다(SightRadius/LoseSightRadius, AttackCommitDistance/AttackReleaseDistance).
	//   걸음이 떨리면 블렌드스페이스가 속도를 그대로 받으므로 다리도 같이 떨고, «값이 바뀔 때만 찍는»
	//   로그가 경계에서는 초당 여러 줄로 퇴화해 원래 목적이 뒤집힌다.
	// 왜 역할별 표가 아닌가: 역할이 하나 늘 때마다 표에 줄이 늘고, 그 줄을 빠뜨리면 그 역할만 조용히
	//   기본 걸음이 된다. 「목표까지 남은 거리」 한 문장이면 역할이 늘어도 규칙은 그대로다.

	// 걷다가 뛰기 시작하는 거리(cm). 목표까지 더 가야 하는 거리가 이 값을 넘으면 뛴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement", meta=(ClampMin="0.0"))
	float RunEnterDistance = 250.0f;

	// 뛰다가 걸음으로 떨어지는 거리(cm). RunEnterDistance 보다 작아야 한다(IsDataValid 가 검사).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement", meta=(ClampMin="0.0"))
	float RunExitDistance = 180.0f;

	// 시야 끝에서 사거리까지 정상적으로 다가오는 데 걸리는 시간(초).
	// 왜 함수인가: 같은 식을 IsDataValid 와 단위 테스트가 각각 쓴다. 식을 두 곳에 적으면 걸음 모델이
	//   바뀔 때 한쪽만 고쳐지고, 그때 테스트는 통과하면서 거짓을 말한다(2026-09-07 /verify 지적).
	// 이상화된 값이다 - 직선 거리·즉시 가속·정지한 대상을 가정한다. 실제 접근은 내비 경로·회피·가속
	//   램프 때문에 반드시 이보다 오래 걸리므로, 호출부는 여유를 두고 비교한다.
	float GetNormalApproachSeconds() const;

	// 회전 속도 (CMC RotationRate — bUseControllerDesiredRotation AI 회전 경로)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	FRotator RotationRate = FRotator(0.0f, 360.0f, 0.0f);

	// 이 아키타입의 로코모션 클립 묶음 (design .sw/enemy-anim/design.md). ABP_Enemy_Base 를 쓰는 적은 필수 -
	//   비면 UVBEnemyAnimInstance 가 경고를 내고 참조 포즈로 선다. 옛 플레이어 ABP 를 아직 쓰는 적(감염체, 이전 전)은
	//   비어도 된다. 그래서 IsDataValid 는 경고만 낸다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Animation")
	TObjectPtr<UVBEnemyAnimSet> AnimSet;

	// 일반 사망 시 Instigator 명성 변동량 (음수 = 명성 하락)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	float DeathReputationChange = -5.0f;

	// 사망(래그돌) 후 액터 제거까지 대기 시간(초)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	float DeathCleanupDelay = 5.0f;

	// 사망 연출 몽타주. 비면 지금까지의 동작 그대로 - 죽는 순간 래그돌이다(잡몹 셋이 그렇다).
	// 차 있으면 하나를 무작위로 골라 재생하고, 끝난 자세에서 래그돌로 넘긴다(design .sw/boss/design.md §13.3).
	//   래그돌을 건너뛰지 않는 이유: 몽타주는 평평한 바닥을 가정하고 만들어졌으므로 경사나 시체 위에서는
	//   마지막 자세가 떠 있다. 물리로 넘겨야 지형에 눕는다.
	// 여러 개를 두는 이유: 보스는 한 번 죽지만 플레이어가 재시도를 반복하는 대상이라 매번 같으면 금방 질린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	TArray<TObjectPtr<UAnimMontage>> DeathMontages;

	// 래그돌을 이 뼈와 그 아래에만 건다. None = 바디 전부(지금까지의 동작, 표준 PA_Mannequin 을 쓰는 잡몹).
	// 왜 필요한가(2026-09-18): 트롤 물리 자산은 뿌리 뼈 SK_Mannequin 과 Mesh 에도 바디가 있고 그 둘의 자세가 발바닥 높이(z=0)다.
	//   바닥에 박힌 바디에 시뮬레이션을 켜면 솔버가 밀어내는 힘으로 시체가 솟는다(실측 3,515cm/s). 골반 아래만 걸면 그 둘은
	//   키네마틱으로 남는다. 자산에서 바디를 지우는 것이 근본 수정이고 이 필드는 그 전에도 뒤에도 서는 안전망이다.
	// 뼈가 골격에 없으면 런타임이 경고하고 전부 거는 쪽으로 떨어진다 - 조용히 안 걸리는 것보다 낫다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	FName RagdollRootBone;

	// «몰아지는 래그돌»(A 단계)을 쓰는가. GASP 의 BP_MovementMode_Ragdoll 에 해당하는 것으로,
	//   물리 바디가 앞장서고 캡슐이 매 틱 스윕 이동으로 그것을 쫓는다.
	// 끄면 지금까지의 동작 그대로 - 물리만 켜지고 캡슐은 죽은 자리에 남는다.
	// 왜 켜는 쪽이 기본인가: 끈 상태가 곧 「액터 위치와 보이는 몸이 갈라진 채로 둔다」이고,
	//   그 어긋남을 디졸브가 덮어 왔다. 그것이 user 가 「GASP 랑 전혀 다르다」고 한 자리다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	bool bUseDrivenRagdoll = true;

	// 골격과 물리 자산을 검증한 GASP powered ragdoll 설정. 비어 있으면 passive 물리만 사용한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	TObjectPtr<UVBRagdollConfig> PoweredRagdoll;

	// 래그돌이 «물려받은» 운동량을 남기는가. GASP 는 남긴다 - 쓰러지던 속도 그대로 넘어가야
	//   몸이 자기가 가던 방향으로 무너진다.
	// 끄면 진입 순간과 그 뒤 두 번 속도를 0 으로 지운다(2026-09-17~18 의 동작).
	//
	// 왜 켜는 쪽으로 뒤집었나(2026-09-20): 지우기는 「시체가 풍선처럼 솟는다」의 대증이었는데,
	//   그 원인 둘이 그 뒤에 따로 잡혔다 - 몽타주 블렌드아웃 «뒤» 에 물리를 켜서 일어서던 속도를
	//   물려받던 것(진입 시점을 블렌드아웃 직전으로 옮겨 해결)과, 트롤 물리 자산의 뿌리 바디가
	//   발바닥 높이(z=0)에 박혀 솔버가 밀어내던 것(RagdollRootBone 으로 해결).
	//   원인이 사라졌는데 대증이 남아 있으면 그 대증이 곧 「GASP 와 다른 점」이 된다.
	// 되돌리는 법: 솟는 것이 다시 보이는 아키타입만 이 값을 false 로 둔다. 전역으로 되돌리지 말 것 -
	//   그러면 어느 적에서 나는지 다시 모르게 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	bool bPreserveRagdollMomentum = true;

	// 캡슐이 쫓아갈 뼈. 비면 RagdollRootBone, 그것도 비면 "pelvis".
	//   GASP 의 SandboxCharacter_Mover_Ragdoll::Get_RagdollTransform 이 pelvis 소켓 변환을 읽는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	FName DrivenRagdollFollowBone;

	// A 단계를 끝낼 속도 문턱(cm/s). 모든 바디 중 최고 속도가 이 아래로 DrivenRagdollSettleFrames 만큼
	//   연속으로 내려가면 B 단계(보통 래그돌)로 넘긴다.
	// 왜 «결과» 가 아니라 «상태» 로 세나: 시간만으로 끊으면 긴 낙하에서 일찍 끊기고,
	//   한 프레임만 보면 솔버가 튀는 프레임에 속는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death", meta=(ClampMin="0.0"))
	float DrivenRagdollSettleSpeed = 40.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death", meta=(ClampMin="1"))
	int32 DrivenRagdollSettleFrames = 10;

	// A 단계의 상한(초). 문턱에 영영 안 닿는 경우(경사에서 계속 구르는 시체)에도 끝이 있어야 한다.
	//   상한이 없으면 비싼 틱이 정리될 때까지 돈다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death", meta=(ClampMin="0.1"))
	float DrivenRagdollMaxSeconds = 4.0f;

	// 캡슐 회전도 몸을 따라가는가. 끄면 위치만 따라간다(GASP 의 Ragdoll_FreezeCapsuleRotation 과 같은 자리).
	// 기본이 끔인 이유: 캡슐 회전의 주인이 이미 여럿이고(DEBT-011), 죽은 뒤의 회전을 읽는 소비자가 없다.
	//   켤 이유가 생기면 그때 주인 표를 먼저 갱신한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	bool bDrivenRagdollFollowsRotation = false;

	// 시체가 깎여 사라지는 연출의 표현 묶음. 비면 지금까지의 동작 그대로 - 정리 지연이 끝나면 그냥 없어진다.
	//
	// 왜 여기가 홈인가: 이 연출의 길이는 DeathCleanupDelay 안에 들어가야만 성립한다. 두 값이 다른 자산에
	//   흩어져 있으면 그 관계를 볼 수 있는 자리가 없어지고, 어긋남은 화면에서 «끝까지 안 사라지고 갑자기
	//   없어짐» 으로만 나타난다. 같은 자산에 두면 IsDataValid 가 저작 시점에 잡는다.
	// 소비자는 UVBDeathDissolveComponent 하나이고, AVBEnemyBase::EnterRagdoll 이 남은 정리 시간과 함께 건넨다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	TObjectPtr<UVBDeathDissolveConfig> DeathDissolve;

	// === 처형 ===

	// 이 적을 처형할 수 있나. false 면 빈사가 되어도 처형 대상에서 빠진다.
	//
	// 왜 여기인가(user 결정 2026-09-19): 「보스는 처형이 없지만 추후 애니메이션 구해줄게」.
	//   UVBBossConfig 가 이 클래스를 상속하므로 보스 설정에서만 끄면 잡몹은 기본값 그대로 간다.
	//   애니가 오면 켜고 Interaction Asset 을 저작하면 끝이라 코드 변경이 없다.
	// 왜 「창 GA 를 안 주기」가 아닌가: State.Combat.FinisherWindow 는 처형 말고도 쓰인다
	//   (피해 계산·반응 억제). 그것을 빼면 무엇이 같이 꺼지는지 추적이 어렵다.
	//   끄는 자리는 「처형 자격」 한 곳이어야 한다 - 판정은 UVBAbilitySystemStatics::FindFinisherTarget.
	// 기본 true 인 이유: 지금까지의 동작이 그것이고, 설정을 안 만든 적이 조용히 처형 불가가 되면
	//   「왜 이 적만 안 되지」를 추적하게 된다(규칙 27 - 모르는 경우는 지금 동작 쪽으로).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	bool bAllowFinisher = true;

	// === AI 시야 (AVBAIController 가 OnPossess 에서 소비) ===
	// 왜 컨트롤러가 아니라 적 자산에 있는가: 정찰병과 경비병이 같은 시야를 가지면 배치 설계가 성립하지 않는다.
	//   VBEnemyBase::EnemyConfig 가 EditDefaultsOnly 라 적 BP 마다 다른 자산을 꽂을 수 있으므로,
	//   아키타입별 차등은 코드 변경 없이 자산 하나 더 만드는 것으로 끝난다.
	// 주의: 이 값들은 반드시 런타임(OnPossess)에서 적용해야 한다. 컨트롤러 생성자에서 읽으면
	//   BP/자산이 준 값이 아직 존재하지 않아 영원히 C++ 디폴트로 굳는다.

	// 적을 인지하는 최대 거리(cm). 이 밖은 시야 쿼리 대상 자체가 아니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Perception")
	float SightRadius = 2000.0f;

	// 이미 본 대상을 놓치는 거리(cm). SightRadius 보다 커야 경계선에서 감지/해제가 떨리지 않는다.
	// 두 값의 차이가 곧 히스테리시스 폭이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Perception")
	float LoseSightRadius = 2500.0f;

	// 시야 반각(도). 정면 기준 좌우 각각 이 각도까지 본다 - 60이면 실제 시야는 120도다.
	// 반각이라는 사실을 놓치면 의도의 두 배로 넓어진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Perception")
	float PeripheralVisionAngleDegrees = 60.0f;

	// 감지 기억 유지 시간(초). 시야에서 사라진 자극이 이 시간 뒤 만료된다.
	// 짧으면 모퉁이 뒤로 숨는 즉시 추격이 끊기고, 길면 시야 밖에서도 계속 따라온다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Perception")
	float SightMaxAge = 5.0f;

	// === AI 전투 조율 (UVBEnemyCombatComponent / 코디네이터 / 공격 GA 가 소비) ===
	// 왜 전역 설정(UVBCombatSettings)이 아니라 여기인가: 이 값들은 아키타입마다 갈린다. 돌격형과 거대몹이
	//   같은 확정 거리·텔레그래프를 가지면 아키타입이라는 개념 자체가 성립하지 않는다.
	//   반대로 토큰 총량·슬롯 수는 그룹 전체의 사실이라 전역 설정이 홈이다.

	// 이 아키타입이 공격 한 번에 소비하는 토큰 코스트(D2). 잡몹 1, 대형몹은 2 이상을 줘
	//   "큰 놈이 붙으면 잔몹이 못 붙는다"를 자산만으로 표현한다.
	// 0 이하는 토큰 시스템을 무의미하게 만들므로(전원 동시 공격) 코디네이터가 1 로 바닥을 잡는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="1"))
	int32 AttackTokenCost = 1;

	// 이 거리 안으로 들어오면 접근을 멈추고 공격을 시작한다(cm).
	// 반드시 공격 자산(UVBAttackConfig)의 AttackRange 보다 짧아야 한다 - 길면 사거리 밖에서 헛스윙한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.0"))
	float AttackCommitDistance = 180.0f;

	// 이 거리 밖으로 벗어나야 공격 역할을 놓고 다시 접근한다(cm). 반드시 AttackCommitDistance 보다 크다.
	// 두 값의 차이가 곧 히스테리시스 폭이다 - SightRadius/LoseSightRadius 와 같은 규율이며,
	//   같게 두면 플레이어가 경계선에서 스트레이프할 때 역할이 매 주기 떨린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.0"))
	float AttackReleaseDistance = 200.0f;

	// 토큰이 없을 때 공격 사거리 밖으로 얼마나 물러나 대기하는가(cm).
	// 실제 대기 거리 = AttackCommitDistance + 이 값. 반경을 직접 적지 않고 사거리에 더하는 이유:
	//   "얼마나 물러나는가"는 그 적의 사거리에 상대적인 양이다. 절대 반경으로 두면 사거리가 다른 적을
	//   추가할 때마다 그 적이 자기 사거리 안에서 대기하게 되어 "누가 지금 위협인가"가 거리로 표현되지 않는다.
	// 종전에는 UVBCombatSettings::SurroundRadius(전역 250)였다. 전역값과 아키타입 사거리(180)의
	//   간격 70 은 아무도 정하지 않은 우연이었고, 적을 하나 더 만드는 순간 깨지는 구조였다.
	// 값이 클수록 대기자가 뒤로 빠져 전장이 넓어 보이고, 작을수록 에워싼 압박이 강해진다.
	//   기본값 70 은 종전 동작을 그대로 재현하는 값이다(250 - 180).
	// 계약: 이 값이 (AttackReleaseDistance - AttackCommitDistance) 보다 커야 한다.
	//   작으면 대기자가 공격 유지 거리 안에 서서 역할 히스테리시스가 깨진다. IsDataValid 가 검사한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.0"))
	float WaitingStandoff = 70.0f;

	// 공격 GA 활성화 후 실제 스윙이 나가기까지의 예고 시간(초, D3). 0 이면 예고 없이 즉시 스윙.
	// 플레이어가 회피 입력을 넣을 수 있는 창이 이 값이다 - 게임 난이도의 1차 노브다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.0"))
	float TelegraphDuration = 0.6f;

	// 이 아키타입의 공격이 끝난 뒤 그룹의 «다음 공격자» 가 나서기까지의 숨(초). 예고 게이트(TelegraphDuration)가 «예고끼리 안 겹침» 을 맡는다면
	//   이 값은 «스윙과 스윙 사이의 빈 시간» 을 맡는다 - 다른 개념이라 홈이 둘이 아니다. 참고작(아캄 계열 분석 3±0.5초, Sifu 동시 2 이하)의 «대화» 리듬.
	//   2026-09-06 user 「다같이 때리는 기분」: 실측 공격 간격 1~2초가 절반, 1초 안 겹침 5묶음/21회. 0 = 숨 없음(종전).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.0", ClampMax="10.0"))
	float PostAttackPauseSeconds = 1.2f;

	// 숨의 무작위 폭(초, ±). 같은 간격이 반복되면 리듬이 기계적으로 읽힌다 - 참고작도 ±0.5 를 둔다. 서버에서만 뽑는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.0", ClampMax="5.0"))
	float PostAttackPauseVariance = 0.4f;

	// 예고 중 재생할 몽타주(선택). 미할당이면 제자리에서 멈추는 것 자체가 예고다.
	// 시간(TelegraphDuration)이 이 몽타주의 길이를 따르지 않는 것은 의도다 - 길이를 따르면
	//   타이밍의 홈이 자산 두 곳(수치와 클립)이 되고, 클립을 갈아 끼울 때마다 난이도가 조용히 바뀐다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat")
	TObjectPtr<UAnimMontage> TelegraphMontage = nullptr;

	// 이 아키타입이 쓰는 공격 GA 의 InputTag. BT 노드의 AbilityTag 가 비어 있을 때 여기서 읽는다.
	// 왜 트리가 아니라 여기인가: 트리는 아키타입 전체가 공유하는데 공격 태그는 아키타입마다 갈린다.
	//   태그를 트리에 적으면 아키타입을 하나 더할 때 트리를 통째로 복제해야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(Categories="Input"))
	FGameplayTag AttackAbilityInputTag;

	// === 슈퍼아머 반격 (design .sw/enemy-soldier/design.md §3) - «맞다가 핑! 하는 강공격» ===
	// 참고작 규칙: 잡몹은 맞으면 끊기고, 맞으면서도 공격하는 예외는 표시된다. 이 셋이 그 예외를 아키타입 데이터로 만든다.
	// 슈퍼아머 반격 GA 의 InputTag. 비면 이 아키타입은 반격이 없다(감염체 잡몹). 그 GA 의 공격 자산이 bSuperArmorDuringAttack 과
	//   긴 예고(TelegraphDurationOverride)·빨강 답(DodgeOnly)을 든다 - 여기는 «어느 GA 인가» 만 정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(Categories="Input"))
	FGameplayTag ArmoredCounterAttackInputTag;

	// 아래 창 안에서 이만큼 맞으면 다음 공격이 슈퍼아머 반격이 된다. 0 = 끔. 태그가 있는데 0 이면 IsDataValid 가 잡는다.
	//   세는 것은 «히트리액트가 실제로 켜진 피격» 이다(UVBEnemyCombatComponent). 슈퍼아머 중 피격은 안 센다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0"))
	int32 ArmoredCounterAfterHits = 0;

	// 맞은 횟수를 세는 창(초). 창 밖의 피격은 버린다. 공격이 시작되면(반격이든 아니든) 횟수를 비운다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.1", ClampMax="10.0"))
	float ArmoredCounterWindowSeconds = 2.0f;

	// 토큰을 쥔 채 접근만 하는 시간의 상한(초). 넘으면 회수해 대기 중인 멤버에게 넘긴다.
	// 이것은 결함이 아니라 정상 재배분이라 로그도 Warning 이 아니라 Log 다.
	// 왜 전역(UVBCombatSettings)이 아닌가: 타당한 상한이 이 아키타입의 속도와 SightRadius 에
	//   달렸다. 느린 거대몹과 빠른 잔몹이 같은 상한을 쓰면 거대몹은 붙기도 전에 매번 토큰을 뺏긴다.
	//   반대로 MaxTokenHoldSeconds 는 '공격 GA 가 안 끝났다'를 잡는 결함 탐지기라 전역이 옳다 -
	//   두 값은 이름이 비슷하지만 성격이 다르다.
	// 저작 지침: 정상 접근 시간보다 커야 접근이 잘리지 않는다. 2026-09-07 부터 접근은 두 걸음이다 -
	//   마지막 RunExitDistance 만큼은 걷고 그 앞은 뛴다(design .sw/enemy-gait/design.md §2).
	//   식의 홈은 GetNormalApproachSeconds() 하나이고, IsDataValid 는 거기에 여유 1.25배를 곱해 비교한다.
	//   IsDataValid 가 이 부등식을 저작 시점에 검사한다 - 종전에는 C++ CDO 만 단위 테스트가 봤고
	//   자산은 아무도 안 봤다. 걸음이 둘로 갈리면서 느린 걷기를 쓰는 아키타입이 여기 걸릴 수 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.5"))
	float MaxApproachHoldSeconds = 8.0f;

	// 접근 워프가 클립의 전진을 늘이거나 줄여도 되는 배수(design .sw/boss/design.md §12.6). SkewWarp 는 애니를 두고 루트 이동만
	//   목적지에 맞추므로 «클립 전진 / 실제 이동» 이 1 에서 멀수록 발이 민다(2026-09-16 user 「슬라이딩」). 이 값이 1.5 면 전진 100cm 클립은
	//   67~150cm 만 가고, 그 밖의 거리는 워프 목적지를 그 경계에 세워 «닿지 않을지언정 미끄러지지는 않는다» 를 택한다.
	//   변형을 거리로 고른 뒤(UVBGA_EnemyMeleeAttack::SelectVariantIndex)의 잔차에만 걸린다. 1.0 = 워프가 이동을 전혀 안 바꿈.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="1.0", ClampMax="4.0"))
	float WarpLungeToleranceRatio = 1.5f;

#if WITH_EDITOR
	// 저장 시점에 자산 내부 계약을 검사한다. 둘 다 위반해도 런타임 로그가 한 줄도 안 남는 부류라
	//   에디터에서 이름을 붙여 주는 것이 유일한 조기 발견 경로다.
	// 시그니처: UE 5.8 현행은 const 버전(Object.h::UObject::IsDataValid, WITH_EDITOR 안).
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
