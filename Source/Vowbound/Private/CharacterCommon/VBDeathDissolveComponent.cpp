// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "CharacterCommon/VBDeathDissolveComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/VBDeathDissolveConfig.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Data/VBDeathDissolveSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "TimerManager.h"
#include "Vowbound/Vowbound.h"


namespace
{
	// 파편이 올라온 뒤 메시를 숨길 때까지 기다리는 프레임 수.
	//
	// 왜 이 숫자인가: 정확한 값이 아니라 «넉넉한» 값이면 된다. 가루가 메시 표면을 덮으므로
	//   겹치는 프레임은 화면에 안 보이고, 모자란 프레임만 구멍이 된다 - 오차가 한쪽뿐이다.
	//   게임 스레드에서 켠 GPU 시뮬이 화면에 닿기까지 한두 프레임이 걸리므로 그보다 넉넉히 잡는다.
	//   60fps 에서 3프레임은 50ms 다. 그 사이 메시는 가루 밑에 있다.
	constexpr int32 GHideMeshAfterFrames = 3;

	// 파편 재질이 기본색을 받는 텍스처 파라미터 이름. M_VB_DeathDust 의 것과 같아야 한다.
	const FName GParamDebrisBaseTexture(TEXT("BaseTexture"));

	// 즉시 시작으로 볼 지연 하한(초). 타이머 rate 가 0 이하면 엔진이 SetTimer 를 clear 로 처리해
	// 콜백이 영영 오지 않는다 - 그 구간은 타이머에 맡기지 않고 직접 부른다.
	constexpr float GImmediateStartEpsilon = 0.01f;
}


UVBDeathDissolveComponent::UVBDeathDissolveComponent()
{
	// 진행은 GPU 가 센다. CPU 는 시작 순간에만 일한다.
	PrimaryComponentTick.bCanEverTick = false;
}

USkeletalMeshComponent* UVBDeathDissolveComponent::ResolveTargetMesh() const
{
	// 소유자를 ACharacter 로만 본다. AVBCharacter/AVBEnemyBase 는 공통 VB 베이스가 없어서
	// 구체 클래스를 캐스트하면 이 컴포넌트가 둘 중 한쪽 전용이 된다.
	// UVBHitFlashComponent 에 같은 함수가 있다. 세 번째가 나오면 그때 공통 헬퍼로 뽑는다(규칙 12).
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	return OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
}

UVBDeathDissolveConfig* UVBDeathDissolveComponent::ResolveConfig(UVBDeathDissolveConfig* Explicit)
{
	// 판단은 UVBDeathDissolveSettings::ResolveConfig 한 곳에 있다. 이 함수는 넘겨주기만 한다.
	return UVBDeathDissolveSettings::ResolveConfig(Explicit);
}

void UVBDeathDissolveComponent::ScheduleDissolve(float SecondsUntilCleanup, UVBDeathDissolveConfig* Config)
{
	Config = UVBDeathDissolveSettings::ResolveConfig(Config);

	// 설정이 없거나 길이가 0 이하면 이 연출은 꺼진 것이다. 길이 하나가 켜짐 여부를 겸한다.
	if (!Config || Config->Duration <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 정리 시각이 이미 지났거나 같으면 시작할 창이 없다. 억지로 시작하면 한 프레임 깜빡이고 사라진다.
	if (SecondsUntilCleanup <= GImmediateStartEpsilon)
	{
		VB_LOG(Warning, "사망 디졸브: %s 정리까지 %.2f초뿐이라 연출을 건너뛴다. DeathCleanupDelay 를 늘려라.",
		       *GetNameSafe(GetOwner()), SecondsUntilCleanup);
		return;
	}

	// 길이가 남은 시간보다 크면 줄인다. 조용히 자르지 않는 이유: 이것은 두 자산(정리 지연과 디졸브 길이)이
	//   서로 어긋났다는 신호다. 화면에는 「끝까지 안 사라지고 갑자기 없어짐」으로만 보이므로 말해줘야 한다.
	//   저작 시점에는 UVBEnemyConfig::IsDataValid 가 같은 관계를 미리 잡는다.
	PendingDuration = Config->Duration;
	if (PendingDuration > SecondsUntilCleanup)
	{
		VB_LOG(Warning, "사망 디졸브: %s 길이 %.2f초가 남은 정리 시간 %.2f초보다 길다 - 남은 시간으로 줄인다.",
		       *GetNameSafe(GetOwner()), PendingDuration, SecondsUntilCleanup);
		PendingDuration = SecondsUntilCleanup;
	}
	PendingConfig = Config;

	// 끝나는 순간이 정리 시각과 맞도록 시작을 역산한다. 정리 타이머는 호출자 것 하나 그대로다 -
	//   여기서 Destroy 를 부르면 사라지는 시각의 주인이 둘이 되고, 둘이 어긋나면 어떤 적은 다 사라지기 전에
	//   없어지고 어떤 적은 투명한 채로 남는다.
	const float StartDelay = SecondsUntilCleanup - PendingDuration;
	if (StartDelay <= GImmediateStartEpsilon)
	{
		BeginDissolve();
		return;
	}

	World->GetTimerManager().SetTimer(StartTimerHandle, this, &UVBDeathDissolveComponent::BeginDissolve, StartDelay, false);
}

void UVBDeathDissolveComponent::ResetDissolve()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StartTimerHandle);
	}

	// 아직 안 터진 숨기기 예약을 거둔다. 안 거두면 되돌린 직후에 그 예약이 몸을 다시 숨긴다 -
	//   시험 버튼을 연달아 누를 때 「되돌렸는데 잠깐 뒤 사라진다」로 나타난다.
	if (HideTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HideTickerHandle);
		HideTickerHandle.Reset();
	}

	// 숨긴 메시를 다시 보이게 한다. 이것을 빠뜨리면 「되돌렸는데 몸이 안 돌아온다」가 된다.
	if (USkeletalMeshComponent* Mesh = HiddenMesh.Get())
	{
		Mesh->SetVisibility(true, /*bPropagateToChildren*/ true);
	}
	HiddenMesh.Reset();

	// 아직 날아가는 파편도 같이 거둔다. 안 거두면 «되돌렸는데 가루는 계속 난다» 가 되어,
	//   되풀이해 보려고 만든 버튼이 오히려 직전 재생과 이번 재생을 섞어 보여준다.
	//   실제 사망 경로에서는 여기까지 오지 않는다 - 그쪽은 파편이 제 수명을 다 살아야 한다.
	if (UNiagaraComponent* Debris = SpawnedDebris.Get())
	{
		Debris->DestroyComponent();
	}
	SpawnedDebris.Reset();

	PendingConfig  = nullptr;
	PendingDuration = 0.0f;
}

void UVBDeathDissolveComponent::BeginDissolve()
{
	USkeletalMeshComponent* TargetMesh = ResolveTargetMesh();
	if (!TargetMesh || !PendingConfig)
	{
		return;
	}

	// 슬롯 머티리얼은 건드리지 않는다.
	//
	// 왜 뺐나 (2026-09-20, user: 「아주 살짝 있는 것 같은데」): 종전에는 슬롯마다 동적 머티리얼(MID)을
	//   만들어 `VB_Dissolve*` 를 심었다. 그런데 이제 같은 프레임에 메시를 숨기므로 그 값들이 화면에
	//   나타날 시간이 없다 - 하는 일이 없는 코드였다. 그러면서 스켈레탈 메시의 재질을 그리는 그 프레임에
	//   통째로 갈아끼우고 있었고, 그것이 남은 «아주 살짝» 의 자리로 가장 유력했다.
	// 값도 치른다: 슬롯 수만큼 MID 를 만드는 비용이 죽는 적마다 들었다.
	// 되살리려면: 이 자리에서 CreateAndSetMaterialInstanceDynamic 으로 슬롯마다 MID 를 만들고
	//   VB_DissolveStartTime/Duration/EdgeColor/EdgeWidth/EdgeGlow/AxisLocal/NoiseScale/Push 를 심은 뒤,
	//   아래 숨기기를 디졸브 길이만큼 미루면 된다. 그 머티리얼 그래프는 M_Mannequin 에 그대로 있다.
	//   다만 그 경로는 몬스터마다 마스터 머티리얼 작업을 요구한다 - 그래서 주 경로에서 내렸다.

	// 시작했다는 사실을 남긴다. 이 줄 하나가 오늘 하루 진단의 범위를 여러 번 좁혔다 -
	//   「파편이 안 나온다」가 왔을 때 이 줄이 있으면 예약·설정·호출 경로는 전부 지워지고
	//   남는 것이 «보이는가» 뿐이다. 없으면 그 앞을 통째로 다시 뒤져야 한다.
	VB_LOG(Log, "사망 디졸브 시작: %s 메시 %s, 길이 %.2f초",
	       *GetNameSafe(GetOwner()), *GetNameSafe(TargetMesh->GetSkeletalMeshAsset()), PendingDuration);

	SpawnDebris(TargetMesh);

	// 원본 메시를 숨긴다.
	//
	// 왜 깎지 않고 숨기나 (2026-09-20 구조 재설계): 몸이 깎이려면 그 몬스터의 마스터 머티리얼마다
	//   디졸브 그래프를 주입해야 한다. 마네킹만 돼 있고 보스 트롤은 마스터가 6개에 전부 Opaque 였다.
	//   즉 «깎기» 는 몬스터마다 자산 작업을 요구하는 경로라 새 몬스터에서 조용히 빠진다.
	//   파티클은 메시의 복제본이라 형태를 그대로 들고 있으므로, 원본을 숨기면 어떤 스켈레탈 메시에서도
	//   «가루가 됐다» 로 읽힌다 - 트롤에서 실제로 확인했다. 머티리얼을 하나도 안 요구하는 것이 요점이다.
	//
	// 왜 «맞추지» 않고 «겹치나» (2026-09-21, 세 번 고치고 나서):
	//   종전에는 메시가 사라지는 순간과 가루가 나타나는 순간을 프레임 단위로 맞추려 했다.
	//   그래서 세 번 고쳤다 - 첫 틱을 그 자리에서 돌리고, 재질 교체를 빼고, 크기 차오름을 껐다.
	//   셋 다 맞는 고침이었는데 user 는 여전히 「한 프레임 늦는 것 같다」고 했다.
	//   맞추는 일은 한 프레임만 어긋나도 보이는데, 어긋날 자리가 게임 스레드·GPU 시뮬·렌더로 여럿이다.
	// 그런데 이 연출은 애초에 맞출 필요가 없다. 가루는 메시 «표면» 에 나므로 몇 프레임 겹쳐 있어도
	//   메시를 가린다. 즉 메시가 늦게 사라지는 것은 공짜고, 일찍 사라지는 것만 구멍을 만든다.
	//   그러면 정확도를 올릴 게 아니라 오차를 한쪽으로 몰면 된다 - 늦는 쪽으로.
	// 왜 코어 티커인가: 월드 타이머는 에디터 월드에서 안 돈다(시험 버튼이 몸을 안 숨겼던 원인).
	//   FTSTicker 는 엔진 루프에 걸리므로 에디터와 PIE 에서 똑같이 돈다.
	//   몇 프레임을 기다리는지는 중요하지 않다 - 오차가 한쪽이라 넉넉히 두면 된다.
	HiddenMesh = TargetMesh;

	// 앞선 예약이 남아 있으면 먼저 거둔다. 안 거두면 핸들이 새것으로 덮여
	//   ResetDissolve 가 옛 예약을 취소할 수 없게 된다 - 되돌린 뒤에 그것이 몸을 다시 숨긴다.
	if (HideTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HideTickerHandle);
	}

	int32 FramesLeft = GHideMeshAfterFrames;
	HideTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
		[this, FramesLeft](float) mutable -> bool
		{
			if (--FramesLeft > 0)
			{
				return true;  // 아직 기다린다
			}

			if (USkeletalMeshComponent* Mesh = HiddenMesh.Get())
			{
				Mesh->SetVisibility(false, /*bPropagateToChildren*/ true);
			}
			return false;     // 한 번 하고 끝
		}));
}

UTexture* UVBDeathDissolveComponent::ResolveDebrisTexture(const USkeletalMeshComponent* TargetMesh)
{
	if (!TargetMesh)
	{
		return nullptr;
	}

	// 삼각형이 가장 많은 섹션의 재질을 고른다. 그 이유는 선언부 주석에 있다.
	int32 BestMaterialIndex = 0;
	if (const USkeletalMesh* Asset = TargetMesh->GetSkeletalMeshAsset())
	{
		if (const FSkeletalMeshRenderData* RenderData = Asset->GetResourceForRendering())
		{
			if (RenderData->LODRenderData.Num() > 0)
			{
				uint32 BestTriangles = 0;
				for (const FSkelMeshRenderSection& Section : RenderData->LODRenderData[0].RenderSections)
				{
					if (Section.NumTriangles > BestTriangles)
					{
						BestTriangles = Section.NumTriangles;
						BestMaterialIndex = Section.MaterialIndex;
					}
				}
			}
		}
	}

	UMaterialInterface* Material = TargetMesh->GetMaterial(BestMaterialIndex);
	if (!Material)
	{
		return nullptr;
	}

	// 그 재질이 쓰는 텍스처를 훑는다.
	//
	// 왜 이름 있는 파라미터를 안 쓰나: 마네킹은 머티리얼 인스턴스라 「Base Texture」 로 읽히지만
	//   트롤은 평범한 머티리얼이라 그런 파라미터가 아예 없다(2026-09-20 실측). 이름에 기대면 절반만 된다.
	// 5인자 GetUsedTextures 는 5.7 에서 폐기됐고 본문이 비어 있다(`final {}`) - 부르면 컴파일은 되고
	//   빈 배열이 온다. 조용히 틀리는 자리라 여기 적어 둔다.
	TArray<UTexture*> Used;
	Material->GetUsedTextures(Used);

	// 기본색을 고른다. 노멀맵은 색이 아니므로 거른다 - 색으로 쓰면 파편이 보랏빛이 된다.
	//   이름이 기본색을 뜻하는 것이 있으면 그것을 먼저 쓴다. 없으면 첫 2D 텍스처로 떨어진다.
	//   순서에만 기대면 마스크 텍스처(MRA 따위)가 뽑혀 파편이 잿빛이 된다.
	UTexture2D* Fallback = nullptr;
	for (UTexture* Texture : Used)
	{
		UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
		if (!Texture2D || Texture2D->CompressionSettings == TC_Normalmap)
		{
			continue;
		}

		const FString Name = Texture2D->GetName();
		if (Name.EndsWith(TEXT("_D")) || Name.Contains(TEXT("BaseColor")) ||
		    Name.Contains(TEXT("Albedo")) || Name.Contains(TEXT("Diffuse")))
		{
			return Texture2D;
		}

		if (!Fallback)
		{
			Fallback = Texture2D;
		}
	}

	return Fallback;
}

void UVBDeathDissolveComponent::SpawnDebris(USkeletalMeshComponent* TargetMesh)
{
	if (!PendingConfig || !PendingConfig->DebrisEffect || !TargetMesh)
	{
		return;
	}

	// 월드에 띄운다. 메시에 붙이지 않는다.
	//
	// 왜 붙이면 안 되나 (2026-09-20 엔진 소스로 확정): SpawnSystemAttached 는 컴포넌트의 소유자를
	//   붙일 대상의 액터로 잡는다 - NiagaraFunctionLibrary.cpp::SpawnSystemAttachedWithParams 가
	//   AttachToComponent->GetOwner() 를 넘기고, CreateNiagaraSystem 이 그것을 NewObject 의 Outer 로 쓴다.
	//   Outer 가 적 액터이므로 적이 Destroy 되는 순간 아직 날아가던 파편까지 함께 파괴된다.
	//   디졸브는 «정리 시각에 끝나도록» 시작을 역산하므로 파편이 사는 시간은 디졸브 길이뿐이고(0.25초),
	//   화면에서는 «파편이 아예 없다» 로 보인다.
	// 떼어내기로는 안 고쳐진다. DetachFromComponent 는 변환 부모만 바꾸고 Outer 를 안 바꾼다 -
	//   실제로 EndPlay 에서 떼는 코드를 넣었었고 아무 일도 하지 않았다.
	// SpawnSystemAtLocation 은 Outer 를 World->GetWorldSettings() 로 잡으므로 적과 수명이 갈린다.
	//
	// 자세는 트랜스폼이 아니라 데이터 인터페이스가 정한다 (2026-09-19 에디터 실측): 표면 샘플링 위치를
	//   월드 공간으로 내므로 시스템을 어디에 두든 파티클은 «지금 그 자세의 표면» 에 난다.
	//   앞서 자세가 틀렸던 진짜 원인은 트랜스폼이 아니라 아래의 «꽂기 전에 켜기» 였다.
	//
	// 자동 활성화를 끄고 스폰한다: 이 시스템은 방출이 «즉시 버스트» 한 번이다. 자동 활성화면 스폰하는
	//   그 자리에서 첫 시뮬레이션이 도는데 메시를 꽂는 것은 그 다음 줄이라, 버스트가 터질 때 데이터
	//   인터페이스가 비어 있어 레퍼런스 자세를 본다. 그래서 순서를 뒤집는다 - 끈 채로 만들고 ->
	//   메시를 꽂고 -> 그때 켠다.
	UNiagaraComponent* Debris = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		TargetMesh,
		PendingConfig->DebrisEffect,
		TargetMesh->GetComponentLocation(),
		TargetMesh->GetComponentRotation(),
		TargetMesh->GetComponentScale(),
		/*bAutoDestroy*/ true,
		/*bAutoActivate*/ false);
	if (!Debris)
	{
		VB_LOG(Warning, "사망 디졸브: %s 파편 시스템 %s 스폰 실패.",
		       *GetNameSafe(GetOwner()), *GetNameSafe(PendingConfig->DebrisEffect));
		return;
	}

	// 지켜보기만 한다 - 시험 재생을 물릴 때 거두려는 것이고, 살려 두려는 것이 아니다.
	SpawnedDebris = Debris;

	// 파편이 「몸에서」 나오는 것으로 보이려면 파티클이 지금 이 자세의 표면에서 태어나야 한다.
	//   쓰러진 자세는 자산이 알 수 없으므로 런타임에 건넨다. 이름이 어긋나면 조용히 무동작이라
	//   꽂았다는 사실을 남긴다 - 파편이 안 보일 때 자산과 이름 중 무엇을 볼지 이 줄이 가른다.
	if (!PendingConfig->DebrisMeshParameterName.IsNone())
	{
		UNiagaraFunctionLibrary::OverrideSystemUserVariableSkeletalMeshComponent(
			Debris, PendingConfig->DebrisMeshParameterName.ToString(), TargetMesh);
		VB_LOG(Log, "사망 디졸브: %s 파편에 메시를 꽂았다 (파라미터 '%s').",
		       *GetNameSafe(GetOwner()), *PendingConfig->DebrisMeshParameterName.ToString());
	}

	// 파편 색을 죽는 메시에서 가져온다.
	//
	// 왜 (2026-09-20 구조 재설계): 파편 재질의 텍스처가 고정 기본값이면 어떤 몬스터가 죽어도 같은 색이
	//   난다 - 트롤이 마네킹 색으로 부서졌다(캡처로 확인). 몬스터마다 설정에 텍스처를 채우게 하면
	//   또 «등록을 빠뜨리면 조용한» 구조가 되므로, 죽는 그 메시에서 직접 읽는다.
	// 실패할 수 있는 자리가 둘이다. 둘 다 말하게 한다 - 한쪽만 말하면 나머지 절반이 조용히 넘어가고,
	//   화면에서는 「색이 이상하다」로만 보여 어디를 볼지 알 수 없다(2026-09-20 에 실제로 그랬다).
	if (!PendingConfig->DebrisMaterial)
	{
		VB_LOG(Warning, "사망 디졸브: %s 설정(%s)에 DebrisMaterial 이 없다 - 파편이 Niagara 기본 재질 색으로 난다.",
		       *GetNameSafe(GetOwner()), *GetNameSafe(PendingConfig));
	}
	else if (UTexture* BaseTexture = ResolveDebrisTexture(TargetMesh))
	{
		UMaterialInstanceDynamic* DustMID = UMaterialInstanceDynamic::Create(PendingConfig->DebrisMaterial, this);
		if (DustMID)
		{
			DustMID->SetTextureParameterValue(GParamDebrisBaseTexture, BaseTexture);
			Debris->SetVariableMaterial(PendingConfig->DebrisMaterialParameterName, DustMID);
			VB_LOG(Log, "사망 디졸브: %s 파편 색을 %s 에서 가져왔다.",
			       *GetNameSafe(GetOwner()), *BaseTexture->GetName());
		}
	}
	else
	{
		VB_LOG(Warning, "사망 디졸브: %s 메시에서 기본색 텍스처를 못 찾았다 - 파편이 재질 기본 색으로 난다.",
		       *GetNameSafe(GetOwner()));
	}

	// 메시가 들어간 뒤에 켠다. 이 줄이 위로 올라가면 다시 레퍼런스 자세에서 터진다.
	Debris->Activate();

	// 켠 그 자리에서 한 틱을 돌린다.
	//
	// 왜 (2026-09-20, user: 「메시가 깜빡거리는 느낌인데」): `Activate()` 는 파티클을 만들지 않는다.
	//   첫 시뮬레이션 틱이 만든다 - 실측으로 0틱에는 캡처조차 안 되고 1틱에 40,000개가 된다.
	//   그런데 아래 호출자가 같은 프레임에 메시를 숨기므로, 그 한 프레임은 «메시도 없고 파티클도 없는»
	//   구멍이 된다. 화면에서는 깜빡임으로 보인다.
	// 왜 미루지 않고 앞당기나: 숨기기를 한 틱 미루는 길도 있었지만 그건 타이머를 요구하고,
	//   타이머는 에디터 월드에서 안 돈다(시험 버튼이 몸을 안 숨겼다). 여기서 한 틱을 돌리면
	//   «없는 것을 있게» 만드는 쪽이라 에디터와 PIE 에서 똑같이 돈다.
	// 대가: 파편이 한 틱만큼 미리 늙는다. 수명이 2.5초 이상이라 눈에 안 띄는 양이다.
	// 델타가 0 이면 한 틱을 돌려도 아무 일이 안 난다 - 조용히 실패할 자리라 말하게 한다.
	const UWorld* DebrisWorld = GetWorld();
	const float   TickDelta   = DebrisWorld ? DebrisWorld->GetDeltaSeconds() : 0.0f;
	if (TickDelta > 0.0f)
	{
		Debris->AdvanceSimulation(1, TickDelta);
	}
	else
	{
		VB_LOG(Warning, "사망 디졸브: %s 월드 델타가 0 이라 파편 첫 틱을 못 돌렸다 - 한 프레임 깜빡일 수 있다.",
		       *GetNameSafe(GetOwner()));
	}
}
