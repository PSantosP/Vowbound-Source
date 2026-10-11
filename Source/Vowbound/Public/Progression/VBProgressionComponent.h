// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "Progression/VBGrowthRules.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBProgressionComponent.generated.h"

class UAbilitySystemComponent;
class UVBGrowthConfig;
class UVBSkillTreeDefinition;
struct FVBSkillNode;

DECLARE_MULTICAST_DELEGATE(FVBOnProgressionChanged);

/**
 * 성장(경험치·레벨·스킬 트리)의 주인. AVBPlayerState 에 붙는다 - 인벤토리·장비·ASC 와 같은 자리라 폰이 죽고 다시 나도 남는다.
 * 설계 .sw/remaining-features/b2-growth.md, 결정 DEC-031(user 2026-10-06).
 *
 * 들고 있는 진실은 둘뿐이다 - 누적 경험치(TotalXP)와 연 칸 목록(Unlocked). 레벨·남은 포인트는 UVBGrowthConfig 표로 센다(VBGrowth).
 * 칸이 하는 일: 능력치 칸은 연 칸 전부의 합을 효과 하나(StatBonusEffect)로 건다, 능력 칸은 GiveAbility.
 * 쓰기는 서버만 한다. 화면은 요청(RequestUnlock)만 보낸다 - 판정은 서버가 VBGrowth::CanUnlock 으로 다시 한다.
 */
UCLASS(ClassGroup=(Vowbound))
class VOWBOUND_API UVBProgressionComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBProgressionComponent();

	// 폰·컨트롤러·PlayerState 어느 쪽을 줘도 그 플레이어의 성장. 없으면 nullptr.
	static UVBProgressionComponent* FindFor(const AActor* Actor);

	const UVBGrowthConfig* GetConfig() const { return Config.Get(); }
	const UVBSkillTreeDefinition* GetSkillTree() const;
	int32 GetTotalXP() const { return TotalXP; }
	int32 GetLevel() const;
	int32 GetMaxLevel() const;
	// 이 레벨이 시작된 뒤 모은 양 / 다음 레벨까지 필요한 양(최대 레벨이면 0/0).
	int32 GetXPIntoLevel() const;
	int32 GetXPForNextLevel() const;
	// 표시용 - 데이터가 바뀌어 음수가 되면 0(CanUnlock 은 음수 그대로 본다).
	int32 GetPointsAvailable() const;
	const TArray<FGameplayTag>& GetUnlocked() const { return Unlocked; }
	// 세이브를 읽어 바뀐 횟수. 이 값이 바뀐 갱신은 «오름» 이 아니라 «불러옴» 이다 - HUD 가 그 갱신에선 알림 없이 기준만 잡는다(VBGrowth::StepNoticeBaseline).
	int32 GetSaveReadSerial() const { return SaveReadSerial; }
	bool IsUnlocked(const FGameplayTag& NodeId) const { return Unlocked.Contains(NodeId); }
	EVBSkillUnlockResult CanUnlock(const FGameplayTag& NodeId) const;

	// 화면이 부른다. 결과는 OnProgressionChanged 로 돌아온다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Growth")
	void RequestUnlock(FGameplayTag NodeId);

	// 서버에서만. 경험치 효과(FVBEffect_GiveXP)·개발용 명령이 부른다. 레벨이 오르면 Event.Progression.LevelUp.
	void GiveXP(int32 Amount);
	// 서버에서만. 열었으면 Unlocked.
	EVBSkillUnlockResult Unlock(const FGameplayTag& NodeId);

	// 서버는 바꾼 직후, 클라는 복제가 도착했을 때.
	FVBOnProgressionChanged OnProgressionChanged;

	//~ IVBSaveParticipant - 성장 구역(UVBSaveGame::Growth). 문맥의 PlayerState 가 주인일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 레벨 표·포인트·트리·능력치 효과. 비면 경험치는 쌓이되 레벨 1 에 머물고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Growth")
	TObjectPtr<const UVBGrowthConfig> Config;

private:
	UPROPERTY(ReplicatedUsing=OnRep_Progression)
	int32 TotalXP = 0;

	UPROPERTY(ReplicatedUsing=OnRep_Progression)
	TArray<FGameplayTag> Unlocked;

	// 위 둘과 같은 갱신에 실려 온다 - 원격 클라의 HUD 도 «불러옴» 을 같은 방식으로 가린다.
	UPROPERTY(ReplicatedUsing=OnRep_Progression)
	int32 SaveReadSerial = 0;

	UFUNCTION()
	void OnRep_Progression();

	// WithValidation 은 안 붙였다 - 칸 이름 하나라 깨질 값이 없고, 열 수 있는지는 Unlock 이 서버에서 다시 본다. 멀티 권한 정리는 FIND-128 과 같이 본다.
	UFUNCTION(Server, Reliable)
	void ServerUnlock(FGameplayTag NodeId);

	// 서버 전용 - 연 칸이 건 것들.
	FActiveGameplayEffectHandle StatEffectHandle;
	TMap<FGameplayTag, FGameplayAbilitySpecHandle> GrantedAbilities;

	// 연 칸 목록에 맞춰 능력치 효과를 다시 건다(떼고 합을 다시 건다).
	void ReapplyStatEffect();
	// 능력 칸 하나의 능력을 준다. 시작 능력과 겹치면 주지 않고 경고한다(주인이 둘).
	void GrantAbility(const FVBSkillNode& Node);
	void RevokeAllAbilities();
	void BroadcastEvent(const FGameplayTag& EventTag, const FGameplayTag& Target, int32 Count) const;
	bool HasWriteAuthority(const TCHAR* What) const;
	UAbilitySystemComponent* GetAbilitySystem() const;
};
