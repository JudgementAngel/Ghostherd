// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"

/**
 * Centralized argument validation for MCP tool handlers.
 *
 * Each function returns FMCPValidateResult — either Ok() or an Error() carrying
 * a fully formed FMCPToolResult that the handler can return as-is.
 *
 * Use the BAIL_IF_INVALID macro to keep handlers short:
 *
 *   BAIL_IF_INVALID(FMCPValidate::PackagePath(PackagePath));
 *   BAIL_IF_INVALID(FMCPValidate::AssetName(AssetName));
 *   BAIL_IF_INVALID(FMCPValidate::AssetDoesNotExist(PackagePath / AssetName));
 */
struct FMCPValidateResult
{
	bool bOk = true;
	FMCPToolResult Error;

	static FMCPValidateResult Ok() { return FMCPValidateResult{true, {}}; }

	/** Plain failure (legacy). Prefer FailCoded for new validators. */
	static FMCPValidateResult Fail(const FString& Message)
	{
		return FMCPValidateResult{false, FMCPToolResult::Error(Message)};
	}

	/** Coded failure carrying a structured error payload (Phase C / C1). */
	static FMCPValidateResult FailCoded(EMCPError Code, const FString& Message,
		const FString& Hint = FString(),
		const TArray<FString>& DidYouMean = TArray<FString>())
	{
		return FMCPValidateResult{false, FMCPToolResult::ErrorStructured(Code, Message, Hint, DidYouMean)};
	}
};

#define BAIL_IF_INVALID(Expr) \
	{ FMCPValidateResult __MCP_R = (Expr); if (!__MCP_R.bOk) return __MCP_R.Error; }

class UNREALMCPSERVER_API FMCPValidate
{
public:
	/** Args object is non-null. Use first thing in every handler. */
	static FMCPValidateResult Args(const TSharedPtr<FJsonObject>& Args);

	/** Field exists and is a non-empty string. Writes value to OutValue on success. */
	static FMCPValidateResult RequiredString(const TSharedPtr<FJsonObject>& Args, const FString& Field, FString& OutValue);

	/** v4: field exists and is a number. Avoids GetNumberField's silent-zero on
	 *  missing fields (matrix-found bug class). */
	static FMCPValidateResult RequiredNumber(const TSharedPtr<FJsonObject>& Args, const FString& Field, double& OutValue);

	/** v4: field exists and is a non-empty array of strings. */
	static FMCPValidateResult RequiredStringArray(const TSharedPtr<FJsonObject>& Args, const FString& Field, TArray<FString>& OutValues);

	/** Long package path: starts with /Game or /Engine, no invalid chars. */
	static FMCPValidateResult PackagePath(const FString& Path);

	/** Asset name: not empty, no invalid object-name characters. */
	static FMCPValidateResult AssetName(const FString& Name);

	/** Asserts nothing already occupies the path — checked on disk AND in memory.
	 *  A loaded duplicate is what makes the Blueprint factories assert; an unloaded
	 *  one is what makes SavePackage silently overwrite. Accepts a package path or a
	 *  full object path. Prefer MCPCommon::CreateAssetPackage, which applies this. */
	static FMCPValidateResult AssetDoesNotExist(const FString& InPath);

	/** Asserts the package DOES exist. Returns clean NotFound error otherwise. */
	static FMCPValidateResult AssetExists(const FString& PackagePath);

	/** Numeric range. */
	static FMCPValidateResult InRangeI(int32 Value, int32 Min, int32 Max, const FString& FieldName);
	static FMCPValidateResult InRangeF(double Value, double Min, double Max, const FString& FieldName);

	/** Enum: value must be in AllowedValues. */
	static FMCPValidateResult OneOf(const FString& Value, const TArray<FString>& AllowedValues, const FString& FieldName);
};
