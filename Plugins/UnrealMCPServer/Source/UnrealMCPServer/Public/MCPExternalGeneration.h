// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
class FMCPToolRegistry;

/** v5 increment 20: non-blocking external generation jobs (V5-30). Submission, polling and
 *  download run through the async HTTP module on the editor ticker as owned operations; only the
 *  final import touches assets. Credentials never leave the C++ request headers. A `mock` provider
 *  exercises the whole pipeline without any network or billable call. */
namespace MCPExternalGeneration
{
    UNREALMCPSERVER_API void RegisterAll(FMCPToolRegistry& Registry);
}
