#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBWeaponVisualConfig.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UNiagaraSystem;
class UAnimMontage;

// 몽타주 재생 위치(초)로 평가하는 구간. 프레임 생략/재생 속도 변화에도 동기화된다.
USTRUCT(BlueprintType)
struct FVBWeaponVisualTiming
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Weapon|Timing", meta=(ClampMin="0")) float DissolveStart = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Weapon|Timing", meta=(ClampMin="0")) float DissolveEnd = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Weapon|Timing", meta=(ClampMin="0")) float FormationStart = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Weapon|Timing", meta=(ClampMin="0")) float FormationEnd = 0.0f;

	bool IsValid(float MontageLength) const
	{
		return FMath::IsFinite(DissolveStart) && FMath::IsFinite(DissolveEnd)
			&& FMath::IsFinite(FormationStart) && FMath::IsFinite(FormationEnd)
			&& DissolveStart >= 0.0f && DissolveEnd >= DissolveStart
			&& FormationStart >= DissolveEnd && FormationEnd >= FormationStart
			&& FormationEnd <= MontageLength;
	}

	static float EvaluateWindow(float Position, float Start, float End)
	{
		if (Position < Start) return 0.0f;
		if (End <= Start) return 1.0f;
		return FMath::Clamp((Position - Start) / (End - Start), 0.0f, 1.0f);
	}
	float GetOutgoingProgress(float Position) const { return 1.0f - EvaluateWindow(Position, DissolveStart, DissolveEnd); }
	float GetIncomingProgress(float Position) const { return EvaluateWindow(Position, FormationStart, FormationEnd); }
};


// 한 손에 붙는 외형. Fighter는 메시 없는 항목을 양손에 하나씩 둔다.
USTRUCT(BlueprintType)
struct FVBWeaponVisualPart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UNiagaraSystem> FormationEffect;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UNiagaraSystem> IdleEffect;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Socket = TEXT("hand_r");
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform AttachmentTransform;
	// 메시 로컬 공간에서 손잡이 -> 칼끝. 길이와 시작점은 메시 bounds에서 얻는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector RevealAxis = FVector::UpVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Color = FLinearColor(0.10f, 0.65f, 0.85f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector FormationScale = FVector(0.10f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector IdleScale = FVector(0.15f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector IdleOffset = FVector::ZeroVector;
	// Niagara별 모양 설정. 키는 User.Radius처럼 전체 파라미터 이름을 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName, float> IdleFloatParameters;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName, bool> IdleBoolParameters;
};

USTRUCT(BlueprintType)
struct FVBWeaponVisualStyle
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVBWeaponVisualPart> Parts;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.05")) float FormationDuration = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.05")) float DissolveDuration = 0.25f;
};

UCLASS(BlueprintType)
class VOWBOUND_API UVBWeaponVisualConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Weapons")
	TMap<EVBWeaponType, FVBWeaponVisualStyle> Styles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Weapons")
	TMap<TObjectPtr<UAnimMontage>, FVBWeaponVisualTiming> MontageTimings;
	// RPC가 프로퍼티 복제보다 먼저 도착한 경우의 코스메틱 유예. 이후 복제 상태로 수렴한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Weapons", meta=(ClampMin="0"))
	float ReplicationGracePeriod = 0.5f;
};
