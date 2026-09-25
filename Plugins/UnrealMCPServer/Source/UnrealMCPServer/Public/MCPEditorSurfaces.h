#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class FMCPToolRegistry;
class FMCPResourceProvider;
class FMCPPromptProvider;
struct FMCPRequestContext;

/**
 * Increment 6: initial editor-surface discovery, explicit reveal and fresh Slate capture
 * (first slice of V5-35/36). Surfaces are process-wide editor UI facts identified by the
 * Slate widget's unique id; frames are returned inline and are not retained. No live
 * observer, artifact store or after-operation readiness exists yet.
 */
namespace MCPEditorSurfaces
{
    struct FEncodeOptions
    {
        FString Format = TEXT("png");   // png | jpeg
        int32 MaxLongEdge = 1280;       // 64..4096; never upscales
        int32 JpegQuality = 85;         // 1..100
        int64 MaxBytes = 1024 * 1024;   // encoded cap; exceeding it is an explicit failure
    };
    struct FEncodedFrame
    {
        int32 Width = 0, Height = 0, SourceWidth = 0, SourceHeight = 0;
        double Scale = 1.0;
        FString Mime, PixelHash, Error;
        TArray64<uint8> Bytes;
    };
    /** Pure, deterministic BGRA -> PNG/JPEG encoding with aspect-preserving box downsampling. */
    UNREALMCPSERVER_API bool EncodeFrame(const TArray<FColor>& Pixels, int32 Width, int32 Height, const FEncodeOptions& Options, FEncodedFrame& Out);

    UNREALMCPSERVER_API void RegisterAll(FMCPToolRegistry& Registry);
    UNREALMCPSERVER_API void RegisterResources(FMCPResourceProvider& Provider);
    UNREALMCPSERVER_API void RegisterPrompts(FMCPPromptProvider& Provider);
    UNREALMCPSERVER_API void Reset();

    /** V5-37 initial slice: owned observer leases sampled from the core ticker. Exposed for fixture tests. */
    UNREALMCPSERVER_API void TickObservers(double NowSeconds);
    UNREALMCPSERVER_API void ClearSession(const FString& SessionId);
    /** Bounded counters for get_server_health (no frame bytes, no owner identities). */
    UNREALMCPSERVER_API TSharedPtr<FJsonObject> DiagnosticsJson();

    // v5 increment 23: after-operation readiness, retained captures and frame comparison (V5-36/37/38).
    /** Slate post-tick counter: a frame captured after an operation is fresh only when this advanced past the operation's end mark. */
    UNREALMCPSERVER_API uint64 CurrentPaintSequence();
    /** Test hook: advance the paint counter as if Slate had painted once. */
    UNREALMCPSERVER_API void AdvancePaintForTest();
    /** Test hook: retain a synthetic BGRA frame for the caller and return its frame id. */
    UNREALMCPSERVER_API FString InjectTestFrame(const FMCPRequestContext& Context, const FString& SurfaceId, const TArray<FColor>& Pixels, int32 Width, int32 Height);
    /** Pixel comparison of two owned frames (observer or retained). Returns nullptr with OutError on failure. */
    UNREALMCPSERVER_API TSharedPtr<FJsonObject> CompareFrames(const FString& BeforeFrameId, const FString& AfterFrameId, const FMCPRequestContext& Context, int32 PixelThreshold, FString& OutError);
}
