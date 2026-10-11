#include "Character/VBSummonedWeapon.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"

AVBSummonedWeapon::AVBSummonedWeapon()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot")));
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(RootComponent);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetGenerateOverlapEvents(false);
	WeaponMesh->SetCastShadow(false);
	FormationFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FormationFX"));
	FormationFX->SetupAttachment(RootComponent);
	FormationFX->SetAutoActivate(false);
	IdleFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("IdleFX"));
	IdleFX->SetupAttachment(RootComponent);
	IdleFX->SetAutoActivate(false);
}

void AVBSummonedWeapon::RebuildVisual()
{
	FormationFX->DeactivateImmediate();
	IdleFX->DeactivateImmediate();
	WeaponMesh->SetStaticMesh(VisualPart.Mesh);
	ManifestMaterial = VisualPart.Material ? UMaterialInstanceDynamic::Create(VisualPart.Material, this) : nullptr;
	for (int32 Slot = 0; Slot < WeaponMesh->GetNumMaterials(); ++Slot)
	{
		WeaponMesh->SetMaterial(Slot, ManifestMaterial);
	}
	LocalAxis = VisualPart.RevealAxis.GetSafeNormal();
	if (LocalAxis.IsNearlyZero()) LocalAxis = FVector::UpVector;
	const FBox Bounds = VisualPart.Mesh ? VisualPart.Mesh->GetBoundingBox() : FBox(FVector(-5), FVector(5));
	BoundsCenter = Bounds.GetCenter();
	const float HalfLength = FVector::DotProduct(Bounds.GetExtent(), LocalAxis.GetAbs());
	ProjectionMin = FVector::DotProduct(BoundsCenter, LocalAxis) - HalfLength;
	ProjectionMax = ProjectionMin + FMath::Max(2.0f * HalfLength, 1.0f);
	if (ManifestMaterial)
	{
		ManifestMaterial->SetVectorParameterValue(TEXT("RevealAxis"), FLinearColor(LocalAxis));
		ManifestMaterial->SetScalarParameterValue(TEXT("RevealMin"), ProjectionMin);
		ManifestMaterial->SetScalarParameterValue(TEXT("RevealLength"), ProjectionMax - ProjectionMin);
		ManifestMaterial->SetVectorParameterValue(TEXT("EnergyColor"), VisualPart.Color);
	}
	FormationFX->SetAsset(VisualPart.FormationEffect);
	FormationFX->SetRelativeScale3D(VisualPart.FormationScale);
	IdleFX->SetAsset(VisualPart.IdleEffect);
	for (const auto& Parameter : VisualPart.IdleFloatParameters) IdleFX->SetVariableFloat(Parameter.Key, Parameter.Value);
	for (const auto& Parameter : VisualPart.IdleBoolParameters) IdleFX->SetVariableBool(Parameter.Key, Parameter.Value);
	IdleFX->SetRelativeLocation(BoundsCenter + VisualPart.IdleOffset);
	IdleFX->SetRelativeScale3D(VisualPart.IdleScale);
	SetManifestProgress(0.0f);
}

void AVBSummonedWeapon::SetManifestProgress(float Progress)
{
	const float PreviousProgress = ManifestProgress;
	ManifestProgress = FMath::Clamp(Progress, 0.0f, 1.0f);
	WeaponMesh->SetVisibility(ManifestProgress > 0.0f && VisualPart.Mesh != nullptr);
	if (ManifestMaterial) ManifestMaterial->SetScalarParameterValue(TEXT("ManifestProgress"), ManifestProgress);
	// 재질의 경계와 같은 로컬 투영값. 손이 움직여도 두 표현이 벌어지지 않는다.
	const float Front = FMath::Lerp(ProjectionMin, ProjectionMax, ManifestProgress * 1.10f - 0.05f);
	FormationFX->SetRelativeLocation(BoundsCenter + LocalAxis * (Front - FVector::DotProduct(BoundsCenter, LocalAxis)));
	const bool bForming = ManifestProgress > 0.001f && ManifestProgress < 0.999f;
	if (bForming && VisualPart.FormationEffect)
	{
		FormationFX->SetVariableLinearColor(TEXT("User.Color"), VisualPart.Color * 1.8f);
		FormationFX->SetVariableLinearColor(TEXT("User.Color Secondary"), VisualPart.Color * 0.6f);
		if (!FormationFX->IsActive()) FormationFX->Activate(true);
	}
	else if (FormationFX->IsActive()) FormationFX->Deactivate();
	// 검은 미완성 부위에 오라가 먼저 나타나지 않는다. 맨손은 오라 자체가 차오른다.
	const bool bIdleVisible = VisualPart.Mesh ? ManifestProgress >= 0.999f : ManifestProgress > 0.001f;
	if (bIdleVisible && VisualPart.IdleEffect)
	{
		// 손 외피의 단발 입자는 생성 순간 색을 보관한다. 밝기는 처음부터 목표값으로,
		// 실체화는 아래 컴포넌트 크기로 표현해야 거의 검은 외피가 고정되지 않는다.
		const float Strength = VisualPart.Mesh ? 0.20f : 0.35f;
		IdleFX->SetVariableLinearColor(TEXT("User.Color"), VisualPart.Color * Strength);
		IdleFX->SetVariableLinearColor(TEXT("User.Color Secondary"), VisualPart.Color * Strength * 0.45f);
		IdleFX->SetRelativeScale3D(VisualPart.IdleScale * (VisualPart.Mesh ? 1.0f : FMath::Lerp(0.2f, 1.0f, ManifestProgress)));
		// 경계 시스템의 단발 외피는 생성 시 크기도 캡처한다. 완성 순간 한 번만
		// 다시 초기화해 최종 손 크기를 보장한다(진행 중 반복 재시작하지 않는다).
		const bool bHandCompleted = !VisualPart.Mesh && PreviousProgress < 0.999f && ManifestProgress >= 0.999f;
		if (!IdleFX->IsActive() || bHandCompleted) IdleFX->ReinitializeSystem();
	}
	else if (IdleFX->IsActive()) IdleFX->DeactivateImmediate();
	if (ManifestProgress <= 0.0f) FormationFX->DeactivateImmediate();
}

void AVBSummonedWeapon::PreviewSummon()
{
	RebuildVisual();
	PreviewElapsed = 0.0f;
}

void AVBSummonedWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PreviewElapsed >= 0.0f)
	{
		PreviewElapsed += DeltaSeconds;
		SetManifestProgress(PreviewElapsed / 1.5f);
		if (ManifestProgress >= 1.0f) PreviewElapsed = -1.0f;
	}
}
