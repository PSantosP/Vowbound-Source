// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AI/VBAIController.h"

#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "EngineUtils.h"                      // TActorIterator (사망 통지 순회)
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Vowbound/Vowbound.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"


AVBAIController::AVBAIController()
{
	// AIPerception 컴포넌트 생성
	// AIController에는 PerceptionComponent 프로퍼티가 있지만
	// 직접 생성해서 설정하는 것이 명확하다.
	SetPerceptionComponent(*CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception")));

	// Sight Sense 설정
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));

	// Affiliation 설정 - 구조적 설정이라 여기 남는다(튜닝값 아님).
	// 2026-08-14 중립 감지를 껐다. 팀이 생기기 전에는 모든 폰이 서로에게 Neutral 이라
	//   중립을 켜지 않으면 아무도 못 봤고, 그 대가로 적이 적을 유효 타겟으로 잡았다(user 관찰).
	//   이제 EVBTeam 이 있으므로 다른 팀만 Hostile 로 잡힌다. 우회를 걷어내는 것이 이 줄의 의미다.
	// 소품이 타겟이 되지 않는 이유는 IGenericTeamAgentInterface 를 구현하지 않기 때문이다.
	//   엔진은 그때만 Neutral 을 돌려주고(AIInterfaces.cpp::FGenericTeamId::GetAttitude 의 캐스트 실패 분기),
	//   bDetectNeutrals=false 가 그것을 거른다.
	// 주의: "팀이 None(255)이면 모두에게 Neutral" 은 거짓이다(2026-08-19 엔진 소스 정정).
	//   기본 해결자는 AIInterfaces.cpp::DefaultTeamAttitudeSolver 의 A != B ? Hostile : Friendly 뿐이라
	//   NoTeam 특례가 없다 - 255 대 0 은 Hostile 이다. 즉 팀 미설정 적도 플레이어를 정상 감지한다.
	SightConfig->DetectionByAffiliation.bDetectEnemies    = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals   = false;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

	// 시야 수치의 유일한 홈은 UVBEnemyConfig 다. 생성자 시점엔 폰이 없어 어느 적인지 모르므로
	//   클래스 CDO 로 시딩만 하고, 적별 실제 값은 OnPossess 에서 다시 적용한다.
	// 시딩을 굳이 하는 이유: 빙의 전 등록 구간에서 지각이 엔진 디폴트(3000/3500/90)로 돌지 않게 하고,
	//   에디터 Details 의 SightConfig 에도 프로젝트 의도값이 보이게 하기 위함이다.
	ApplyPerceptionConfig(GetDefault<UVBEnemyConfig>());

	GetPerceptionComponent()->SetDominantSense(UAISenseConfig_Sight::StaticClass());
}

void AVBAIController::ApplyPerceptionConfig(const UVBEnemyConfig* Config)
{
	if (!Config || !SightConfig || !GetPerceptionComponent())
	{
		return;
	}

	SightConfig->SightRadius                  = Config->SightRadius;
	SightConfig->LoseSightRadius              = Config->LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = Config->PeripheralVisionAngleDegrees; // 반각 - 60이면 정면 120도
	SightConfig->SetMaxAge(Config->SightMaxAge);

	// 값을 쓴 뒤 ConfigureSense 를 다시 부르는 것이 핵심이다. 숫자만 바꾸면 아무 일도 일어나지 않는다.
	// 지각 시스템은 리스너 등록 시 이 값들을 FDigestedSightProperties 사본으로 복사해 두고 매 쿼리에서
	//   사본만 읽는다. ConfigureSense 의 재설정 경로가 OnListenerConfigUpdated 를 태워 그 사본을 다시 만들고
	//   MaxAge 도 함께 갱신한다.
	GetPerceptionComponent()->ConfigureSense(*SightConfig);
}

void AVBAIController::BeginPlay()
{
	Super::BeginPlay();

	// 델리게이트 바인딩은 BeginPlay에서 해야한다.
	if (GetPerceptionComponent())
	{
		GetPerceptionComponent()->OnTargetPerceptionInfoUpdated.
		                          AddDynamic(this, &AVBAIController::OnTargetPerceptionInfoUpdated);
	}
	else
	{
		VB_LOG(Warning, "GetPerceptionComponent Is Null");
	}
}

void AVBAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// 시야 수치를 여기서 적용한다. 생성자에서 적용하면 자산이 준 값이 절대 반영되지 않는다 -
	//   생성자는 C++ 디폴트만 보는 시점이고, SightConfig 는 서브오브젝트라 BP CDO 직렬화도 덮지 못한다.
	//   종전 구조는 "에디터에 필드가 보이는데 바꿔도 아무 일도 안 일어나는" 상태였다.
	// BeginPlay 가 아니라 OnPossess 인 이유: 런타임 스폰 적은 SpawnActor 안에서 BeginPlay 가 먼저 끝나고
	//   그 뒤에 Possess 가 불린다(APawn::SpawnDefaultController). BeginPlay 시점엔 GetPawn() 이 null 이라
	//   어느 적의 config 를 써야 하는지 알 수 없다. PostInitializeComponents 는 더 이르므로 같은 이유로 탈락.
	// Super 뒤에 두는 이유: 지각 리스너의 body actor 가 폰이라 빙의가 끝난 뒤여야 한다.
	const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(InPawn);
	ApplyPerceptionConfig(Enemy ? Enemy->GetEnemyConfig() : GetDefault<UVBEnemyConfig>());

	// 컨트롤러의 팀을 폰에 맞춘다. 지각은 '듣는 쪽'의 팀을 컨트롤러에서 읽으므로,
	//   폰만 팀을 답하면 판정의 한쪽이 비어 전부 Neutral 로 떨어진다(= AI 가 아무도 못 본다).
	if (const IGenericTeamAgentInterface* TeamPawn = Cast<const IGenericTeamAgentInterface>(InPawn))
	{
		const FGenericTeamId PawnTeam = TeamPawn->GetGenericTeamId();
		SetGenericTeamId(PawnTeam);

		// 소속 없는 폰은 모두에게 중립이고 중립 감지를 껐으므로 이 AI 는 아무것도 못 본다.
		// 조용한 실패라 반드시 남긴다 - 증상이 'AI 가 가만히 있다'로만 나타난다.
		if (PawnTeam == FGenericTeamId::NoTeam)
		{
			VB_LOG(Warning, "%s: 폰 %s 의 팀이 None - 이 AI 는 아무도 감지하지 못한다. Config/BP 의 Team 확인",
			       *GetName(), *InPawn->GetName());
		}
	}

	// BehaviourTree 시작
	if (DefaultBehaviorTree)
	{
		RunBehaviorTree(DefaultBehaviorTree);
		VB_LOG(Log, "AI BT 시작: %s → %s",
		       *InPawn->GetName(), *DefaultBehaviorTree.GetName());
	}
}

void AVBAIController::OnTargetPerceptionInfoUpdated(const FActorPerceptionUpdateInfo& UpdateInfo)
{
	// Actor가 유효하지 않으면 무시한다.
	AActor* Actor = UpdateInfo.Target.Get();
	if (!Actor)
	{
		return;
	}

	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	if (UpdateInfo.Stimulus.WasSuccessfullySensed())
	{
		// 사망한 대상은 타겟으로 잡지 않는다. NotifyActorDied 가 이미 비운 타겟을 지각이 다시 채우는 것을 막는 재획득 방어.
		if (UVBAbilitySystemStatics::IsActorDead(Actor))
		{
			return;
		}

		// 감지 성공 -> Blackboard에 타겟 설정
		BB->SetValueAsObject(TargetActorKeyName, Actor);
		VB_LOG(Log, "AI 감지: %s → Target = %s", *GetName(), *Actor->GetName());
	}
	else
	{
		// 감지 해제 -> 타겟이 현재 BB의 타겟과 같으면 클리어
		if (BB->GetValueAsObject(TargetActorKeyName) == Actor)
		{
			BB->ClearValue(TargetActorKeyName);
			VB_LOG(Log, "AI 감지 해제: %s → Target cleared", *GetName());
		}
	}
}

void AVBAIController::NotifyActorDied(const UObject* WorldContextObject, AActor* DeadActor)
{
	if (!DeadActor)
	{
		return;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(
		WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return;
	}

	// 서버에서만 의미가 있다. AI 블랙보드는 서버 권위이고 클라의 BB 를 비워봐야 다음 복제에 덮인다.
	// 지금 유일한 호출부는 서버 전용 AVBCharacter::HandleDeath 라 이 가드는 중복이지만,
	// 이 함수는 static public 이라 호출부 규율에만 의존하는 구조다. 규율 의존형 방어는 지금 안 뚫려도
	// 결함이라는 판단을 게임오버 입력 차단(FIND-071)에 적용했으므로 여기에도 같게 적용한다.
	if (World->GetNetMode() == NM_Client)
	{
		return;
	}

	// 죽은 대상을 노리던 AI 만 타겟을 비운다. 다른 대상을 쫓던 AI 는 건드리지 않는다.
	// 지각 stimulus 는 그대로 살아 있지만 OnTargetPerceptionInfoUpdated 의 사망 게이트가 재획득을 막는다.
	int32 ClearedCount = 0;
	for (TActorIterator<AVBAIController> It(World); It; ++It)
	{
		UBlackboardComponent* BB = It->GetBlackboardComponent();
		if (BB && BB->GetValueAsObject(It->TargetActorKeyName) == DeadActor)
		{
			BB->ClearValue(It->TargetActorKeyName);
			++ClearedCount;
		}
	}

	if (ClearedCount > 0)
	{
		VB_LOG(Log, "사망 통지: %s 를 노리던 AI %d 기의 타겟 해제", *DeadActor->GetName(), ClearedCount);
	}
}
