#include "MCPInputValidator.h"
#include <cmath>

namespace
{
constexpr int32 MaxDepth = 32;
constexpr int32 MaxNodes = 16384;
constexpr int32 MaxStringUnits = 1024 * 1024;
constexpr int64 MaxTextUnits = 4 * 1024 * 1024;

struct FCheck
{
    const TSet<const FJsonValue*>* Deferred = nullptr;
    int32 Nodes = 0;
    int64 TextUnits = 0;
    FString Error;
    bool Unsupported = false;

    bool Fail(const FString& Path, const FString& Message, bool bSchema = false)
    {
        Error = Path + TEXT(": ") + Message;
        Unsupported = bSchema;
        return false;
    }
    bool Text(const FString& Value, const FString& Path)
    {
        TextUnits += Value.Len();
        return (Value.Len() <= MaxStringUnits && TextUnits <= MaxTextUnits)
            || Fail(Path, TEXT("JSON text budget exceeded"));
    }
    bool Tree(const TSharedPtr<FJsonValue>& Value, const FString& Path, int32 Depth)
    {
        if (++Nodes > MaxNodes || Depth > MaxDepth) return Fail(Path, TEXT("JSON depth/node budget exceeded"));
        if (!Value.IsValid()) return Fail(Path, TEXT("invalid JSON value"));
        if (Value->Type == EJson::Number && !std::isfinite(Value->AsNumber())) return Fail(Path, TEXT("number must be finite"));
        if (Value->Type == EJson::String) return Text(Value->AsString(), Path);
        if (Value->Type == EJson::Object)
        {
            const auto Object = Value->AsObject();
            if (!Object.IsValid()) return Fail(Path, TEXT("invalid JSON object"));
            for (const auto& Pair : Object->Values)
                if (!Text(FString(*Pair.Key), Path) || !Tree(Pair.Value, Path, Depth + 1)) return false;
        }
        if (Value->Type == EJson::Array)
            for (const auto& Item : Value->AsArray()) if (!Tree(Item, Path, Depth + 1)) return false;
        return Value->Type != EJson::None || Fail(Path, TEXT("invalid JSON type"));
    }
    bool KnownType(const FString& Type) const
    {
        return Type == TEXT("object") || Type == TEXT("array") || Type == TEXT("string")
            || Type == TEXT("number") || Type == TEXT("integer") || Type == TEXT("boolean") || Type == TEXT("null");
    }
    bool Matches(const FString& Type, const TSharedPtr<FJsonValue>& Value) const
    {
        if (Type == TEXT("object")) return Value->Type == EJson::Object;
        if (Type == TEXT("array")) return Value->Type == EJson::Array;
        if (Type == TEXT("string")) return Value->Type == EJson::String;
        if (Type == TEXT("boolean")) return Value->Type == EJson::Boolean;
        if (Type == TEXT("null")) return Value->Type == EJson::Null;
        if (Type == TEXT("number")) return Value->Type == EJson::Number;
        return Type == TEXT("integer") && Value->Type == EJson::Number && std::floor(Value->AsNumber()) == Value->AsNumber();
    }
    bool Schema(const TSharedPtr<FJsonValue>& Value, const FString& Path, int32 Depth)
    {
        if (Depth > MaxDepth) return Fail(Path, TEXT("schema depth exceeded"), true);
        if (Value->Type == EJson::Boolean) return true;
        if (Value->Type != EJson::Object) return Fail(Path, TEXT("schema must be an object or boolean"), true);
        const auto S = Value->AsObject();
        static const TSet<FString> Keywords = { TEXT("type"), TEXT("properties"), TEXT("required"), TEXT("items"), TEXT("enum"),
            TEXT("additionalProperties"), TEXT("minimum"), TEXT("maximum"), TEXT("exclusiveMinimum"), TEXT("exclusiveMaximum"),
            TEXT("minItems"), TEXT("maxItems"), TEXT("description"), TEXT("title"), TEXT("examples"), TEXT("default"),
            TEXT("$schema"), TEXT("$comment"), TEXT("deprecated"), TEXT("readOnly"), TEXT("writeOnly") };
        for (const auto& Pair : S->Values)
            if (!Keywords.Contains(FString(*Pair.Key))) return Fail(Path, TEXT("unsupported schema keyword: ") + FString(*Pair.Key), true);
        if (const auto Type = S->TryGetField(TEXT("type")))
        {
            if (Type->Type == EJson::String)
            { if (!KnownType(Type->AsString())) return Fail(Path, TEXT("unknown schema type"), true); }
            else if (Type->Type == EJson::Array && !Type->AsArray().IsEmpty())
            {
                for (const auto& T : Type->AsArray())
                    if (T->Type != EJson::String || !KnownType(T->AsString())) return Fail(Path, TEXT("invalid type union"), true);
            }
            else return Fail(Path, TEXT("invalid schema type"), true);
        }
        if (const auto Props = S->TryGetField(TEXT("properties")))
        {
            if (Props->Type != EJson::Object) return Fail(Path, TEXT("properties must be an object"), true);
            for (const auto& Pair : Props->AsObject()->Values)
                if (!Schema(Pair.Value, Path + TEXT(".properties.") + FString(*Pair.Key), Depth + 1)) return false;
        }
        if (const auto Required = S->TryGetField(TEXT("required")))
        {
            if (Required->Type != EJson::Array) return Fail(Path, TEXT("required must be an array"), true);
            TSet<FString> Names;
            for (const auto& Name : Required->AsArray())
            {
                if (Name->Type != EJson::String || Names.Contains(Name->AsString())) return Fail(Path, TEXT("required names must be unique strings"), true);
                Names.Add(Name->AsString());
            }
        }
        for (const TCHAR* Key : { TEXT("items"), TEXT("additionalProperties") })
            if (const auto Child = S->TryGetField(Key)) if (!Schema(Child, Path + TEXT(".") + Key, Depth + 1)) return false;
        if (const auto Enum = S->TryGetField(TEXT("enum")))
        {
            if (Enum->Type != EJson::Array || Enum->AsArray().IsEmpty()) return Fail(Path, TEXT("enum must be non-empty"), true);
            for (const auto& Item : Enum->AsArray())
                if (Item->Type == EJson::Object || Item->Type == EJson::Array) return Fail(Path, TEXT("structured enum values are unsupported"), true);
        }
        for (const TCHAR* Key : { TEXT("minimum"), TEXT("maximum"), TEXT("exclusiveMinimum"), TEXT("exclusiveMaximum"), TEXT("minItems"), TEXT("maxItems") })
            if (const auto Limit = S->TryGetField(Key))
            {
                if (Limit->Type != EJson::Number) return Fail(Path, TEXT("numeric schema bound required"), true);
                if ((FString(Key) == TEXT("minItems") || FString(Key) == TEXT("maxItems"))
                    && (Limit->AsNumber() < 0 || std::floor(Limit->AsNumber()) != Limit->AsNumber()))
                    return Fail(Path, TEXT("item bound must be a non-negative integer"), true);
            }
        return true;
    }
    bool Input(const TSharedPtr<FJsonValue>& SValue, const TSharedPtr<FJsonValue>& Value, const FString& Path, int32 Depth)
    {
        if (++Nodes > MaxNodes || Depth > MaxDepth) return Fail(Path, TEXT("validation work budget exceeded"));
        if (SValue->Type == EJson::Boolean) return SValue->AsBool() || Fail(Path, TEXT("value forbidden by schema"));
        if (Deferred && Deferred->Contains(Value.Get())) return true;
        const auto S = SValue->AsObject();
        if (const auto Type = S->TryGetField(TEXT("type")))
        {
            bool Match = Type->Type == EJson::String && Matches(Type->AsString(), Value);
            if (Type->Type == EJson::Array) for (const auto& T : Type->AsArray()) Match |= Matches(T->AsString(), Value);
            if (!Match) return Fail(Path, TEXT("value does not match declared type"));
        }
        if (const auto Enum = S->TryGetField(TEXT("enum")))
        {
            bool Found = false;
            for (const auto& Allowed : Enum->AsArray())
            {
                if (++Nodes > MaxNodes) return Fail(Path, TEXT("validation work budget exceeded"));
                if (Value->Type != Allowed->Type) continue;
                if (Value->Type == EJson::String) Found |= Value->AsString() == Allowed->AsString();
                else if (Value->Type == EJson::Number) Found |= Value->AsNumber() == Allowed->AsNumber();
                else if (Value->Type == EJson::Boolean) Found |= Value->AsBool() == Allowed->AsBool();
                else if (Value->Type == EJson::Null) Found = true;
            }
            if (!Found) return Fail(Path, TEXT("value is not in enum"));
        }
        if (Value->Type == EJson::Number)
        {
            const double N = Value->AsNumber();
            double Bound;
            if (S->TryGetNumberField(TEXT("minimum"), Bound) && N < Bound) return Fail(Path, TEXT("below minimum"));
            if (S->TryGetNumberField(TEXT("maximum"), Bound) && N > Bound) return Fail(Path, TEXT("above maximum"));
            if (S->TryGetNumberField(TEXT("exclusiveMinimum"), Bound) && N <= Bound) return Fail(Path, TEXT("below exclusive minimum"));
            if (S->TryGetNumberField(TEXT("exclusiveMaximum"), Bound) && N >= Bound) return Fail(Path, TEXT("above exclusive maximum"));
        }
        if (Value->Type == EJson::Object)
        {
            const auto Object = Value->AsObject();
            if (const auto Required = S->TryGetField(TEXT("required")))
                for (const auto& Name : Required->AsArray())
                    if (!Object->HasField(Name->AsString())) return Fail(Path + TEXT(".") + Name->AsString(), TEXT("required field missing"));
            const TSharedPtr<FJsonObject>* Props = nullptr;
            S->TryGetObjectField(TEXT("properties"), Props);
            for (const auto& Pair : Object->Values)
            {
                TSharedPtr<FJsonValue> Child = Props ? (*Props)->TryGetField(FString(*Pair.Key)) : nullptr;
                if (!Child.IsValid()) Child = S->TryGetField(TEXT("additionalProperties"));
                if (Child.IsValid() && !Input(Child, Pair.Value, Path + TEXT(".") + FString(*Pair.Key), Depth + 1)) return false;
            }
        }
        if (Value->Type == EJson::Array)
        {
            const auto& Array = Value->AsArray();
            double Bound;
            if (S->TryGetNumberField(TEXT("minItems"), Bound) && Array.Num() < Bound) return Fail(Path, TEXT("too few items"));
            if (S->TryGetNumberField(TEXT("maxItems"), Bound) && Array.Num() > Bound) return Fail(Path, TEXT("too many items"));
            if (const auto Items = S->TryGetField(TEXT("items")))
                for (int32 I = 0; I < Array.Num(); ++I)
                    if (!Input(Items, Array[I], FString::Printf(TEXT("%s[%d]"), *Path, I), Depth + 1)) return false;
        }
        return true;
    }
};
}

FMCPValidateResult FMCPInputValidator::Validate(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonObject>& Arguments)
{
    return ValidateDeferred(Schema, Arguments, {});
}

FMCPValidateResult FMCPInputValidator::ValidateDeferred(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonObject>& Arguments,
    const TSet<const FJsonValue*>& DeferredValues)
{
    if (!Schema.IsValid()) return FMCPValidateResult::FailCoded(EMCPError::Unsupported, TEXT("Tool has no input schema"));
    if (!Arguments.IsValid()) return FMCPValidateResult::FailCoded(EMCPError::OutOfRange, TEXT("$: arguments must be an object"));
    FCheck Check;
    Check.Deferred = &DeferredValues;
    auto SchemaValue = MakeShared<FJsonValueObject>(Schema);
    auto InputValue = MakeShared<FJsonValueObject>(Arguments);
    if (!Check.Tree(SchemaValue, TEXT("schema"), 0) || !Check.Schema(SchemaValue, TEXT("schema"), 0))
        return FMCPValidateResult::FailCoded(EMCPError::Unsupported, Check.Error);
    Check.Nodes = 0; Check.TextUnits = 0;
    if (!Check.Tree(InputValue, TEXT("$"), 0)) return FMCPValidateResult::FailCoded(EMCPError::OutOfRange, Check.Error);
    Check.Nodes = 0;
    if (!Check.Input(SchemaValue, InputValue, TEXT("$"), 0)) return FMCPValidateResult::FailCoded(EMCPError::OutOfRange, Check.Error);
    return FMCPValidateResult::Ok();
}
