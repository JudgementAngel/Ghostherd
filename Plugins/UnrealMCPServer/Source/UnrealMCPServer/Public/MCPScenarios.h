#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class FMCPToolRegistry;
struct FMCPRequestContext;

/** v5 increment 20: a driver-based operation advances on the editor ticker; the driver reports progress and finishes itself. */
struct FMCPOperationTick
{
    FString OperationId;
    double NowSeconds = 0.0, ElapsedSeconds = 0.0;
    bool bCancelRequested = false;
    TFunction<void(const FString& Type, const FString& Message)> Event;
    TFunction<void(double Progress, const FString& Message)> Progress;
    /** State: succeeded | failed | cancelled. */
    TFunction<void(const FString& State, const FString& Error, TSharedPtr<FJsonObject> Result)> Finish;
};
using FMCPOperationDriver = TFunction<void(FMCPOperationTick&)>;

/** v5 increment 16: owned, tick-driven editor operations and PIE scenarios (V5-10/V5-28 initial slices). */
namespace MCPScenarios
{
    UNREALMCPSERVER_API void RegisterAll(FMCPToolRegistry& Registry);
    /** Advance all operations; called by the core ticker, exposed for fixture tests. */
    UNREALMCPSERVER_API void TickOperations(double NowSeconds);
    UNREALMCPSERVER_API void ClearSession(const FString& SessionId);
    UNREALMCPSERVER_API void Reset();
    UNREALMCPSERVER_API TSharedPtr<FJsonObject> DiagnosticsJson();
    /** Start an owned driver-based operation (quotas and deadline apply). Returns the id, or empty with OutError set. */
    UNREALMCPSERVER_API FString StartDrivenOperation(const FMCPRequestContext& Context, const FString& Kind, const FString& Name, double DeadlineSeconds,
        FMCPOperationDriver Driver, TFunction<void(const FString& Reason)> OnAbort, TSharedPtr<FJsonObject> Details, FString& OutError);
    /** v5 increment 23: state, end time (platform seconds, 0 while running) and the Slate paint sequence at the end of an owned operation. */
    UNREALMCPSERVER_API bool GetOperationMark(const FString& Id, const FMCPRequestContext& Context, FString& OutState, double& OutEndedAt, uint64& OutPaintAtEnd);
    /** Owner-checked operation description (nullptr when unknown or foreign). */
    UNREALMCPSERVER_API TSharedPtr<FJsonObject> DescribeOperation(const FString& Id, const FMCPRequestContext& Context, bool bEvents);
}
