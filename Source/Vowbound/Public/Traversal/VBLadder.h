// Copyright Vowbound. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBLadder.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * 오를 수 있는 사다리. 트래버설 컴포넌트가 이 액터에서 «축» 과 «끝» 을 읽는다.
 *
 * 왜 전용 액터인가 (user 결정 2026-09-19 「사다리는 사다리 전용이지 당연히」):
 *   등반은 평평한 면이라 트레이스만으로 충분했다. 사다리는 다르다 - 축이 있고 위아래 끝이 있는데
 *   트레이스로는 그 둘을 못 얻는다. 등반에서 벽 끝을 트레이스로 때우다가 몸통이 벽 위 허공으로
 *   나가는 것을 겪었고(SystemMap 09 「벽의 끝은 두 높이로 본다」), 사다리에서는 그 자리가 더 잦다.
 *
 * 좌표 관례는 등반의 벽면과 같다:
 *   - 액터의 앞(+X)이 «바깥 법선» 이다. 타는 사람은 그 반대(-X)를 본다.
 *   - 액터의 위(+Z)가 사다리 축이고, 액터 위치가 축의 바닥이다.
 *   - 높이는 0 에서 LadderHeight 까지다.
 */
UCLASS()
class VOWBOUND_API AVBLadder : public AActor
{
	GENERATED_BODY()

public:
	AVBLadder();

	virtual void OnConstruction(const FTransform& Transform) override;

	// 축 위의 한 점. Height 는 0(바닥)에서 LadderHeight(꼭대기) 사이다.
	FVector GetAxisPoint(float Height) const;

	// 바깥 법선. 타는 사람은 이 반대를 본다.
	FVector GetFacingNormal() const;

	FORCEINLINE float GetLadderHeight() const { return LadderHeight; }

	// 월드 한 점을 축 위 높이로 바꾼다. 범위 밖이면 잘라 준다.
	float ProjectToHeight(const FVector& WorldPoint) const;

protected:
	// 형체를 만든다. 세로대 둘 + 가로대 여럿을 인스턴스로 찍는다.
	//   왜 코드로 짓나: 사다리 길이가 레벨마다 다른데 길이별 메시를 만들면 자산이 늘고 길이가 자산에 박힌다.
	//   높이 하나만 바꾸면 가로대 수가 따라오는 편이 배치하는 사람에게 쉽다.
	void RebuildVisual();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	TObjectPtr<USceneComponent> Pivot;

	// 편집기에서 «타는 면» 쪽을 보여 주는 상자. 콜리전은 없다 - 찾기는 액터 검색으로 한다.
	//   왜 콜리전으로 안 찾나: 채널을 하나 더 만들면 표시를 두 곳(프로필과 액터)에 두게 된다.
	//   전용 액터를 만든 이유가 그 표시를 한 곳에 모으는 것이다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	TObjectPtr<UBoxComponent> Marker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	TObjectPtr<UInstancedStaticMeshComponent> Parts;

	// 세로대·가로대로 쓸 메시. 비우면 형체가 없다 - 그래도 사다리는 돈다(순수 시각물이다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	TObjectPtr<UStaticMesh> PartMesh;

	// 축 길이(cm). 액터 위치가 바닥이고 여기까지가 꼭대기다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	float LadderHeight = 400.0f;

	// 세로대 사이 간격(cm). 가로대의 길이이기도 하다.
	// 클립이 손발을 놓는 가로 위치는 실측 |X| = 26.4 다(AnimPose 2026-09-19, 세 클립 평균).
	//   그대로 쓰면 폭 53 인데 대가 손발에 딱 붙어 좁아 보인다 - user 요청으로 60, 다시 66 으로 넓혔다(2026-09-19).
	//   손발은 대보다 3.5cm 안쪽에 놓이는데, 가로대가 폭 전체를 가로지르므로 잡고 밟는 그림은 유지된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	float LadderWidth = 66.0f;

	// 가로대 간격(cm).
	// 40.3 은 클립이 한 번 잡을 때마다 오르는 높이 다(AnimPose 실측 2026-09-19):
	//   VB_Ladder_Up 이 7.2초에 골반을 241.5cm 올리고 그 사이 손을 여섯 번 다시 잡는다(왼 3 · 오른 3).
	//   가로대를 그 간격에 맞춰야 손이 가로대를 잡고 발이 가로대를 딛는다.
	//   30 으로 뒀을 때는 손이 대 사이 허공을 잡았다 - 눈에 바로 보이는 종류의 어긋남이다.
	// 같이 움직여야 하는 값: UVBClimbAnimSet::LadderSpeed(33.5). 둘 다 같은 클립에서 나왔다.
	//   클립을 갈면 둘 다 다시 잰다 - 하나만 고치면 손이 가로대를 스쳐 지나간다.
	// 40.255 = 클립의 루트 총 상승 241.53 을 한 주기의 디딤 횟수 6 으로 나눈 값이다.
	//   실측 디딤 간격은 40.13~40.28 로 흔들리는데 «총 상승 / 횟수» 를 쓰는 이유가 따로 있다:
	//   몽타주가 자기 섹션으로 도므로 한 주기가 끝나면 위상이 0 으로 돌아간다. 그때 간격이
	//   총 상승의 약수가 아니면 주기마다 어긋남이 쌓인다. 6 으로 나눈 값은 그 드리프트가 0 이다.
	//   개별 디딤은 최대 0.2cm 벗어나고 그건 IK 가 먹는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	float RungSpacing = 40.255f;

	// 가로대를 «보기에» 몇 칸으로 쪼갤지. 1 이면 디딤 간격 그대로, 2 면 그 절반마다 하나씩 더 놓는다.
	// 왜 그냥 간격을 줄이지 않나 (2026-09-19 user 「그 사이 틈새는 살짝만 내리고」):
	//   RungSpacing 은 클립이 한 번 디딜 때 오르는 높이 다. 그 값을 줄이면 발이 가로대를 비껴간다.
	//   정수로 쪼개면 안 깨진다 - 디딤이 여전히 가로대 «어느 하나» 위에 떨어지고, 사이 것은 눈요기다.
	//   실제 사다리도 그렇다: 빠르게 오를 때 발은 가로대를 하나씩 건너뛴다.
	// 2 면 보이는 간격이 20.13cm 다. 1 과 3(13.4cm) 사이에 정수가 없으므로 «살짝 좁히는» 선택지는 2 뿐이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	int32 RungSubdivision = 2;

	// 가로대가 세로대 면보다 얼마나 «뒤로» 뻗어 있나(cm). 타는 사람에게서 멀어지는 쪽이다.
	// 기본 0 = 평범한 사다리다. 세로대와 가로대가 같은 면에 있다.
	//
	// 왜 0 이 아닌 값이 있었나: 이 클립은 손과 발을 같은 면에 두지 않는다. 손은 root 기준 깊이
	//   36~40 에서 멈추고 발은 63~67 에서 멈춘다(실측 세 클립, 차이 27.10 / 26.79 / 25.12).
	//   그 26cm 를 IK 로 못 메우므로(상한 6cm 의 네 배) 한때 가로대를 뒤로 26.3 뻗어 두 면을 다 만들었다.
	// 왜 되돌렸나 (2026-09-19, user 「매달리는 반대의 방향에 툭 튀어나온것처럼 보여서」):
	//   접촉은 맞았지만 가로대가 타는 사람 반대쪽으로 물러나 있어 사다리로 안 보였다.
	//   맞는 것과 사다리로 보이는 것이 부딪히면 보이는 쪽을 택한다 - 그것이 이 자산의 일이다.
	//   대신 발이 세로대 면보다 약 25cm 뒤에서 멈춘다(벽에 붙여 두면 벽이 가린다).
	// 되살리려면 이 값을 26.3 으로 둔다. 반대쪽(타는 사람 쪽)으로 내밀고 싶으면 이 값은 그대로 0 으로 두고
	//   UVBClimbAnimSet::LadderDepth 를 63.5 로 올린 뒤 음수 깊이를 준다 - 그러면 발이 세로대 면에 서고
	//   손이 잡는 것이 27cm 앞으로 나온다. 어느 쪽이 나은지는 보고 정할 일이라 값만 남겨 둔다.
	// 곁들여 원본 클립도 같은 성질이고(16.9cm) 리타겟이 그 틈을 26.7 로 벌렸다(FIND-124, 배율 0.9435).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	float RungDepth = 0.0f;

	// 가로대 격자를 통째로 «아래로» 내리는 양(cm). 0 이면 바닥에 첫 가로대가 온다.
	// 왜 필요한가 (2026-09-19 user 사진 「조금만 더 내려야겠지?」):
	//   이 클립은 손이 잡는 높이와 발이 딛는 높이가 27.0cm 어긋나 있다(실측 - 손 189.80 / 발 82.29,
	//   간격 40.255 로 나눈 나머지 차이). 둘은 같은 격자에 못 올라간다.
	//   지금까지는 발에 맞췄고, 그러면 손이 바로 위 가로대보다 13.25cm 아래를 쥔다.
	// 그래서 «어느 쪽에 맞출지» 를 값으로 뺀다:
	//     0      = 발 기준 (발이 정확, 손이 13.25 아래를 쥠)
	//     6.6    = 중간    (둘 다 6.6 어긋남 - 말단 IK 가 닿는 범위)
	//     13.25  = 손 기준 (손이 정확, 발이 13.25 떠 있음)
	//   진짜 답은 말단 IK 다. 그것이 들어오면 이 값은 «IK 가 덜 일하게 하는» 튜닝으로 남는다.
	// 이 값은 형체뿐 아니라 정렬 계산에도 들어간다 - GetRungTopOffset() 이 같이 내려가므로
	//   꼭대기 한계·시작 위상·당겨 앉기가 전부 따라온다. 한 곳만 고치면 그 셋이 어긋난다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	float RungDropOffset = 6.6f;

	// 대의 굵기(지름, cm). 원통이라 지름이다.
	// 4 인 근거: 실제 사다리 가로대가 지름 3~4cm 다. 그보다 굵으면 손이 감쌀 수 없어 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Ladder")
	float PartThickness = 4.0f;

public:
	FORCEINLINE float GetRungSpacing() const { return RungSpacing; }
	FORCEINLINE int32 GetRungSubdivision() const { return RungSubdivision; }
	FORCEINLINE float GetLadderWidth() const { return LadderWidth; }
	FORCEINLINE float GetRungDepth() const { return RungDepth; }
	FORCEINLINE float GetPartThickness() const { return PartThickness; }

	FORCEINLINE float GetRungDropOffset() const { return RungDropOffset; }

	// 가로대 «윗면» 의 높이. 발이 닿는 자리라 배치·정렬이 다 이 값을 본다.
	//   중심이 i*RungSpacing - RungDropOffset 이고 두께의 절반만큼 위가 윗면이다.
	FORCEINLINE float GetRungTopOffset() const { return PartThickness * 0.5f - RungDropOffset; }
};
