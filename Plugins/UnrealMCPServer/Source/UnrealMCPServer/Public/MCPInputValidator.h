#pragma once
#include "MCPValidate.h"

/** Bounded JSON Schema subset for native and imported tool inputs. No UObject access. */
class UNREALMCPSERVER_API FMCPInputValidator
{
public:
    static FMCPValidateResult Validate(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonObject>& Arguments);
    /** Script preflight only: defer exact reference-value nodes, never registry dispatch. */
    static FMCPValidateResult ValidateDeferred(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonObject>& Arguments,
        const TSet<const FJsonValue*>& DeferredValues);
};
