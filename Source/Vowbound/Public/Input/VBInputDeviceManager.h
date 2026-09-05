// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CommonInputSubsystem.h"
#include "InputMappingContext.h"
#include "VBInputDeviceManager.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBInputDeviceManager : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBInputDeviceManager();
	
	ECommonInputType GetCurrentDeviceType() const;
	bool IsUsingGamepad() const;
	
	// 델리게이트 - 미래 UI 아이콘 스왑용
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInputDeviceChanged, ECommonInputType, NewDeviceType);
	UPROPERTY(BlueprintAssignable, Category="Vowbound|Input")
	FOnInputDeviceChanged OnInputDeviceChanged;
	
	

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputMappingContext> IMC_KeyboardMouse;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputMappingContext> IMC_Gamepad;
	
	int32 InputMappingPriority = 0;

private:
	ECommonInputType CurrentDeviceType = ECommonInputType::MouseAndKeyboard;
	TWeakObjectPtr<APlayerController> CachedPC;
	
	// BeginPlay에서 호출
	void InitializeInputDetection();
	void HandleInputMethodChanged(ECommonInputType NewInputType);
	void SwitchToKeyboardMouse();
	void SwitchToGamepad();
	void ApplyMappingContext(UInputMappingContext* NewIMC, UInputMappingContext* OldIMC);
};
