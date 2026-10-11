#include "Animation/VBLookAtPOIAnimInstance.h"
#include "Animation/VBLookAtPOIComponent.h"
#include "Animation/VBPlayerAttentionComponent.h"
#include "Engine/DataAsset.h"
#include "UObject/UnrealType.h"
#include "UObject/Stack.h"

namespace
{
	FProperty* FindFieldWithPrefix(const UStruct* Struct, const TCHAR* Prefix)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
			if (It->GetName().StartsWith(Prefix)) return *It;
		return nullptr;
	}
	void SetNumber(FProperty* Property, void* Data, double Value)
	{
		if (FNumericProperty* Number = CastField<FNumericProperty>(Property))
		{
			void* Address = Number->ContainerPtrToValuePtr<void>(Data);
			if (Number->IsFloatingPoint()) Number->SetFloatingPointPropertyValue(Address, Value);
			else Number->SetIntPropertyValue(Address, static_cast<int64>(Value));
		}
	}
}

void UVBLookAtPOIAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	BodyPartLabels.Reset();
	if (!LookAtSettings) return;
	// Reflection is limited to immutable GASP authoring data, not executing original Blueprint logic.
	FArrayProperty* Parts = FindFProperty<FArrayProperty>(LookAtSettings->GetClass(), TEXT("LookAt Body Parts"));
	FStructProperty* PartType = Parts ? CastField<FStructProperty>(Parts->Inner) : nullptr;
	FNameProperty* Label = PartType ? CastField<FNameProperty>(FindFieldWithPrefix(PartType->Struct, TEXT("label_"))) : nullptr;
	if (!Label) return;
	FScriptArrayHelper Values(Parts, Parts->ContainerPtrToValuePtr<void>(LookAtSettings));
	for (int32 Index = 0; Index < Values.Num(); ++Index)
		BodyPartLabels.AddUnique(Label->GetPropertyValue_InContainer(Values.GetRawPtr(Index)));
}

void UVBLookAtPOIAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	bLookAtEnabled = false;
	bInteractionAttention = false;
	if (const AActor* Owner = GetOwningActor())
		if (const UVBLookAtPOIComponent* Component = Owner->FindComponentByClass<UVBLookAtPOIComponent>())
		{
			bLookAtEnabled = Component->ResolveTarget(LookAtTarget);
			if (bLookAtEnabled)
				if (const auto* Attention = Owner->FindComponentByClass<UVBPlayerAttentionComponent>())
				{
					FVector SelectedPoint;
					bInteractionAttention = Attention->GetSelectedSource() == EVBPlayerAttentionSource::Interaction
						&& Attention->ResolveAttention(SelectedPoint, false) && SelectedPoint.Equals(LookAtTarget, 1.f);
				}
		}
}

void UVBLookAtPOIAnimInstance::GetLookAtWeightOverrides(TArray<int32>& Values) const { checkNoEntry(); }
void UVBLookAtPOIAnimInstance::GetLookAtLimitOverrides(TArray<int32>& Values) const { checkNoEntry(); }

DEFINE_FUNCTION(UVBLookAtPOIAnimInstance::execGetLookAtWeightOverrides)
{
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FArrayProperty>(nullptr);
	FArrayProperty* Property = CastField<FArrayProperty>(Stack.MostRecentProperty);
	void* Address = Stack.MostRecentPropertyAddress;
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->BuildCurveOverrides(Property, Address, false);
	P_NATIVE_END;
}

DEFINE_FUNCTION(UVBLookAtPOIAnimInstance::execGetLookAtLimitOverrides)
{
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FArrayProperty>(nullptr);
	FArrayProperty* Property = CastField<FArrayProperty>(Stack.MostRecentProperty);
	void* Address = Stack.MostRecentPropertyAddress;
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->BuildCurveOverrides(Property, Address, true);
	P_NATIVE_END;
}

void UVBLookAtPOIAnimInstance::BuildCurveOverrides(FArrayProperty* ArrayProperty, void* ArrayAddress, bool bLimits) const
{
	if (!ArrayProperty || !ArrayAddress) return;
	FScriptArrayHelper Values(ArrayProperty, ArrayAddress);
	Values.EmptyValues();
	const FStructProperty* Type = CastField<FStructProperty>(ArrayProperty->Inner);
	if (!Type) return;
	FNameProperty* Label = CastField<FNameProperty>(FindFieldWithPrefix(Type->Struct, TEXT("BodyPartLabel_")));
	FProperty* Override = FindFieldWithPrefix(Type->Struct, TEXT("OverrideWeight_"));
	FProperty* Value = FindFieldWithPrefix(Type->Struct, bLimits ? TEXT("AngleValue_") : TEXT("BodyPartWeight_"));
	FByteProperty* Quadrant = CastField<FByteProperty>(FindFieldWithPrefix(Type->Struct, TEXT("LimitQuadrant_")));
	if (!Label || !Override || !Value || (bLimits && (!Quadrant || !Quadrant->Enum))) return;
	static const TCHAR* Suffixes[] = { TEXT("Limit_H_Pos"), TEXT("Limit_H_Neg"), TEXT("Limit_V_Pos"), TEXT("Limit_V_Neg") };
	for (FName BodyPart : BodyPartLabels)
	{
		if (!bLimits && bInteractionAttention)
		{
			const float Weight = BodyPart == TEXT("Hips") ? 0.f : BodyPart == TEXT("Torso") ? 0.15f :
				BodyPart == TEXT("Head") ? 0.5f : -1.f;
			if (Weight >= 0.f)
			{
				void* Entry = Values.GetRawPtr(Values.AddValue());
				Label->SetPropertyValue_InContainer(Entry, BodyPart);
				SetNumber(Override, Entry, 1.f);
				SetNumber(Value, Entry, Weight);
				continue;
			}
		}
		const FString Prefix = TEXT("LookAtPOI_") + BodyPart.ToString() + TEXT("_");
		const float Mask = GetCurveValue(FName(Prefix + TEXT("Override")));
		if (FMath::IsNearlyEqual(Mask, 0.0f, 0.000001f)) continue;
		for (int32 Index = 0; Index < (bLimits ? 4 : 1); ++Index)
		{
			float CurveValue = 0.0f;
			if (!GetCurveValueWithDefault(FName(Prefix + (bLimits ? Suffixes[Index] : TEXT("Weight"))), 0.0f, CurveValue)) continue;
			void* Entry = Values.GetRawPtr(Values.AddValue());
			Label->SetPropertyValue_InContainer(Entry, BodyPart);
			SetNumber(Override, Entry, Mask);
			SetNumber(Value, Entry, CurveValue);
			if (bLimits)
			{
				const int64 EnumValue = Quadrant->Enum->GetValueByNameString(FString::Printf(TEXT("NewEnumerator%d"), Index + 3));
				Quadrant->SetPropertyValue_InContainer(Entry, static_cast<uint8>(EnumValue));
			}
		}
	}
}
