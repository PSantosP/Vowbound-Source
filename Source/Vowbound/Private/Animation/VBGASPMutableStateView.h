#pragma once

#include "VBGASPStateView.h"

namespace VBGASP
{
// Writes stay on the current AnimInstance; no process-wide mutable state/cache.
struct FMutableStateView : FStateView
{
	using FStateView::FStateView;
	explicit FMutableStateView(const FStateView& Other) : FStateView(Other.Type, Other.Data) {}
	void SetBool(FName Name, bool Value) const
	{
		auto* P = CastField<FBoolProperty>(Find(Name));
		if (ensureMsgf(P, TEXT("GASP missing bool %s"), *Name.ToString())) P->SetPropertyValue_InContainer(Data, Value);
	}
	void SetNumber(FName Name, double Value) const
	{
		auto* P = CastField<FNumericProperty>(Find(Name));
		if (!ensureMsgf(P, TEXT("GASP missing numeric field %s"), *Name.ToString())) return;
		void* Address = P->ContainerPtrToValuePtr<void>(Data);
		if (P->IsFloatingPoint()) P->SetFloatingPointPropertyValue(Address, Value);
		else P->SetIntPropertyValue(Address, static_cast<int64>(Value));
	}
	void SetEnumId(FName Name, const TCHAR* Enumerator) const
	{
		auto* P = CastField<FByteProperty>(Find(Name));
		if (!ensureMsgf(P && P->Enum, TEXT("GASP missing enum %s"), *Name.ToString())) return;
		const int64 Value = P->Enum->GetValueByNameString(Enumerator);
		if (ensureMsgf(Value != INDEX_NONE, TEXT("GASP missing %s.%s"), *Name.ToString(), Enumerator))
			P->SetPropertyValue_InContainer(Data, static_cast<uint8>(Value));
	}
	bool IsEnumId(FName Name, const TCHAR* Enumerator) const
	{
		const auto* P = CastField<FByteProperty>(Find(Name));
		return P && P->Enum && P->GetPropertyValue_InContainer(Data) == P->Enum->GetValueByNameString(Enumerator);
	}
	void Copy(FName ToName, const FStateView& From, FName FromName) const
	{
		FProperty* To = Find(ToName);
		FProperty* Source = From.Find(FromName);
		if (ensureMsgf(To && Source && To->SameType(Source), TEXT("GASP incompatible fields %s <- %s"), *ToName.ToString(), *FromName.ToString()))
			To->CopyCompleteValue(To->ContainerPtrToValuePtr<void>(Data), Source->ContainerPtrToValuePtr<void>(From.Data));
	}
	template<typename T> void SetStruct(FName Name, const T& Value) const
	{
		auto* P = CastField<FStructProperty>(Find(Name));
		if (ensureMsgf(P && P->Struct == TBaseStructure<T>::Get(), TEXT("GASP incompatible struct %s"), *Name.ToString()))
			P->CopyCompleteValue(P->ContainerPtrToValuePtr<void>(Data), &Value);
	}
	template<typename T> void SetNativeStruct(FName Name, const T& Value) const
	{
		auto* P = CastField<FStructProperty>(Find(Name));
		if (ensureMsgf(P && P->Struct == T::StaticStruct(), TEXT("GASP incompatible native struct %s"), *Name.ToString()))
			P->CopyCompleteValue(P->ContainerPtrToValuePtr<void>(Data), &Value);
	}
	void SetObject(FName Name, UObject* Value) const
	{
		auto* P = CastField<FObjectPropertyBase>(Find(Name));
		if (ensureMsgf(P && (!Value || Value->IsA(P->PropertyClass)), TEXT("GASP incompatible object %s"), *Name.ToString()))
			P->SetObjectPropertyValue_InContainer(Data, Value);
	}
	void SetObjectArray(FName Name, const TArray<UObject*>& Values) const
	{
		auto* P = CastField<FArrayProperty>(Find(Name));
		auto* Item = P ? CastField<FObjectPropertyBase>(P->Inner) : nullptr;
		if (!ensureMsgf(Item, TEXT("GASP missing object array %s"), *Name.ToString())) return;
		FScriptArrayHelper Array(P, P->ContainerPtrToValuePtr<void>(Data));
		Array.EmptyValues();
		for (UObject* Value : Values)
		{
			if (!Value || !Value->IsA(Item->PropertyClass)) continue;
			Item->SetObjectPropertyValue(Array.GetRawPtr(Array.AddValue()), Value);
		}
	}
	void SetNames(FName Name, const TArray<FName>& Values) const
	{
		auto* P = CastField<FArrayProperty>(Find(Name));
		if (ensureMsgf(P && CastField<FNameProperty>(P->Inner), TEXT("GASP missing name array %s"), *Name.ToString()))
			P->CopyCompleteValue(P->ContainerPtrToValuePtr<void>(Data), &Values);
	}
};
}
