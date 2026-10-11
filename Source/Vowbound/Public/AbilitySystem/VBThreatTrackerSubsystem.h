// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBThreatTrackerSubsystem.generated.h"

class APlayerController;

/**
 * FVBThreatEntry
 * 지금 예고 중인 적 하나. HUD 가 화면 밖 화살표를 그리는 데 필요한 것만 든다.
 */
USTRUCT()
struct FVBThreatEntry
{
	GENERATED_BODY()

	TWeakObjectPtr<AActor> Enemy;

	// 답(Attack.Answer.*). 지금은 색만 쓰지만 «회피만» 이면 화살표 모양을 바꾸는 확장의 키다.
	FGameplayTag Answer;

	// 답의 색. 큐가 연출 자산에서 읽어 실어 보낸다 - HUD 는 연출 자산을 모른다.
	FLinearColor Color = FLinearColor::White;

	double StartTime = 0.0;
};

/**
 * UVBThreatTrackerSubsystem
 * 클라이언트 쪽 «지금 나를 노리는 예고» 목록(design §11).
 *
 * 무엇: 예고 큐(UVBGC_Telegraph)가 OnActive 에서 등록하고 OnRemove 에서 뺀다. HUD 가 매 틱 읽어 화면 밖 화살표를 그린다.
 * 왜 서브시스템인가: 생산자(큐, CDO 라 상태를 못 가진다)와 소비자(HUD 위젯)가 서로를 몰라야 한다.
 *   큐가 HUD 를 직접 찾으면 PC·HUD 클래스에 묶이고, HUD 가 적 태그를 매 틱 훑으면 적 목록의 홈이 둘이 된다.
 *   UVBTimeDilationSubsystem 과 같은 판단이다.
 * 복제: 없다. 큐가 각 클라에서 따로 돌므로 이 목록도 클라마다 따로 선다. 서버(리슨)에서도 같은 코드가 돌고 무해하다.
 * 정리: 약참조라 죽은 적은 GetThreats 가 걷어낸다. 큐 Remove 를 못 받은 경우(적 파괴)도 여기서 사라진다.
 */
UCLASS()
class VOWBOUND_API UVBThreatTrackerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 같은 적을 다시 등록하면 갱신이다(중복 행 없음).
	void Register(AActor* Enemy, const FGameplayTag& Answer, const FLinearColor& Color);
	void Unregister(const AActor* Enemy);

	// 살아 있는 항목만 돌려준다. 죽은 약참조는 이 호출이 걷어낸다.
	const TArray<FVBThreatEntry>& GetThreats();

	// 월드 위치가 이 컨트롤러의 화면 안에 있는가. 카메라 뒤(투영 실패)도 밖이다.
	//   투영 좌표가 필요한 호출부(HUD)를 위해 OutScreenPos 를 선택으로 돌려준다(픽셀 단위, DPI 스케일 전).
	static bool IsOnScreen(const APlayerController* PC, const FVector& WorldLocation, FVector2D* OutScreenPos = nullptr);

private:
	TArray<FVBThreatEntry> Threats;
};
