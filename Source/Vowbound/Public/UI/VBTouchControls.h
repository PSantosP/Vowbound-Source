// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputActionValue.h"
#include "Widgets/Layout/Anchors.h"   // FAnchors - 배치 구조체가 값으로 든다
#include "VBTouchControls.generated.h"

class UInputAction;
class UEnhancedInputLocalPlayerSubsystem;
class UVBTouchInputWidget;

// 터치 버튼의 액션이 «화면 열기» 면 주입 대신 이 처리기가 받는다(참 = 처리함). HUD 가 단다(.sw/touch-controls 2단계).
DECLARE_DELEGATE_RetVal_OneParam(bool, FVBTouchUIActionHandler, const UInputAction*);

// 화면 조작 하나의 배치. UVBTouchControlsWidget 이 목록으로 들고 런타임에 만든다.
USTRUCT(BlueprintType)
struct FVBTouchControlSlot
{
	GENERATED_BODY()

	// 스틱 / 시점 패드 / 버튼 중 무엇인가 - 그 파생의 WBP(그림과 반경·배율 같은 기본값을 든다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	TSubclassOf<UVBTouchInputWidget> WidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	TObjectPtr<UInputAction> InputAction;

	// 버튼 글자. 버튼이 아니면 무시한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	FText Label;

	// 캔버스 슬롯 값. 앵커가 한 점이면 Position = 그 점에서의 오프셋, Size = 크기(엔진 캔버스 규칙 그대로).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	FAnchors Anchors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	FVector2D Alignment = FVector2D(0.5f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	FVector2D Size = FVector2D(120.0f, 120.0f);

	// 겹칠 때 위에 오는 순서. 시점 패드는 화면 절반을 덮으므로 버튼보다 낮아야 버튼이 눌린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	int32 ZOrder = 1;
};

/**
 * 화면 터치 조작의 공통 부모. 손가락 하나를 붙잡고, 연결된 입력 액션에 값을 주입한다.
 * 설계 .sw/touch-controls/design.md
 *
 * 왜 주입인가: Enhanced Input 의 주입은 IMC 매핑과 무관하게 처리된다. 그래서 터치가 캐릭터의 기존 바인딩
 *   (HandleMove·HandleLook·능력 입력)을 그대로 탄다 - 새 입력 경로가 생기지 않는다.
 * 주의: 주입은 IMC 의 수식어(부정·스위즐·감도)를 거치지 않는다. 값은 «수식어를 거친 뒤» 의 모양으로 넣는다.
 * 손가락: 자기 위젯에서 시작한 손가락 하나만 따른다(포인터 인덱스). 그래서 스틱과 버튼을 동시에 누를 수 있다.
 * 정리: 위젯이 사라지거나 캡처를 잃으면 주입을 멈춘다 - 안 그러면 버튼이 눌린 채로 남는다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBTouchInputWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// UVBTouchControlsWidget 이 배치 목록대로 만들면서 넣는다.
	void SetInputAction(UInputAction* InAction) { InputAction = InAction; }

	// 손가락을 뗀 것으로 친다(주입을 멈춘다). 메뉴가 떠서 조작을 감출 때 부른다 -
	//   감추기만 하면 손 뗌 이벤트가 안 와서 주입이 계속된다(.sw/ui-foundation 2.2 터치).
	void ReleaseTouch() { EndActiveTouch(); }

protected:
	// 이 조작이 누르는 입력 액션. 배치 목록(FVBTouchControlSlot)이 정한다 - 버튼 목록이 코드에 없다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	TObjectPtr<UInputAction> InputAction;

	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual void NativeDestruct() override;

	// 파생이 채운다. 좌표는 이 위젯의 로컬 좌표다.
	virtual void OnTouchBegin(const FVector2D& LocalPosition) {}
	virtual void OnTouchMove(const FVector2D& LocalPosition, const FVector2D& ScreenDelta) {}
	virtual void OnTouchEnd() {}

	// 누르는 동안 매 틱 같은 값을 넣는다. 멈추면 다음 틱에 «뗀 것» 으로 처리된다(Completed).
	void StartInjection(const FInputActionValue& Value);
	void UpdateInjection(const FInputActionValue& Value);
	void StopInjection();

	// 이번 틱에 한 번만 넣는다(시점처럼 «이동량» 인 입력).
	void InjectOnce(const FInputActionValue& Value);

private:
	UEnhancedInputLocalPlayerSubsystem* GetInputSubsystem() const;

	// 지금 붙잡은 손가락. 없으면 INDEX_NONE.
	int32 ActivePointerIndex = INDEX_NONE;
	bool  bInjecting = false;

	void EndActiveTouch();
};

/**
 * 가상 스틱. 누른 자리가 아니라 위젯 중심이 원점이다. 오프셋/반경을 [-1,1] 로 만들어 계속 주입한다(이동).
 * 화면 위쪽 = +Y(앞). 게임패드 왼쪽 스틱과 같은 뜻이다(2026-09-24 실측: IMC_Gamepad 의 Gamepad_Left2D -> IA_Move 는
 *   데드존만 있고 축 뒤집기가 없다. 그래서 값을 그대로 넣으면 같은 뜻이 된다).
 */
UCLASS()
class VOWBOUND_API UVBTouchStick : public UVBTouchInputWidget
{
	GENERATED_BODY()

protected:
	// 끝까지 민 것으로 치는 거리(위젯 로컬 단위). 그림 크기와 같이 정하는 값이라 WBP 에 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch", meta=(ClampMin="1.0"))
	float Radius = 100.0f;

	// 손가락을 따라 움직이는 손잡이 그림. 없어도 입력은 된다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UWidget> Thumb;

	virtual void OnTouchBegin(const FVector2D& LocalPosition) override;
	virtual void OnTouchMove(const FVector2D& LocalPosition, const FVector2D& ScreenDelta) override;
	virtual void OnTouchEnd() override;

private:
	FVector2D ComputeValue(const FVector2D& LocalPosition) const;
	void      PlaceThumb(const FVector2D& Value);
};

/**
 * 시점 패드. 손가락 이동량(화면 px) x 배율을 그 틱에 한 번 넣는다 - 마우스와 같은 «이동량» 입력이다.
 */
UCLASS()
class VOWBOUND_API UVBTouchLookPad : public UVBTouchInputWidget
{
	GENERATED_BODY()

protected:
	// 축별 배율(부호 포함). 1 = 마우스 1픽셀 이동과 같은 양.
	// 부호(2026-09-24 실측): IMC_KeyboardMouse 의 Mouse2D -> IA_Look 은 Y 만 부정(Negate Y)한다. 마우스를 위로 밀면
	//   원시 +Y -> 부정 -> -Y 가 캐릭터에 간다. 손가락을 위로 밀면 화면 좌표 Y 가 줄어 이미 -Y 이므로 배율은 둘 다 양수다.
	//   주입은 IMC 수식어를 안 거치므로 그 매핑이 바뀌면 여기도 같이 봐야 한다.
	// 크기는 손맛 값이라 WBP 에서 조정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	FVector2D LookScale = FVector2D(1.0f, 1.0f);

	virtual void OnTouchMove(const FVector2D& LocalPosition, const FVector2D& ScreenDelta) override;
};

/**
 * 버튼. 누르는 동안 true 를 계속 넣고 떼면 멈춘다. 키보드 키와 같게 Started/Completed 가 나서 홀드 능력(가드·질주·상호작용 홀드)도 된다.
 */
UCLASS()
class VOWBOUND_API UVBTouchButton : public UVBTouchInputWidget
{
	GENERATED_BODY()

public:
	void SetLabel(const FText& InLabel);
	void SetUIActionHandler(const FVBTouchUIActionHandler& InHandler) { UIActionHandler = InHandler; }

protected:
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> Label;

	// 누르는 동안의 불투명도 - 눌렸다는 표시. 뗄 때는 누르기 직전 값으로 돌린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PressedOpacity = 0.6f;

	virtual void OnTouchBegin(const FVector2D& LocalPosition) override;
	virtual void OnTouchEnd() override;

private:
	float ReleasedOpacity = 1.0f;

	// 화면 열기 액션의 처리기. 비어 있으면 늘 주입한다.
	FVBTouchUIActionHandler UIActionHandler;
};

/**
 * 화면 조작 묶음. Controls 목록대로 스틱·패드·버튼을 만들어 자기 루트 캔버스에 붙인다.
 * 왜 런타임에 만드나: 위젯 트리 저작 도구가 사용자 위젯 자식을 못 넣는다(HUD 의 화면 밖 화살표와 같은 사정).
 *   배치는 이 목록(데이터)이고, 버튼을 더하거나 옮기는 것은 WBP 기본값 편집이다.
 * 루트 위젯은 CanvasPanel 이어야 한다. 아니면 경고하고 아무것도 안 만든다.
 */
UCLASS()
class VOWBOUND_API UVBTouchControlsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 붙잡힌 손가락을 전부 놓는다. HUD 가 메뉴 입력 모드로 바뀔 때 부른다.
	void ReleaseAllTouches();

	// 만든 버튼 전부에 화면 액션 처리기를 넘긴다. 조작은 NativeOnInitialized(=CreateWidget 안)에서 이미 만들어져 있다.
	void SetUIActionHandler(const FVBTouchUIActionHandler& InHandler);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Touch")
	TArray<FVBTouchControlSlot> Controls;

	virtual void NativeOnInitialized() override;

private:
	// 만든 조작들. ReleaseAllTouches 가 돈다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBTouchInputWidget>> CreatedControls;
};
