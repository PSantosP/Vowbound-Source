#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

// The imported Choosers own the BP context type. This view only crosses that
// serialization boundary; all decisions using these values are native code.
// No ProcessEvent, Blueprint function execution, or shared mutable cache.
namespace VBGASP
{
struct FStateView
{
	const UStruct* Type = nullptr;
	void* Data = nullptr;
	explicit FStateView(UObject* Object) : Type(Object ? Object->GetClass() : nullptr), Data(Object) {}
	FStateView(const UStruct* InType, void* InData) : Type(InType), Data(InData) {}
	FProperty* Find(FName Name) const
	{
		if (!Type || !Data) return nullptr;
		if (FProperty* Exact = Type->FindPropertyByName(Name)) return Exact;
		const FString Prefix = Name.ToString() + TEXT("_");
		for (TFieldIterator<FProperty> It(Type); It; ++It)
			if (It->GetName().StartsWith(Prefix)) return *It;
		return nullptr;
	}
	FStateView Child(FName Name) const
	{
		const auto* P = CastField<FStructProperty>(Find(Name));
		return FStateView(P ? P->Struct : nullptr, P ? P->ContainerPtrToValuePtr<void>(Data) : nullptr);
	}
	bool Bool(FName Name) const
	{
		const auto* P = CastField<FBoolProperty>(Find(Name));
		return P && P->GetPropertyValue_InContainer(Data);
	}
	double Number(FName Name) const
	{
		const auto* P = CastField<FNumericProperty>(Find(Name));
		if (!P) return 0.0;
		const void* Address = P->ContainerPtrToValuePtr<void>(Data);
		return P->IsFloatingPoint() ? P->GetFloatingPointPropertyValue(Address) : P->GetSignedIntPropertyValue(Address);
	}
	bool Is(FName Name, const TCHAR* AuthoredValue) const
	{
		const auto* P = CastField<FByteProperty>(Find(Name));
		return P && P->Enum && P->Enum->GetAuthoredNameStringByValue(P->GetPropertyValue_InContainer(Data)) == AuthoredValue;
	}
	template<typename T> T Struct(FName Name) const
	{
		const auto* P = CastField<FStructProperty>(Find(Name));
		return P && P->Struct == TBaseStructure<T>::Get() ? *P->ContainerPtrToValuePtr<T>(Data) : T{};
	}
	template<typename T> T NativeStruct(FName Name) const
	{
		const auto* P = CastField<FStructProperty>(Find(Name));
		return P && P->Struct == T::StaticStruct() ? *P->ContainerPtrToValuePtr<T>(Data) : T{};
	}
	bool HasName(FName Name, FName Value) const
	{
		const auto* P = CastField<FArrayProperty>(Find(Name));
		if (!P || !CastField<FNameProperty>(P->Inner)) return false;
		return P->ContainerPtrToValuePtr<TArray<FName>>(Data)->Contains(Value);
	}
};
}
