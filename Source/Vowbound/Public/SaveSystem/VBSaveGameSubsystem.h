// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBSaveGameSubsystem.generated.h"

class UVBSaveGame;
class APawn;

// 게임 슬롯 저장/로드 완료 통지. (슬롯 인덱스, 성공 여부)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVBOnGameSaveCompleted, int32, SlotIndex, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVBOnGameLoadCompleted, int32, SlotIndex, bool, bSuccess);

/**
 * 세이브/로드 중앙 API.
 * 이유: 저장 로직을 GameInstance 수명에 묶어 레벨 전환에도 활성 세이브가 유지되게 한다.
 *       GameInstanceSubsystem 이라 별도 UGameInstance 파생 없이 동작하고
 *       BP/C++ 어디서나 GetGameInstance()->GetSubsystem<UVBSaveGameSubsystem>() 로 접근 가능.
 *
 * 슬롯 구성:
 *   - Settings : "VBSettings" 단일 글로벌 슬롯. 동기 처리(데이터가 작고 메뉴 진입 시 즉시 필요).
 *   - Game     : "VBGameSave_<N>" 멀티 슬롯. 비동기 처리(프레임 히치 방지, 60fps 목표).
 */
UCLASS()
class VOWBOUND_API UVBSaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 어디서나 세이브 서브시스템에 접근하는 정적 헬퍼. WorldContext 로 GameInstance 를 찾는다.
	UFUNCTION(BlueprintPure, Category="Vowbound|Save", meta=(WorldContext="WorldContextObject"))
	static UVBSaveGameSubsystem* Get(const UObject* WorldContextObject);

	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---------------------------------------------------------------- Settings (글로벌, 동기)

	// 설정 세이브를 캐시에서 반환하거나, 디스크에서 로드하거나, 없으면 새로 만든다. 항상 유효 포인터.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	UVBSaveGame* LoadOrCreateSettings();

	// 설정 값을 읽는 유일한 경로. 서브시스템/세이브가 없어도 기본값 참조를 돌려준다(null 없음).
	// 왜 값 복사가 아니라 참조인가: 소비처가 입력 이벤트마다 홈을 직접 읽으므로, UpdateSettings 가
	//  홈을 갱신한 순간부터 다음 입력이 새 값을 본다. 값 캐시도, 변경 브로드캐스트도 필요 없다.
	// GetTargetLockConfig()/GetParryConfig() 의 "폴백 있음, null 없음" 규약과 같은 형태다.
	static const FVBSettingsSaveData& GetSettingsData(const UObject* WorldContextObject);

	// 설정 홈을 갱신하고 영속화한다. 홈 갱신이 곧 적용이다(소비처가 매번 홈을 읽는다).
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void UpdateSettings(const FVBSettingsSaveData& NewSettings);

	// 현재 설정을 "VBSettings" 슬롯에 저장(비동기). 메뉴에서 옵션 변경 후 호출.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void SaveSettings();

	// ---------------------------------------------------------------- Game Slot (비동기)

	// 해당 슬롯에 저장 파일이 존재하는지. 메뉴 "Continue" 활성화 판단용.
	UFUNCTION(BlueprintPure, Category="Vowbound|Save")
	bool DoesGameSaveExist(int32 SlotIndex) const;

	// 활성 세이브를 해당 슬롯에 비동기 저장. 완료 시 OnGameSaveCompleted 브로드캐스트.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void SaveGameSlot(int32 SlotIndex);

	// 해당 슬롯을 비동기 로드하여 활성 세이브로 설정. 완료 시 OnGameLoadCompleted 브로드캐스트.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void LoadGameSlot(int32 SlotIndex);

	// 해당 슬롯 저장 파일 삭제.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void DeleteGameSave(int32 SlotIndex);

	// 새 게임 시작(슬롯 미지정). 빈 활성 세이브를 만들 뿐 **디스크에 쓰지 않는다**.
	// 왜: New Game 은 "지금부터 플레이"이지 "슬롯 점유"가 아니다(2026-08-12 확정).
	//   시작 시점에 슬롯을 잡으면 슬롯 수만큼만 새 게임을 시작할 수 있게 되고(3개면 3번),
	//   플레이할 생각도 없이 눌러본 것만으로 남의 세이브 자리를 먹는다.
	//   슬롯은 **플레이어가 실제로 저장할 때** 정해진다(SaveCurrentGame 의 슬롯 해석).
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void StartNewGame();

	// 슬롯을 명시해 새 게임 시작 + 즉시 점유. 슬롯 목록에서 사용자가 직접 고른 경우 전용
	// (덮어쓰기 포함). 사용자가 고른 것이므로 즉시 점유가 옳다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void StartNewGameInSlot(int32 SlotIndex);

	// 활성 게임 세이브를 반환(없으면 생성). 게임플레이 중 진행도/빌드 갱신 대상.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	UVBSaveGame* GetOrCreateActiveGameSave();

	// 활성 게임 세이브(없으면 nullptr). 읽기 전용 조회.
	UFUNCTION(BlueprintPure, Category="Vowbound|Save")
	UVBSaveGame* GetActiveGameSave() const { return ActiveGameSave; }

	// ---------------------------------------------------------------- 복원 핸드오프 (레벨 전환 넘어)

	// 게임플레이 레벨로 이동한다. 목적지는 UVBGameFlowSettings::GameplayLevel 하나가 정하고,
	// 미지정 시의 경고도 여기 한 곳에만 있다.
	// 왜 위젯이 아니라 여기인가: 호출부 셋(새 게임 / 슬롯 지정 새 게임 / 로드 완료)이 전부
	//   "세이브를 새로 시작했거나 불러왔으니 그 세이브가 사는 레벨로 간다" 라는 세이브 흐름의 종착이다.
	//   종전에는 위젯 둘이 각자 TSoftObjectPtr 필드를 들고 각자 OpenLevel 을 불렀고, 한쪽만 바꾸면
	//   새 게임과 불러오기가 다른 맵으로 갔다(FIND-087). 두 위젯에 공통 베이스가 없어
	//   (UVBSaveSlotListWidget 은 UUserWidget 직속) 함께 닿는 지점은 이 서브시스템뿐이다.
	// 확장: "세이브가 자기 레벨을 기억한다"로 갈 때 고칠 곳이 이 함수 하나가 된다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void TravelToGameplayLevel();

	// 마지막으로 로드(Continue)하거나 새로 만든(New Game) 슬롯 인덱스. SaveCurrentSlot 이 되쓸 대상.
	UFUNCTION(BlueprintPure, Category="Vowbound|Save")
	int32 GetActiveSlotIndex() const { return ActiveSlotIndex; }

	// "다음 레벨 진입 시 활성 세이브를 플레이어에 적용해야 하는가"(=Continue) 를 1회성으로 소비한다.
	// true 반환 시 내부 플래그를 즉시 내려 중복 적용을 막는다. New Game 은 이 플래그를 세우지 않는다.
	// 왜 1회성: OpenLevel 이 월드를 파괴하므로 GI 수명 서브시스템이 플래그를 들고 있다가 새 레벨 GameMode 가 소비한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	bool ConsumePendingContinueApply();

	// 활성 슬롯에 현재 활성 세이브를 저장(비동기). SaveCurrentGame 경유로 스냅샷까지 수행(로컬 폰 해석).
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void SaveCurrentSlot();

	// 슬롯을 지정하지 않는 저장. 슬롯은 여기서 해석한다(활성 슬롯 -> 빈 슬롯 -> 0).
	// 사용자가 고를 화면이 없는 경로(오토세이브 등) 전용이다 - 조용한 no-op 을 만들지 않기 위한 폴백이지
	// 정상 경로가 아니다. 사용자가 슬롯을 고르는 저장은 SaveCurrentGameToSlot 이 받는다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void SaveCurrentGame(APawn* Pawn);

	// 지정한 슬롯에 현재 플레이어 상태(위치/빌드) 스냅샷을 조립해 저장(비동기). 단일 권위 저장경로.
	// 저장 후 그 슬롯이 활성 슬롯이 된다 - 이어지는 저장/Continue 가 같은 자리를 가리켜야 한다.
	// 반환값 = 비동기 저장을 실제로 걸었는지. false 면 OnGameSaveCompleted 가 오지 않으므로,
	//   완료를 기다리려는 호출자는 이 값을 보고 구독해야 한다(영영 안 오는 구독 방지).
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	bool SaveCurrentGameToSlot(APawn* Pawn, int32 SlotIndex);

	// 슬롯 인덱스 → 슬롯 문자열 이름 ("VBGameSave_<N>").
	UFUNCTION(BlueprintPure, Category="Vowbound|Save")
	static FString MakeGameSlotName(int32 SlotIndex);

	// ---------------------------------------------------------------- Slot Metadata (멀티 슬롯 UI)

	// 메뉴에 노출할 게임 슬롯 수.
	UFUNCTION(BlueprintPure, Category="Vowbound|Save")
	int32 GetMaxSlotCount() const { return MaxSlotCount; }

	// 단일 슬롯 메타 조회. 슬롯을 동기 로드해 헤더 정보를 추출(목록 표시용).
	// 반환값 = 슬롯 존재 여부(빈 슬롯이면 false, 그래도 OutMeta.SlotIndex 는 채워짐).
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	bool GetSlotMetadata(int32 SlotIndex, FVBSaveSlotMeta& OutMeta) const;

	// 0..MaxSlotCount-1 전체 슬롯 메타 배열. 빈 슬롯도 bExists=false 로 포함.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	TArray<FVBSaveSlotMeta> GetAllSlotMetadata() const;

	// 가장 최근(Timestamp 최대) 저장 슬롯 인덱스. 하나도 없으면 INDEX_NONE.
	// 왜 위젯이 아닌 여기가 주인인가: 메인메뉴 "Continue" 와 게임오버 "계속하기"는 반드시 같은 세이브를
	//   가리켜야 한다. 정책("존재하는 슬롯 중 Timestamp 최대")이 두 벌로 복사되면 한쪽만 고쳐졌을 때 조용히
	//   다른 세이브를 연다(DRY 가 아니라 의미론적 단일화). 소유자는 GetAllSlotMetadata 를 이미 가진 이 서브시스템.
	// 전제: GetAllSlotMetadata 가 빈 슬롯도 bExists=false 로 포함 → bExists 검사 필수.
	// 부작용: GetSlotMetadata 경유로 슬롯을 동기 로드한다(메뉴/사망 시점 호출이라 빈도 낮음).
	UFUNCTION(BlueprintPure, Category="Vowbound|Save")
	int32 GetMostRecentSlotIndex() const;

	// 저장된 게임이 하나라도 있는지 = GetMostRecentSlotIndex() >= 0. Continue 버튼 활성화 판단용.
	UFUNCTION(BlueprintPure, Category="Vowbound|Save")
	bool HasAnySaveGame() const;

	// ---------------------------------------------------------------- Delegates

	UPROPERTY(BlueprintAssignable, Category="Vowbound|Save")
	FVBOnGameSaveCompleted OnGameSaveCompleted;

	UPROPERTY(BlueprintAssignable, Category="Vowbound|Save")
	FVBOnGameLoadCompleted OnGameLoadCompleted;

private:
	// 메뉴에 노출할 게임 슬롯 수. 확장 시 UDeveloperSettings 로 외부화.
	static constexpr int32 MaxSlotCount = 3;

	// 로드된 세이브의 SaveVersion 이 낮으면 현재 포맷으로 마이그레이션. 확장 지점.
	UVBSaveGame* MigrateIfNeeded(UVBSaveGame* InSave) const;

	// 슬롯 미지정 저장이 쓸 슬롯을 정한다. 활성 슬롯 -> 첫 빈 슬롯 -> 0(경고).
	// 무조건 0 으로 가면 첫 저장이 남의 세이브를 소리 없이 덮는다.
	int32 ResolveSaveSlot() const;

	// 글로벌 설정 캐시. LoadOrCreateSettings 가 보증.
	UPROPERTY()
	TObjectPtr<UVBSaveGame> SettingsSave;

	// 현재 플레이 중 게임 세이브 캐시.
	UPROPERTY()
	TObjectPtr<UVBSaveGame> ActiveGameSave;

	// 되쓸/적용할 슬롯. LoadGameSlot(Continue) 성공 콜백과 StartNewGame 이 세팅.
	int32 ActiveSlotIndex = INDEX_NONE;

	// Continue 로드가 끝나 "다음 레벨에서 적용 대기" 상태인지. New Game 은 false 유지.
	bool bPendingContinueApply = false;
};
