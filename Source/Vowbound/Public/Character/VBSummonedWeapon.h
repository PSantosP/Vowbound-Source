#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/VBWeaponVisualConfig.h"
#include "VBSummonedWeapon.generated.h"

class UStaticMeshComponent;
class UNiagaraComponent;
class UMaterialInstanceDynamic;

// 로컬 코스메틱. 데미지/충돌/무기 상태의 소유자가 아니다.
UCLASS()
class VOWBOUND_API AVBSummonedWeapon : public AActor
{
	GENERATED_BODY()
public:
	AVBSummonedWeapon();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound 소환 무기") FVBWeaponVisualPart VisualPart;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound 소환 무기") TObjectPtr<UStaticMeshComponent> WeaponMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound 소환 무기") TObjectPtr<UNiagaraComponent> FormationFX;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound 소환 무기") TObjectPtr<UNiagaraComponent> IdleFX;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Vowbound 소환 무기") float ManifestProgress = 0.0f;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Vowbound 소환 무기") void RebuildVisual();
	UFUNCTION(BlueprintCallable, Category="Vowbound 소환 무기") void SetManifestProgress(float Progress);
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Vowbound 소환 무기") void PreviewSummon();
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

private:
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ManifestMaterial;
	FVector LocalAxis = FVector::UpVector;
	FVector BoundsCenter = FVector::ZeroVector;
	float ProjectionMin = 0.0f;
	float ProjectionMax = 1.0f;
	float PreviewElapsed = -1.0f;
};
