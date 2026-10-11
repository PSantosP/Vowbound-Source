// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Progression/VBProgressionComponent.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBStatBonus.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Player/VBPlayerStateLookup.h"
#include "Progression/VBGrowthConfig.h"
#include "Progression/VBSkillTreeDefinition.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Vowbound/Vowbound.h"

UVBProgressionComponent::UVBProgressionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

UVBProgressionComponent* UVBProgressionComponent::FindFor(const AActor* Actor)
{
	return VBPlayerStateLookup::FindComponent<UVBProgressionComponent>(Actor);
}

const UVBSkillTreeDefinition* UVBProgressionComponent::GetSkillTree() const
{
	return Config ? Config->SkillTree.Get() : nullptr;
}

int32 UVBProgressionComponent::GetLevel() const
{
	return Config ? VBGrowth::LevelForXP(Config->XPToNextLevel, TotalXP) : 1;
}

int32 UVBProgressionComponent::GetMaxLevel() const
{
	return Config ? Config->GetMaxLevel() : 1;
}

int32 UVBProgressionComponent::GetXPIntoLevel() const
{
	const int32 Level = GetLevel();
	if (!Config || Level >= GetMaxLevel())
	{
		return 0;
	}
	return TotalXP - VBGrowth::XPAtLevelStart(Config->XPToNextLevel, Level);
}

int32 UVBProgressionComponent::GetXPForNextLevel() const
{
	const int32 Level = GetLevel();
	if (!Config || Level >= GetMaxLevel())
	{
		return 0;
	}
	return FMath::Max(1, Config->XPToNextLevel[Level - 1]);
}

int32 UVBProgressionComponent::GetPointsAvailable() const
{
	const UVBSkillTreeDefinition* Tree = GetSkillTree();
	if (!Tree)
	{
		return 0;
	}
	return FMath::Max(0, VBGrowth::PointsEarned(GetLevel(), Config->PointsPerLevel) - VBGrowth::PointsSpent(*Tree, Unlocked));
}

EVBSkillUnlockResult UVBProgressionComponent::CanUnlock(const FGameplayTag& NodeId) const
{
	const UVBSkillTreeDefinition* Tree = GetSkillTree();
	if (!Tree)
	{
		return EVBSkillUnlockResult::Unavailable;
	}
	// 남은 포인트는 음수 그대로 넘긴다 - 데이터가 바뀌어 빚이 생겼으면 그 빚을 갚기 전에는 못 연다.
	const int32 Points = VBGrowth::PointsEarned(GetLevel(), Config->PointsPerLevel) - VBGrowth::PointsSpent(*Tree, Unlocked);
	return VBGrowth::CanUnlock(*Tree, Unlocked, GetLevel(), Points, NodeId);
}

void UVBProgressionComponent::RequestUnlock(FGameplayTag NodeId)
{
	ServerUnlock(NodeId);
}

void UVBProgressionComponent::ServerUnlock_Implementation(FGameplayTag NodeId)
{
	Unlock(NodeId);
}

void UVBProgressionComponent::GiveXP(int32 Amount)
{
	if (!HasWriteAuthority(TEXT("GiveXP")) || Amount <= 0)
	{
		return;
	}
	const int32 Before = GetLevel();
	TotalXP = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(TotalXP) + Amount, MAX_int32));
	const int32 After = GetLevel();
	VB_LOG(Log, "성장: 경험치 +%d -> 누적 %d, 레벨 %d%s", Amount, TotalXP, After, Config ? TEXT("") : TEXT(" (성장 설정 없음 - 레벨 1 고정)"));
	if (After > Before)
	{
		// 사건의 수 = 오른 레벨 수(한 번에 둘 오를 수 있다). 대상 태그는 없다.
		BroadcastEvent(VBGameplayTags::Event_Progression_LevelUp, FGameplayTag(), After - Before);
	}
	OnProgressionChanged.Broadcast();
}

EVBSkillUnlockResult UVBProgressionComponent::Unlock(const FGameplayTag& NodeId)
{
	if (!HasWriteAuthority(TEXT("Unlock")))
	{
		return EVBSkillUnlockResult::Unavailable;
	}
	const EVBSkillUnlockResult Result = CanUnlock(NodeId);
	VB_LOG(Log, "성장: %s 열기 - %s (레벨 %d, 남은 포인트 %d)", *NodeId.ToString(), VBGrowth::LexToString(Result), GetLevel(), GetPointsAvailable());
	if (Result != EVBSkillUnlockResult::Unlocked)
	{
		return Result;
	}
	const FVBSkillNode* Node = GetSkillTree()->FindNode(NodeId);
	Unlocked.Add(NodeId);
	if (Node->GrantedAbility)
	{
		GrantAbility(*Node);
	}
	if (!Node->Stats.IsEmpty())
	{
		ReapplyStatEffect();
	}
	BroadcastEvent(VBGameplayTags::Event_Progression_SkillUnlocked, NodeId, 1);
	OnProgressionChanged.Broadcast();
	return Result;
}

void UVBProgressionComponent::ReapplyStatEffect()
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	if (ASC && StatEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(StatEffectHandle);
	}
	StatEffectHandle.Invalidate();
	const UVBSkillTreeDefinition* Tree = GetSkillTree();
	if (!Tree)
	{
		return;
	}
	TMap<FGameplayTag, float> Sum;
	for (const FGameplayTag& NodeId : Unlocked)
	{
		if (const FVBSkillNode* Node = Tree->FindNode(NodeId))
		{
			for (const TPair<FGameplayTag, float>& Stat : Node->Stats)
			{
				Sum.FindOrAdd(Stat.Key) += Stat.Value;
			}
		}
	}
	if (Sum.IsEmpty())
	{
		return;
	}
	StatEffectHandle = VBStatBonus::Apply(ASC, Config->StatBonusEffect, Sum, TEXT("스킬 트리"));
	// 칸은 이미 열렸다(포인트를 썼다) - 되돌리지 않고 소리를 낸다. 원인은 설정(효과 없음·순간 효과)이라 IsDataValid 가 먼저 잡는다.
	VB_CLOG(!StatEffectHandle.IsValid(), Error, "성장: 능력치 효과(%s)를 걸지 못했다 - 연 능력치 칸 %d 개가 몸에 안 들어갔다. StatBonusEffect 를 확인하라",
		*GetNameSafe(Config->StatBonusEffect.Get()), Sum.Num());
}

void UVBProgressionComponent::GrantAbility(const FVBSkillNode& Node)
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	if (!ASC || !Node.GrantedAbility || GrantedAbilities.Contains(Node.NodeId))
	{
		return;
	}
	// 시작 능력(PlayerState DefaultAbilities)에도 있으면 이미 가진 능력을 또 주게 된다 - 주인이 둘. 주지 않고 데이터를 고치라고 알린다.
	//   이어하기는 PossessedBy(시작 능력 부여) 전에 읽히므로 그 길에서는 이 검사가 겹침을 못 본다 - 데이터에서 빼는 것이 정답이다.
	if (ASC->FindAbilitySpecFromClass(Node.GrantedAbility))
	{
		VB_LOG(Warning, "성장: %s 의 능력 %s 를 이미 가졌다 - 시작 능력과 겹친다(BP_VBPlayerState DefaultAbilities 에서 빼라). 주지 않는다",
			*Node.NodeId.ToString(), *GetNameSafe(Node.GrantedAbility.Get()));
		return;
	}
	GrantedAbilities.Add(Node.NodeId, ASC->GiveAbility(FGameplayAbilitySpec(Node.GrantedAbility, 1, INDEX_NONE, GetOwner())));
}

void UVBProgressionComponent::RevokeAllAbilities()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		for (const TPair<FGameplayTag, FGameplayAbilitySpecHandle>& Granted : GrantedAbilities)
		{
			ASC->ClearAbility(Granted.Value);
		}
	}
	GrantedAbilities.Reset();
}

void UVBProgressionComponent::BroadcastEvent(const FGameplayTag& EventTag, const FGameplayTag& Target, int32 Count) const
{
	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
	{
		FVBGameEvent Event;
		Event.EventTag = EventTag;
		if (Target.IsValid())
		{
			Event.TargetTags.AddTag(Target);
		}
		Event.Instigator = Cast<APlayerState>(GetOwner());
		Event.Count = Count;
		Events->Broadcast(Event);
	}
}

void UVBProgressionComponent::OnRep_Progression()
{
	OnProgressionChanged.Broadcast();
}

void UVBProgressionComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner())
	{
		return;
	}
	Save.Growth.TotalXP = TotalXP;
	Save.Growth.Unlocked = Unlocked;
}

void UVBProgressionComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner() || !HasWriteAuthority(TEXT("ReadSave")))
	{
		return;
	}
	TotalXP = FMath::Max(0, Save.Growth.TotalXP);
	Unlocked.Reset();
	const UVBSkillTreeDefinition* Tree = GetSkillTree();
	for (const FGameplayTag& NodeId : Save.Growth.Unlocked)
	{
		// 트리에서 지워진 칸만 버린다. 레벨·선행·포인트가 안 맞아도(데이터가 바뀜) 연 것을 몰래 닫지 않는다 - 아래에서 소리만 낸다.
		if (!Tree || !Tree->FindNode(NodeId))
		{
			VB_LOG(Warning, "성장 로드: %s 는 트리에 없다 - 버린다(칸을 지웠거나 이름을 바꿨나)", *NodeId.ToString());
			continue;
		}
		Unlocked.AddUnique(NodeId);
	}
	if (Tree)
	{
		const int32 Debt = VBGrowth::PointsSpent(*Tree, Unlocked) - VBGrowth::PointsEarned(GetLevel(), Config->PointsPerLevel);
		VB_CLOG(Debt > 0, Warning, "성장 로드: 연 칸이 레벨 %d 로 받는 포인트보다 %d 많다 - 칸은 그대로 두고 새로 열기만 막힌다(비용·레벨 표가 바뀌었나)",
			GetLevel(), Debt);
	}
	RevokeAllAbilities();
	for (const FGameplayTag& NodeId : Unlocked)
	{
		const FVBSkillNode* Node = Tree->FindNode(NodeId);
		if (Node->GrantedAbility)
		{
			GrantAbility(*Node);
		}
	}
	ReapplyStatEffect();
	VB_LOG(Log, "성장 로드: 누적 %d, 레벨 %d, 연 칸 %d", TotalXP, GetLevel(), Unlocked.Num());
	// 이 갱신은 «불러옴» 이다 - 받는 쪽(HUD 알림)이 오름·해금으로 읽지 않게 표시를 바꾼다(f3 verify 10-08 Q3: 이어하기 직후 알림이 몰려 떴다).
	++SaveReadSerial;
	OnProgressionChanged.Broadcast();
}

void UVBProgressionComponent::InitializeComponent()
{
	Super::InitializeComponent();
	VB_CLOG(!Config, Warning, "성장: %s 에 Config(UVBGrowthConfig)가 비었다 - 경험치는 쌓이되 레벨 1 에 머문다", *GetNameSafe(GetOwner()));
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBProgressionComponent::UninitializeComponent()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBProgressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UVBProgressionComponent, TotalXP, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UVBProgressionComponent, Unlocked, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UVBProgressionComponent, SaveReadSerial, COND_OwnerOnly);
}

bool UVBProgressionComponent::HasWriteAuthority(const TCHAR* What) const
{
	// 장비·인벤토리와 같은 뜻(주인 액터의 권한). 서버 쓰기 판정을 하나로 합치는 일은 구조 정리 2단계 후보다(.sw/structure-refactor/design.md).
	const AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		VB_LOG(Warning, "성장 %s 을 권한 없는 쪽에서 불렀다 - 무시한다(쓰기는 서버만)", What);
		return false;
	}
	return true;
}

UAbilitySystemComponent* UVBProgressionComponent::GetAbilitySystem() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

#if !UE_BUILD_SHIPPING
namespace
{
	// 개발용 콘솔 명령: 서버 월드의 모든 플레이어에게 경험치를 준다. 출시 빌드에는 없다.
	//   왜: 반격을 트리 끝(레벨 6)으로 옮겨(DEC-031) PIE 에서 열어 보려면 적을 수십 마리 잡아야 한다. 실제 길(GiveXP)을 탄다 - 레벨업 사건도 그대로다.
	FAutoConsoleCommandWithWorldAndArgs GVBGrowthGiveXPCommand(
		TEXT("vb.Growth.GiveXP"),
		TEXT("Dev only. vb.Growth.GiveXP <amount> - give experience to every player on the server world."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
			const int32 Amount = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			if (Amount <= 0 || !GameState || World->GetNetMode() == NM_Client)
			{
				VB_LOG(Warning, "vb.Growth.GiveXP: %s", Amount <= 0 ? TEXT("양(1 이상)이 필요하다") : TEXT("서버 게임 월드에서만 된다(PIE 중에 친다)"));
				return;
			}
			for (APlayerState* PlayerState : GameState->PlayerArray)
			{
				if (UVBProgressionComponent* Progression = UVBProgressionComponent::FindFor(PlayerState))
				{
					Progression->GiveXP(Amount);
				}
			}
		}));
}
#endif
