// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Common/MCPPropertyIO.h"

#include "JsonObjectConverter.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"

namespace MCPCommon
{

namespace
{
	/** Human/agent-readable description of what a property expects. */
	FString DescribeExpectedFormat(const FProperty* Prop)
	{
		if (CastField<FStructProperty>(Prop))
		{
			const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
			return FString::Printf(TEXT("struct %s — UE text syntax '(X=0,Y=0,Z=0)' or a JSON object with the struct's fields"),
				*StructProp->Struct->GetName());
		}
		if (CastField<FArrayProperty>(Prop))
		{
			return TEXT("array — a JSON array of element values");
		}
		if (CastField<FMapProperty>(Prop))
		{
			return TEXT("map — a JSON object of key/value pairs");
		}
		if (CastField<FSetProperty>(Prop))
		{
			return TEXT("set — a JSON array of unique element values");
		}
		if (CastField<FEnumProperty>(Prop) || CastField<FByteProperty>(Prop))
		{
			return TEXT("enum — the enumerator name as a string (short form accepted)");
		}
		if (CastField<FBoolProperty>(Prop))
		{
			return TEXT("boolean — true/false");
		}
		if (CastField<FNumericProperty>(Prop))
		{
			return TEXT("number");
		}
		if (CastField<FObjectPropertyBase>(Prop))
		{
			return TEXT("object reference — a content path string like /Game/Folder/Asset.Asset");
		}
		return FString::Printf(TEXT("%s — UE text-export syntax"), *Prop->GetClass()->GetName());
	}
}

bool SetPropertyFromJson(FProperty* Prop, void* Container, UObject* OwnerForImportText,
	const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	if (!Prop || !Container || !Value.IsValid())
	{
		OutError = TEXT("Internal: null property/container/value");
		return false;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);

	// Strings first try UE's text-import syntax — it covers enum names,
	// "X=,Y=,Z=" structs, object paths, and plain scalars.
	if (Value->Type == EJson::String)
	{
		if (Prop->ImportText_Direct(*Value->AsString(), ValuePtr, OwnerForImportText, PPF_None))
		{
			return true;
		}
	}

	// Everything else (and strings that failed text import) goes through the
	// JSON converter, which understands arrays, maps, sets, and nested structs.
	FText FailReason;
	if (FJsonObjectConverter::JsonValueToUProperty(Value, Prop, ValuePtr,
		/*CheckFlags*/ 0, /*SkipFlags*/ 0, /*bStrictMode*/ false, &FailReason))
	{
		return true;
	}

	OutError = FString::Printf(TEXT("Could not parse value for '%s'. Expected %s.%s"),
		*Prop->GetName(), *DescribeExpectedFormat(Prop),
		FailReason.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *FailReason.ToString()));
	return false;
}

bool SetPropertyFromString(FProperty* Prop, void* Container, UObject* OwnerForImportText,
	const FString& Value, FString& OutError)
{
	// Try raw text import first; fall back to treating the string as JSON
	// (lets agents pass '[1,2,3]' or '{"x":1}' in string args).
	if (Prop && Container)
	{
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
		if (Prop->ImportText_Direct(*Value, ValuePtr, OwnerForImportText, PPF_None))
		{
			return true;
		}

		const FString Trimmed = Value.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("[")) || Trimmed.StartsWith(TEXT("{")))
		{
			TSharedPtr<FJsonValue> Parsed;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				return SetPropertyFromJson(Prop, Container, OwnerForImportText, Parsed, OutError);
			}
		}
	}

	OutError = FString::Printf(TEXT("Could not parse '%s' for '%s'. Expected %s."),
		*Value, Prop ? *Prop->GetName() : TEXT("<null>"),
		Prop ? *DescribeExpectedFormat(Prop) : TEXT("a valid property"));
	return false;
}

bool SetObjectPropertyWithNotify(FProperty* Prop, UObject* Object,
	const FString& Value, FString& OutError)
{
	if (!Prop || !Object)
	{
		OutError = TEXT("Internal: null property/object");
		return false;
	}

	Object->Modify();
	Object->PreEditChange(Prop);

	const bool bOk = SetPropertyFromString(Prop, Object, Object, Value, OutError);

	// Always pair PreEditChange with PostEditChangeProperty, even on a failed
	// import — owners like UStaticMeshComponent re-sync cached state here.
	FPropertyChangedEvent ChangeEvent(Prop, EPropertyChangeType::ValueSet);
	Object->PostEditChangeProperty(ChangeEvent);

	return bOk;
}

TSharedPtr<FJsonValue> ExportPropertyToJson(FProperty* Prop, const void* Container)
{
	if (!Prop || !Container)
	{
		return nullptr;
	}
	const void* ValuePtr = Prop->ContainerPtrToValuePtr<const void>(Container);
	return FJsonObjectConverter::UPropertyToJsonValue(Prop, ValuePtr);
}

}
