#include "MCPEditorSurfaces.h"
#include "MCPToolBuilder.h"
#include "MCPToolRegistry.h"
#include "MCPResourceProvider.h"
#include "MCPPromptProvider.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Misc/PackageName.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"
#include "Misc/App.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "Containers/Ticker.h"
#include "MCPScenarios.h"
#include "MCPRequestContext.h"

namespace MCPEditorSurfaces
{
namespace
{
constexpr int32 MaxWalkDepth = 96;
constexpr int32 MaxWalkNodes = 50000;
constexpr int32 MaxCatalogEntries = 2000;

struct FEntry
{
    TWeakPtr<SWidget> Widget;
    FString Kind, Fingerprint;
    uint32 Generation = 0;
};
TMap<FString, FEntry> Catalog;
uint32 CatalogGeneration = 0;
uint64 FrameSequence = 0;
uint64 PaintSequence = 0; // v5 increment 23: Slate post-tick counter

struct FBackend { bool bAvailable = false; FString Name = TEXT("none"), Reason; };
FBackend Backend()
{
    FBackend Out;
    if (!FSlateApplication::IsInitialized()) { Out.Reason = TEXT("Slate application is not initialized"); return Out; }
    if (GUsingNullRHI) { Out.Reason = TEXT("NullRHI process: no visual backend; structural discovery only"); return Out; }
    if (!FApp::CanEverRender()) { Out.Reason = TEXT("This process cannot render"); return Out; }
    Out.bAvailable = true; Out.Name = TEXT("slate_widget");
    return Out;
}
TSharedPtr<FJsonObject> BackendJson(const FBackend& B)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetBoolField(TEXT("available"), B.bAvailable); Out->SetStringField(TEXT("name"), B.Name);
    if (!B.Reason.IsEmpty()) Out->SetStringField(TEXT("reason"), B.Reason);
    return Out;
}
FMCPToolResult Error(EMCPError Code, const FString& Message, const FString& Hint = FString())
{
    return FMCPToolResult::ErrorStructured(Code, Message, Hint);
}
FString IdFor(const SWidget& Widget) { return FString::Printf(TEXT("srf-%llu"), (unsigned long long)Widget.GetId()); }

TSharedPtr<FJsonObject> RectJson(double X, double Y, double W, double H)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetNumberField(TEXT("x"), X); Out->SetNumberField(TEXT("y"), Y);
    Out->SetNumberField(TEXT("width"), W); Out->SetNumberField(TEXT("height"), H);
    return Out;
}
TSharedPtr<FJsonObject> GeometryJson(const SWidget& Widget)
{
    const FSlateRect R = Widget.GetTickSpaceGeometry().GetLayoutBoundingRect();
    return RectJson(R.Left, R.Top, FMath::Max(0.f, R.Right - R.Left), FMath::Max(0.f, R.Bottom - R.Top));
}
void CollectLiveWindows(const TArray<TSharedRef<SWindow>>& Windows, TArray<TSharedRef<SWindow>>& Out)
{
    for (const auto& W : Windows) { Out.Add(W); CollectLiveWindows(W->GetChildWindows(), Out); }
}
TArray<TSharedRef<SWindow>> LiveWindows()
{
    TArray<TSharedRef<SWindow>> Out;
    if (FSlateApplication::IsInitialized()) CollectLiveWindows(FSlateApplication::Get().GetTopLevelWindows(), Out);
    return Out;
}
bool IsLiveWindow(const TSharedPtr<SWindow>& Window)
{
    if (!Window.IsValid()) return false;
    for (const auto& W : LiveWindows()) if (&W.Get() == Window.Get()) return true;
    return false;
}
bool AncestorsVisible(TSharedPtr<SWidget> Widget)
{
    for (int32 Guard = 0; Widget.IsValid() && Guard < 512; ++Guard)
    {
        if (!Widget->GetVisibility().IsVisible()) return false;
        Widget = Widget->GetParentWidget();
    }
    return true;
}
void SetCapture(const TSharedPtr<FJsonObject>& Desc, const FBackend& B, bool bTargetVisible, const FString& HiddenReason)
{
    auto C = MakeShared<FJsonObject>();
    const bool Supported = B.bAvailable && bTargetVisible;
    C->SetBoolField(TEXT("supported"), Supported);
    C->SetStringField(TEXT("backend"), Supported ? B.Name : TEXT("none"));
    if (!B.bAvailable) C->SetStringField(TEXT("reason"), B.Reason);
    else if (!bTargetVisible) C->SetStringField(TEXT("reason"), HiddenReason);
    Desc->SetObjectField(TEXT("capture"), C);
}
FString WindowTypeName(const SWindow& W)
{
    switch (W.GetType())
    {
    case EWindowType::Menu: return TEXT("menu");
    case EWindowType::ToolTip: return TEXT("tooltip");
    case EWindowType::Notification: return TEXT("notification");
    case EWindowType::CursorDecorator: return TEXT("cursor_decorator");
    default: return TEXT("normal");
    }
}
FString WindowKind(const SWindow& W)
{
    if (W.GetType() != EWindowType::Normal) return TEXT("overlay");
    return const_cast<SWindow&>(W).IsModalWindow() ? TEXT("modal") : TEXT("window");
}
bool WindowShowable(const SWindow& W) { return W.IsVisible() && !W.IsWindowMinimized(); }
FString WindowHiddenReason(const SWindow& W)
{
    if (W.IsWindowMinimized()) return TEXT("minimized: reveal required");
    if (!W.IsVisible()) return TEXT("hidden window: reveal required");
    return FString();
}

struct FScan
{
    FBackend B;
    TSet<FString> Seen;
    TArray<TSharedPtr<FJsonObject>> Descriptors;
    TMap<FTabManager*, TArray<FString>> ManagerAssets;
    TMap<FTabManager*, FString> ManagerEditors;
    TMap<SDockTab*, TArray<FString>> OwnerTabAssets;
    TMap<SDockTab*, FString> OwnerTabEditors;
    int32 Budget = MaxWalkNodes;
};
void Upsert(FScan& S, const TSharedRef<SWidget>& Widget, const FString& Kind, const TSharedPtr<FJsonObject>& Desc)
{
    const FString Id = IdFor(*Widget);
    if (S.Seen.Contains(Id)) return;
    if (!Catalog.Contains(Id) && Catalog.Num() >= MaxCatalogEntries) return;
    S.Seen.Add(Id);
    Desc->SetStringField(TEXT("surface_id"), Id);
    Desc->SetStringField(TEXT("kind"), Kind);
    const FString Fingerprint = JsonToString(Desc);
    FEntry& E = Catalog.FindOrAdd(Id);
    if (E.Widget.Pin().Get() != &Widget.Get() || E.Kind != Kind || E.Fingerprint != Fingerprint) E.Generation = ++CatalogGeneration;
    E.Widget = Widget; E.Kind = Kind; E.Fingerprint = Fingerprint;
    Desc->SetNumberField(TEXT("generation"), E.Generation);
    S.Descriptors.Add(Desc);
}
TSharedPtr<FJsonObject> DescribeWindow(const FScan& S, const TSharedRef<SWindow>& W)
{
    auto D = MakeShared<FJsonObject>();
    D->SetStringField(TEXT("widget_type"), W->GetTypeAsString());
    D->SetStringField(TEXT("title"), W->GetTitle().ToString());
    D->SetStringField(TEXT("window_type"), WindowTypeName(*W));
    D->SetBoolField(TEXT("modal"), W->IsModalWindow());
    D->SetBoolField(TEXT("active"), W->IsActive());
    D->SetBoolField(TEXT("visible"), W->IsVisible());
    D->SetBoolField(TEXT("minimized"), W->IsWindowMinimized());
    D->SetBoolField(TEXT("maximized"), W->IsWindowMaximized());
    if (const TSharedPtr<SWindow> Parent = W->GetParentWindow()) D->SetStringField(TEXT("parent_surface_id"), IdFor(*Parent));
    const FVector2D P = W->GetPositionInScreen(), Sz = W->GetSizeInScreen();
    D->SetObjectField(TEXT("screen_rect"), RectJson(P.X, P.Y, Sz.X, Sz.Y));
    D->SetNumberField(TEXT("dpi_scale"), W->GetDPIScaleFactor());
    SetCapture(D, S.B, WindowShowable(*W), WindowHiddenReason(*W));
    return D;
}
TSharedPtr<FJsonObject> DescribeTab(FScan& S, const TSharedRef<SDockTab>& Tab, const TSharedRef<SWindow>& Window)
{
    auto D = MakeShared<FJsonObject>();
    D->SetStringField(TEXT("widget_type"), Tab->GetTypeAsString());
    D->SetStringField(TEXT("title"), Tab->GetTabLabel().ToString());
    const TCHAR* Role = TEXT("panel");
    switch (Tab->GetTabRole()) { case ETabRole::MajorTab: Role = TEXT("major"); break; case ETabRole::NomadTab: Role = TEXT("nomad"); break; case ETabRole::DocumentTab: Role = TEXT("document"); break; default: break; }
    D->SetStringField(TEXT("tab_role"), Role);
    D->SetStringField(TEXT("layout_id"), Tab->GetLayoutIdentifier().ToString());
    D->SetBoolField(TEXT("foreground"), Tab->IsForeground());
    D->SetBoolField(TEXT("active"), Tab->IsActive());
    D->SetStringField(TEXT("parent_surface_id"), IdFor(*Window));
    D->SetStringField(TEXT("window_title"), Window->GetTitle().ToString());
    if (const TSharedPtr<FTabManager> Manager = Tab->GetTabManagerPtr())
    {
        if (const TArray<FString>* Assets = S.ManagerAssets.Find(Manager.Get()))
        {
            TArray<TSharedPtr<FJsonValue>> Arr;
            for (const FString& A : *Assets) Arr.Add(MakeShared<FJsonValueString>(A));
            D->SetArrayField(TEXT("asset_paths"), Arr);
        }
        if (const FString* Editor = S.ManagerEditors.Find(Manager.Get())) D->SetStringField(TEXT("editor_name"), *Editor);
        D->SetBoolField(TEXT("owner_tab"), Manager->GetOwnerTab().Get() == &Tab.Get());
    }
    if (const TArray<FString>* Assets = S.OwnerTabAssets.Find(&Tab.Get()))
    {
        TArray<TSharedPtr<FJsonValue>> Arr;
        for (const FString& A : *Assets) Arr.Add(MakeShared<FJsonValueString>(A));
        D->SetArrayField(TEXT("asset_paths"), Arr);
        if (const FString* Editor = S.OwnerTabEditors.Find(&Tab.Get())) D->SetStringField(TEXT("editor_name"), *Editor);
        D->SetBoolField(TEXT("owner_tab"), true);
    }
    // Visibility is judged on the content widget: a foreground tab's header may sit in a
    // collapsed tab well (UMG Designer hides it) while its content is fully painted.
    const TSharedRef<SWidget> Content = Tab->GetContent();
    const bool bContentAttached = Content->GetParentWidget().IsValid();
    const bool bShowable = Tab->IsForeground() && WindowShowable(*Window) && bContentAttached && AncestorsVisible(Content);
    D->SetObjectField(TEXT("screen_rect"), bShowable ? GeometryJson(*Content) : GeometryJson(*Tab));
    D->SetNumberField(TEXT("dpi_scale"), Window->GetDPIScaleFactor());
    D->SetBoolField(TEXT("visible"), bShowable);
    FString Hidden = !Tab->IsForeground() ? TEXT("tab is not foreground: reveal required") : WindowHiddenReason(*Window);
    if (Hidden.IsEmpty() && !bShowable) Hidden = bContentAttached ? TEXT("tab content is collapsed or hidden by an ancestor") : TEXT("tab content is not attached to the window");
    SetCapture(D, S.B, bShowable, Hidden);
    return D;
}
TSharedPtr<FJsonObject> DescribeLevelViewport(const FScan& S, const TSharedRef<SLevelViewport>& Viewport, const TSharedRef<SWindow>& Window)
{
    auto D = MakeShared<FJsonObject>();
    D->SetStringField(TEXT("widget_type"), Viewport->GetTypeAsString());
    D->SetStringField(TEXT("title"), TEXT("Level viewport"));
    D->SetStringField(TEXT("parent_surface_id"), IdFor(*Window));
    D->SetStringField(TEXT("window_title"), Window->GetTitle().ToString());
    if (UWorld* World = Viewport->GetLevelViewportClient().GetWorld()) D->SetStringField(TEXT("world_path"), World->GetPathName());
    FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
    D->SetBoolField(TEXT("active"), LevelEditor && LevelEditor->GetFirstActiveLevelViewport().Get() == &Viewport.Get());
    D->SetBoolField(TEXT("pie_session_active"), GEditor && GEditor->IsPlaySessionInProgress());
    const bool bShowable = WindowShowable(*Window) && AncestorsVisible(Viewport);
    D->SetObjectField(TEXT("screen_rect"), GeometryJson(*Viewport));
    D->SetNumberField(TEXT("dpi_scale"), Window->GetDPIScaleFactor());
    D->SetBoolField(TEXT("visible"), bShowable);
    SetCapture(D, S.B, bShowable, WindowShowable(*Window) ? TEXT("viewport widget is collapsed or hidden") : WindowHiddenReason(*Window));
    return D;
}
void Walk(FScan& S, const TSharedRef<SWidget>& Widget, const TSharedRef<SWindow>& Window, int32 Depth)
{
    if (Depth > MaxWalkDepth || --S.Budget <= 0) return;
    static const FName TabType(TEXT("SDockTab")), ViewportType(TEXT("SLevelViewport"));
    const FName Type = Widget->GetType();
    if (Type == TabType) Upsert(S, Widget, TEXT("tab"), DescribeTab(S, StaticCastSharedRef<SDockTab>(Widget), Window));
    else if (Type == ViewportType) Upsert(S, Widget, TEXT("level_viewport"), DescribeLevelViewport(S, StaticCastSharedRef<SLevelViewport>(Widget), Window));
    FChildren* Children = Widget->GetChildren();
    if (!Children) return;
    for (int32 I = 0; I < Children->Num(); ++I) Walk(S, Children->GetChildAt(I), Window, Depth + 1);
}
void CollectAssetEditors(FScan& S)
{
    UAssetEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
    if (!Sub) return;
    for (UObject* Asset : Sub->GetAllEditedAssets())
    {
        if (!IsValid(Asset)) continue;
        for (IAssetEditorInstance* Instance : Sub->FindEditorsForAsset(Asset))
        {
            if (!Instance) continue;
            const TSharedPtr<FTabManager> Manager = Instance->GetAssociatedTabManager();
            if (!Manager.IsValid()) continue;
            S.ManagerAssets.FindOrAdd(Manager.Get()).AddUnique(Asset->GetPathName());
            S.ManagerEditors.FindOrAdd(Manager.Get()) = Instance->GetEditorName().ToString();
            // The editor's major tab belongs to the global tab manager; associate it by owner tab.
            if (const TSharedPtr<SDockTab> Owner = Manager->GetOwnerTab())
            {
                S.OwnerTabAssets.FindOrAdd(Owner.Get()).AddUnique(Asset->GetPathName());
                S.OwnerTabEditors.FindOrAdd(Owner.Get()) = Instance->GetEditorName().ToString();
            }
        }
    }
}
/** Enumerate every live window and the tabs/viewports inside it, refreshing the catalog. */
FScan Scan()
{
    FScan S; S.B = Backend();
    if (FSlateApplication::IsInitialized())
    {
        CollectAssetEditors(S);
        for (const TSharedRef<SWindow>& Window : LiveWindows())
        {
            Upsert(S, Window, WindowKind(*Window), DescribeWindow(S, Window));
            Walk(S, Window, Window, 0);
        }
    }
    for (auto It = Catalog.CreateIterator(); It; ++It)
        if (!S.Seen.Contains(It.Key()) || !It.Value().Widget.IsValid()) It.RemoveCurrent();
    return S;
}
TSharedPtr<FJsonObject> ScanJson(const FScan& S, const FString& KindFilter, const FString& TitleFilter, int32 Offset, int32 Limit)
{
    TArray<TSharedPtr<FJsonValue>> Items; int32 Matched = 0;
    for (const auto& D : S.Descriptors)
    {
        if (KindFilter != TEXT("any") && D->GetStringField(TEXT("kind")) != KindFilter) continue;
        if (!TitleFilter.IsEmpty() && !D->GetStringField(TEXT("title")).Contains(TitleFilter)) continue;
        if (Matched++ < Offset) continue;
        if (Items.Num() < Limit) Items.Add(MakeShared<FJsonValueObject>(D));
    }
    auto Out = MakeShared<FJsonObject>();
    Out->SetNumberField(TEXT("catalog_generation"), CatalogGeneration);
    Out->SetObjectField(TEXT("visual_backend"), BackendJson(S.B));
    Out->SetNumberField(TEXT("total_surfaces"), S.Descriptors.Num());
    Out->SetNumberField(TEXT("matched"), Matched);
    Out->SetNumberField(TEXT("offset"), Offset);
    Out->SetBoolField(TEXT("truncated"), Matched > Offset + Items.Num());
    Out->SetBoolField(TEXT("walk_budget_exhausted"), S.Budget <= 0);
    Out->SetArrayField(TEXT("surfaces"), Items);
    // v5 increment 23: blocking overlays are named explicitly so a caller never mistakes a dialog for the target.
    FString ModalId, MenuId;
    if (FSlateApplication::IsInitialized())
    {
        if (TSharedPtr<SWindow> M = FSlateApplication::Get().GetActiveModalWindow()) ModalId = IdFor(*M);
        if (TSharedPtr<SWindow> Menu = FSlateApplication::Get().GetVisibleMenuWindow()) MenuId = IdFor(*Menu);
    }
    Out->SetStringField(TEXT("active_modal_surface_id"), ModalId); Out->SetBoolField(TEXT("modal_blocking"), !ModalId.IsEmpty());
    Out->SetStringField(TEXT("visible_menu_surface_id"), MenuId);
    Out->SetNumberField(TEXT("paint_sequence"), (double)PaintSequence);
    Out->SetStringField(TEXT("captured_at"), FDateTime::UtcNow().ToIso8601());
    return Out;
}
/** Resolve an owned catalog entry, rescanning so the descriptor and generation are current. */
struct FResolved { FEntry* Entry = nullptr; TSharedPtr<SWidget> Widget; TSharedPtr<FJsonObject> Desc; FBackend B; };
FResolved Resolve(const FString& Id)
{
    FResolved R;
    const FScan S = Scan(); R.B = S.B;
    R.Entry = Catalog.Find(Id);
    if (!R.Entry) return R;
    R.Widget = R.Entry->Widget.Pin();
    for (const auto& D : S.Descriptors) if (D->GetStringField(TEXT("surface_id")) == Id) { R.Desc = D; break; }
    if (!R.Widget.IsValid() || !R.Desc.IsValid() || !IsLiveWindow(FSlateApplication::Get().FindWidgetWindow(R.Widget.ToSharedRef())))
    { Catalog.Remove(Id); R.Entry = nullptr; R.Widget.Reset(); R.Desc.Reset(); }
    return R;
}
TSharedRef<SWidget> CaptureTarget(const FResolved& R)
{
    if (R.Entry->Kind == TEXT("tab")) return StaticCastSharedPtr<SDockTab>(R.Widget)->GetContent();
    return R.Widget.ToSharedRef();
}
void Box(const TArray<FColor>& Src, int32 W, int32 H, int32 OutW, int32 OutH, TArray<FColor>& Dst)
{
    Dst.SetNumUninitialized(OutW * OutH);
    for (int32 Y = 0; Y < OutH; ++Y)
    {
        const int32 Y0 = (int32)((int64)Y * H / OutH), Y1 = FMath::Max(Y0 + 1, (int32)((int64)(Y + 1) * H / OutH));
        for (int32 X = 0; X < OutW; ++X)
        {
            const int32 X0 = (int32)((int64)X * W / OutW), X1 = FMath::Max(X0 + 1, (int32)((int64)(X + 1) * W / OutW));
            uint64 R = 0, G = 0, B = 0, N = 0;
            for (int32 SY = Y0; SY < Y1; ++SY) for (int32 SX = X0; SX < X1; ++SX)
            { const FColor& C = Src[SY * W + SX]; R += C.R; G += C.G; B += C.B; ++N; }
            Dst[Y * OutW + X] = FColor((uint8)(R / N), (uint8)(G / N), (uint8)(B / N), 255);
        }
    }
}

// ---------------------------------------------------------------- observers (V5-37 initial slice)
constexpr int32 MaxObserversPerClient = 2, MaxObserversGlobal = 8, MaxHistory = 3;
constexpr int64 MaxObserverBytesGlobal = 64 * 1024 * 1024;
constexpr double MinRateHz = 0.2, MaxRateHz = 2.0;
struct FStoredFrame
{
    FString FrameId, SurfaceId, Mime, PixelHash, CapturedAt;
    uint64 Sequence = 0; uint32 SurfaceGeneration = 0;
    int32 Width = 0, Height = 0, SourceWidth = 0, SourceHeight = 0;
    double Scale = 1.0, CaptureMs = 0.0;
    bool bRegion = false; FIntRect Region;
    TArray64<uint8> Bytes;
    FString AfterOperationId, Freshness = TEXT("fresh"); uint64 PaintSeq = 0; // v5 increment 23
};
struct FObserver
{
    FString Id, Principal, Session, SurfaceId, State = TEXT("active"), LastError, LastHash;
    FString Mode = TEXT("periodic"), AfterOperationId; uint64 LastPaintSeq = 0; bool bAfterCaptured = false; // v5 increment 23
    double RequestedRateHz = 1.0, ActualRateHz = 1.0, ExpiresAt = 0.0, NextSampleAt = 0.0, StartedAt = 0.0;
    int32 History = 1, Samples = 0, Unchanged = 0, Dropped = 0, Failures = 0;
    FEncodeOptions Encode;
    bool bRegion = false; FIntRect Region; // physical pixels relative to the surface origin
    TArray<FStoredFrame> Frames; // oldest first
};
TMap<FString, FObserver> Observers;
FTSTicker::FDelegateHandle TickerHandle;
int64 ObserverBytes = 0;

// v5 increment 23: retained captures, last-hash per surface for the "stable" wait policy.
FDelegateHandle PaintHandle;
struct FPrevPixels { int32 W = 0, H = 0; TArray<FColor> P; };
TMap<FString, FPrevPixels> LastCapturePixels; // v5 increment 24: tolerant "stable" policy (threshold 16, < 0.5% changed)
constexpr int64 MaxStablePixels = 2 * 1024 * 1024;
double SampleBudgetMs = 5.0; int64 SampleTicksOverBudget = 0, SamplesDeferred = 0;
struct FRetained { FStoredFrame Frame; FString Principal, Session; double ExpiresAt = 0.0; };
TMap<FString, FRetained> Retained;
constexpr int32 MaxRetainedPerSession = 8;
constexpr double RetainedTtlSeconds = 600.0;
void EnsurePaintHook()
{
    if (PaintHandle.IsValid() || !FSlateApplication::IsInitialized()) return;
    PaintHandle = FSlateApplication::Get().OnPostTick().AddLambda([](float) { ++PaintSequence; });
}
void SweepRetained(double Now)
{
    for (auto It = Retained.CreateIterator(); It; ++It) if (Now >= It.Value().ExpiresAt) { ObserverBytes -= It.Value().Frame.Bytes.Num(); It.RemoveCurrent(); }
}
const FStoredFrame* FindOwnedFrame(const FString& FrameId, const FMCPRequestContext& Context)
{
    for (const auto& Pair : Observers)
        if (Pair.Value.Principal == Context.PrincipalId && Pair.Value.Session == Context.SessionId)
            for (const auto& F : Pair.Value.Frames) if (F.FrameId == FrameId) return &F;
    if (const FRetained* R = Retained.Find(FrameId)) if (R->Principal == Context.PrincipalId && R->Session == Context.SessionId) return &R->Frame;
    return nullptr;
}
FString RetainFrame(FStoredFrame&& F, const FMCPRequestContext& Context, double Now)
{
    SweepRetained(Now);
    int32 Owned = 0; FString Oldest; double OldestAt = TNumericLimits<double>::Max();
    for (const auto& P : Retained) if (P.Value.Principal == Context.PrincipalId && P.Value.Session == Context.SessionId) { ++Owned; if (P.Value.ExpiresAt < OldestAt) { OldestAt = P.Value.ExpiresAt; Oldest = P.Key; } }
    if (Owned >= MaxRetainedPerSession && !Oldest.IsEmpty()) { ObserverBytes -= Retained[Oldest].Frame.Bytes.Num(); Retained.Remove(Oldest); }
    FRetained R; R.Principal = Context.PrincipalId; R.Session = Context.SessionId; R.ExpiresAt = Now + RetainedTtlSeconds;
    ObserverBytes += F.Bytes.Num();
    const FString Id = F.FrameId; R.Frame = MoveTemp(F);
    Retained.Add(Id, MoveTemp(R));
    return Id;
}
struct FReadiness { bool bKnown = false; FString State; double EndedAt = 0.0; uint64 PaintAtEnd = 0, PaintsSince = 0; bool bFinished = false, bReady = false; };
FReadiness ReadinessFor(const FString& OperationId, const FMCPRequestContext& Context)
{
    FReadiness R;
    R.bKnown = MCPScenarios::GetOperationMark(OperationId, Context, R.State, R.EndedAt, R.PaintAtEnd);
    if (!R.bKnown) return R;
    R.bFinished = R.EndedAt > 0.0;
    R.PaintsSince = R.bFinished && PaintSequence > R.PaintAtEnd ? PaintSequence - R.PaintAtEnd : 0;
    R.bReady = R.bFinished && R.PaintsSince > 0;
    return R;
}

int64 FramesBytes(const FObserver& O) { int64 N = 0; for (const auto& F : O.Frames) N += F.Bytes.Num(); return N; }
void DropFrames(FObserver& O) { ObserverBytes -= FramesBytes(O); O.Frames.Empty(); }
void EnsureTicker()
{
    if (TickerHandle.IsValid()) return;
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("MCPEditorObservers"), 0.0f, [](float) { TickObservers(FPlatformTime::Seconds()); return true; });
}
void ReleaseTicker()
{
    if (TickerHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle); TickerHandle.Reset(); }
}
TSharedPtr<FJsonObject> FrameMeta(const FStoredFrame& F)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("frame_id"), F.FrameId); Out->SetNumberField(TEXT("sequence"), (double)F.Sequence);
    Out->SetStringField(TEXT("surface_id"), F.SurfaceId); Out->SetNumberField(TEXT("surface_generation"), F.SurfaceGeneration);
    Out->SetStringField(TEXT("captured_at"), F.CapturedAt); Out->SetStringField(TEXT("mime"), F.Mime);
    Out->SetNumberField(TEXT("width"), F.Width); Out->SetNumberField(TEXT("height"), F.Height);
    auto Source = MakeShared<FJsonObject>(); Source->SetNumberField(TEXT("width"), F.SourceWidth); Source->SetNumberField(TEXT("height"), F.SourceHeight);
    Out->SetObjectField(TEXT("source"), Source);
    Out->SetNumberField(TEXT("scale"), F.Scale); Out->SetNumberField(TEXT("bytes"), (double)F.Bytes.Num());
    Out->SetStringField(TEXT("pixel_hash"), F.PixelHash); Out->SetNumberField(TEXT("capture_ms"), F.CaptureMs);
    Out->SetStringField(TEXT("freshness"), F.Freshness); Out->SetStringField(TEXT("backend"), TEXT("slate_widget"));
    Out->SetNumberField(TEXT("paint_sequence"), (double)F.PaintSeq);
    if (!F.AfterOperationId.IsEmpty()) Out->SetStringField(TEXT("after_operation_id"), F.AfterOperationId);
    if (F.bRegion) Out->SetObjectField(TEXT("region"), RectJson(F.Region.Min.X, F.Region.Min.Y, F.Region.Width(), F.Region.Height()));
    Out->SetStringField(TEXT("resource_uri"), TEXT("unreal://visual/frames/") + F.FrameId);
    return Out;
}
TSharedPtr<FJsonObject> ObserverJson(const FObserver& O, double Now)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("observer_id"), O.Id); Out->SetStringField(TEXT("surface_id"), O.SurfaceId);
    Out->SetStringField(TEXT("state"), O.State); Out->SetStringField(TEXT("mode"), O.Mode);
    if (!O.AfterOperationId.IsEmpty()) { Out->SetStringField(TEXT("after_operation_id"), O.AfterOperationId); Out->SetBoolField(TEXT("after_operation_captured"), O.bAfterCaptured); }
    Out->SetNumberField(TEXT("paint_sequence"), (double)PaintSequence);
    Out->SetNumberField(TEXT("requested_rate_hz"), O.RequestedRateHz); Out->SetNumberField(TEXT("actual_rate_hz"), O.ActualRateHz);
    Out->SetNumberField(TEXT("lease_remaining_seconds"), FMath::Max(0.0, O.ExpiresAt - Now));
    Out->SetNumberField(TEXT("history_limit"), O.History); Out->SetNumberField(TEXT("retained_frames"), O.Frames.Num());
    Out->SetNumberField(TEXT("retained_bytes"), (double)FramesBytes(O));
    Out->SetNumberField(TEXT("samples"), O.Samples); Out->SetNumberField(TEXT("unchanged"), O.Unchanged);
    Out->SetNumberField(TEXT("dropped"), O.Dropped); Out->SetNumberField(TEXT("failures"), O.Failures);
    Out->SetNumberField(TEXT("latest_sequence"), O.Frames.Num() ? (double)O.Frames.Last().Sequence : 0.0);
    Out->SetNumberField(TEXT("next_poll_ms"), O.State == TEXT("active") ? 1000.0 / O.ActualRateHz : 0.0);
    if (!O.LastError.IsEmpty()) Out->SetStringField(TEXT("last_error"), O.LastError);
    Out->SetStringField(TEXT("image_delivery"), TEXT("poll_inline_content"));
    if (O.bRegion) Out->SetObjectField(TEXT("region"), RectJson(O.Region.Min.X, O.Region.Min.Y, O.Region.Width(), O.Region.Height()));
    return Out;
}
/** Validate a caller region against the target's current draw size. Empty result = ok. */
FVector2D TargetSize(const FResolved& R)
{
    // Windows know their size before their first paint; other widgets need cached geometry.
    if (R.Entry->Kind == TEXT("window") || R.Entry->Kind == TEXT("modal") || R.Entry->Kind == TEXT("overlay"))
        return FVector2D(StaticCastSharedPtr<SWindow>(R.Widget)->GetSizeInScreen());
    return FVector2D(CaptureTarget(R)->GetTickSpaceGeometry().GetAbsoluteSize());
}
FString CheckRegion(const FResolved& R, const FIntRect& Region)
{
    const FVector2D Size = TargetSize(R);
    if (Region.Min.X < 0 || Region.Min.Y < 0 || Region.Width() < 1 || Region.Height() < 1) return TEXT("region must have positive size");
    if (Size.X < 1 || Size.Y < 1) return TEXT("surface has no painted geometry yet; retry after an editor tick");
    if (Region.Max.X > FMath::CeilToInt(Size.X) || Region.Max.Y > FMath::CeilToInt(Size.Y))
        return FString::Printf(TEXT("region exceeds the surface size %dx%d"), FMath::CeilToInt(Size.X), FMath::CeilToInt(Size.Y));
    return FString();
}
FIntRect ParseRegion(const TSharedPtr<FJsonObject>& Rg)
{
    const int32 X = (int32)Rg->GetNumberField(TEXT("x")), Y = (int32)Rg->GetNumberField(TEXT("y"));
    return FIntRect(X, Y, X + (int32)Rg->GetNumberField(TEXT("width")), Y + (int32)Rg->GetNumberField(TEXT("height")));
}
FObserver* FindOwnedObserver(const FString& Id, const FMCPRequestContext& Context)
{
    FObserver* O = Observers.Find(Id);
    return O && O->Principal == Context.PrincipalId && O->Session == Context.SessionId ? O : nullptr;
}
void Sample(FObserver& O, double Now)
{
    const double Started = FPlatformTime::Seconds();
    ++O.Samples; O.LastPaintSeq = PaintSequence;
    const FResolved R = Resolve(O.SurfaceId);
    if (!R.Entry) { O.State = TEXT("surface_invalidated"); O.LastError = TEXT("Surface closed or replaced"); DropFrames(O); return; }
    const auto Capture = R.Desc->GetObjectField(TEXT("capture"));
    if (!R.B.bAvailable) { ++O.Failures; O.LastError = TEXT("No visual backend: ") + R.B.Reason; return; }
    if (!Capture->GetBoolField(TEXT("supported"))) { ++O.Failures; O.LastError = TEXT("Surface not capturable: ") + Capture->GetStringField(TEXT("reason")); return; }
    if (O.bRegion) { const FString Bad = CheckRegion(R, O.Region); if (!Bad.IsEmpty()) { ++O.Failures; O.LastError = TEXT("Observer region invalid now: ") + Bad; return; } }
    TArray<FColor> Pixels; FIntVector Size(0);
    const bool bRead = O.bRegion ? FSlateApplication::Get().TakeScreenshot(CaptureTarget(R), O.Region, Pixels, Size) : FSlateApplication::Get().TakeScreenshot(CaptureTarget(R), Pixels, Size);
    if (!bRead || Size.X <= 0 || Size.Y <= 0)
    { ++O.Failures; O.LastError = TEXT("Slate readback produced no pixels"); return; }
    const FString Hash = FSHA1::HashBuffer(Pixels.GetData(), (int64)Size.X * Size.Y * sizeof(FColor)).ToString();
    if (Hash == O.LastHash) { ++O.Unchanged; O.LastError.Empty(); return; }
    FEncodedFrame Encoded;
    if (!EncodeFrame(Pixels, Size.X, Size.Y, O.Encode, Encoded)) { ++O.Failures; O.LastError = Encoded.Error; return; }
    while (O.Frames.Num() >= O.History) { ObserverBytes -= O.Frames[0].Bytes.Num(); O.Frames.RemoveAt(0); ++O.Dropped; }
    if (ObserverBytes + Encoded.Bytes.Num() > MaxObserverBytesGlobal)
    {
        DropFrames(O);
        if (ObserverBytes + Encoded.Bytes.Num() > MaxObserverBytesGlobal) { ++O.Dropped; O.LastError = TEXT("Global observer byte budget exhausted; frame dropped"); return; }
    }
    FStoredFrame F;
    F.FrameId = TEXT("frm-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    F.Sequence = ++FrameSequence; F.SurfaceId = O.SurfaceId; F.SurfaceGeneration = R.Entry->Generation;
    F.CapturedAt = FDateTime::UtcNow().ToIso8601(); F.Mime = Encoded.Mime; F.PixelHash = Hash;
    F.Width = Encoded.Width; F.Height = Encoded.Height; F.SourceWidth = Encoded.SourceWidth; F.SourceHeight = Encoded.SourceHeight;
    F.Scale = Encoded.Scale; F.CaptureMs = (FPlatformTime::Seconds() - Started) * 1000.0; F.Bytes = MoveTemp(Encoded.Bytes);
    F.bRegion = O.bRegion; F.Region = O.Region; F.PaintSeq = PaintSequence;
    ObserverBytes += F.Bytes.Num(); O.LastHash = Hash; O.LastError.Empty();
    // Adaptive throttle: a capture that costs more than half the sampling period halves the rate.
    if (F.CaptureMs > 500.0 / O.ActualRateHz && O.ActualRateHz > MinRateHz) O.ActualRateHz = FMath::Max(MinRateHz, O.ActualRateHz * 0.5);
    O.Frames.Add(MoveTemp(F));
}
FMCPToolResult FrameResult(const FString& Text, const FStoredFrame& F, const TSharedPtr<FJsonObject>& Structured)
{
    FMCPToolResult Result = FMCPToolResult::WithImage(Text, FBase64::Encode(F.Bytes.GetData(), (uint32)F.Bytes.Num()), F.Mime);
    Result.StructuredContent = Structured;
    return Result;
}
struct FVerification
{
    FString Id, Principal, Session, Outcome, Method, Criteria, Notes, SurfaceId, RecordedAt;
    TArray<FString> FrameIds; TArray<bool> Retained;
};
TArray<FVerification> Verifications;
constexpr int32 MaxVerificationsPerSession = 64, MaxVerificationsGlobal = 512;
bool IsRetainedFrame(const FString& FrameId, const FMCPRequestContext& Context)
{
    for (const auto& Pair : Observers)
        if (Pair.Value.Principal == Context.PrincipalId && Pair.Value.Session == Context.SessionId)
            for (const auto& F : Pair.Value.Frames) if (F.FrameId == FrameId) return true;
    return false;
}
TSharedPtr<FJsonObject> VerificationJson(const FVerification& V)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("verification_id"), V.Id); Out->SetStringField(TEXT("outcome"), V.Outcome);
    Out->SetStringField(TEXT("method"), V.Method); Out->SetStringField(TEXT("criteria"), V.Criteria);
    if (!V.Notes.IsEmpty()) Out->SetStringField(TEXT("notes"), V.Notes);
    if (!V.SurfaceId.IsEmpty()) Out->SetStringField(TEXT("surface_id"), V.SurfaceId);
    Out->SetStringField(TEXT("recorded_at"), V.RecordedAt);
    TArray<TSharedPtr<FJsonValue>> Frames;
    for (int32 I = 0; I < V.FrameIds.Num(); ++I)
    {
        auto F = MakeShared<FJsonObject>(); F->SetStringField(TEXT("frame_id"), V.FrameIds[I]); F->SetBoolField(TEXT("retained_at_record"), V.Retained[I]);
        Frames.Add(MakeShared<FJsonValueObject>(F));
    }
    Out->SetArrayField(TEXT("frames"), Frames);
    Out->SetStringField(TEXT("attribution"), V.Method == TEXT("deterministic") ? TEXT("validator") : TEXT("assistant_judgment"));
    return Out;
}
const TCHAR* VerificationSchema = TEXT(R"({"type":"object","required":["verification_id","outcome","method","criteria","frames","recorded_at","attribution"],"properties":{"verification_id":{"type":"string"},"outcome":{"type":"string"},"method":{"type":"string"},"criteria":{"type":"string"},"notes":{"type":"string"},"surface_id":{"type":"string"},"recorded_at":{"type":"string"},"frames":{"type":"array"},"attribution":{"type":"string"}}})");
const TCHAR* ObserverSchema = TEXT(R"({"type":"object","required":["observer_id","surface_id","state","actual_rate_hz","lease_remaining_seconds","retained_frames","samples","unchanged","dropped","failures","latest_sequence"],"properties":{"observer_id":{"type":"string"},"surface_id":{"type":"string"},"state":{"type":"string"},"mode":{"type":"string"},"requested_rate_hz":{"type":"number"},"actual_rate_hz":{"type":"number"},"lease_remaining_seconds":{"type":"number"},"history_limit":{"type":"integer"},"retained_frames":{"type":"integer"},"retained_bytes":{"type":"integer"},"samples":{"type":"integer"},"unchanged":{"type":"integer"},"dropped":{"type":"integer"},"failures":{"type":"integer"},"latest_sequence":{"type":"integer"},"next_poll_ms":{"type":"number"},"last_error":{"type":"string"},"image_delivery":{"type":"string"},"no_change":{"type":"boolean"},"frame":{"type":"object"},"history":{"type":"array"}}})");
const TCHAR* SurfaceSchema = TEXT(R"({"type":"object","required":["catalog_generation","visual_backend","surfaces","total_surfaces","matched","truncated"],"properties":{"catalog_generation":{"type":"integer"},"visual_backend":{"type":"object"},"surfaces":{"type":"array","items":{"type":"object"}},"total_surfaces":{"type":"integer"},"matched":{"type":"integer"},"offset":{"type":"integer"},"truncated":{"type":"boolean"},"walk_budget_exhausted":{"type":"boolean"},"captured_at":{"type":"string"}}})");
const TCHAR* FrameSchema = TEXT(R"({"type":"object","required":["frame_id","sequence","surface_id","surface_generation","captured_at","backend","freshness","width","height","mime","bytes","pixel_hash"],"properties":{"frame_id":{"type":"string"},"sequence":{"type":"integer"},"surface_id":{"type":"string"},"surface_generation":{"type":"integer"},"captured_at":{"type":"string"},"backend":{"type":"string"},"freshness":{"type":"string"},"readback":{"type":"string"},"width":{"type":"integer"},"height":{"type":"integer"},"mime":{"type":"string"},"bytes":{"type":"integer"},"pixel_hash":{"type":"string"},"source":{"type":"object"},"region":{"type":"object"},"scale":{"type":"number"},"dpi_scale":{"type":"number"},"capture_ms":{"type":"number"},"warnings":{"type":"array"},"image_delivery":{"type":"string"},"surface":{"type":"object"}}})");
const TCHAR* FocusSchema = TEXT(R"({"type":"object","required":["surface_id","effects","resolved"],"properties":{"surface_id":{"type":"string"},"effects":{"type":"array"},"resolved":{"type":"object"},"surfaces":{"type":"array"},"note":{"type":"string"}}})");
}

bool EncodeFrame(const TArray<FColor>& Pixels, int32 Width, int32 Height, const FEncodeOptions& Options, FEncodedFrame& Out)
{
    Out = FEncodedFrame();
    if (Width < 1 || Height < 1 || Width > 16384 || Height > 16384) { Out.Error = TEXT("Source dimensions must be 1..16384"); return false; }
    if ((int64)Pixels.Num() < (int64)Width * Height) { Out.Error = TEXT("Pixel buffer is smaller than the declared size"); return false; }
    if (Options.Format != TEXT("png") && Options.Format != TEXT("jpeg")) { Out.Error = TEXT("format must be png or jpeg"); return false; }
    if (Options.MaxLongEdge < 64 || Options.MaxLongEdge > 4096) { Out.Error = TEXT("max_long_edge must be 64..4096"); return false; }
    if (Options.JpegQuality < 1 || Options.JpegQuality > 100) { Out.Error = TEXT("jpeg_quality must be 1..100"); return false; }
    if (Options.MaxBytes < 16 * 1024 || Options.MaxBytes > 8 * 1024 * 1024) { Out.Error = TEXT("max_bytes must be 16384..8388608"); return false; }
    Out.SourceWidth = Width; Out.SourceHeight = Height;
    Out.PixelHash = FSHA1::HashBuffer(Pixels.GetData(), (int64)Width * Height * sizeof(FColor)).ToString();
    const int32 LongEdge = FMath::Max(Width, Height);
    Out.Scale = LongEdge > Options.MaxLongEdge ? (double)Options.MaxLongEdge / LongEdge : 1.0;
    Out.Width = FMath::Max(1, (int32)FMath::RoundToInt(Width * Out.Scale));
    Out.Height = FMath::Max(1, (int32)FMath::RoundToInt(Height * Out.Scale));
    TArray<FColor> Scaled;
    if (Out.Scale < 1.0) Box(Pixels, Width, Height, Out.Width, Out.Height, Scaled);
    else { Scaled.SetNumUninitialized(Width * Height); for (int32 I = 0; I < Width * Height; ++I) { Scaled[I] = Pixels[I]; Scaled[I].A = 255; } }
    IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    const bool bPng = Options.Format == TEXT("png");
    TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(bPng ? EImageFormat::PNG : EImageFormat::JPEG);
    if (!Wrapper.IsValid() || !Wrapper->SetRaw(Scaled.GetData(), Scaled.Num() * sizeof(FColor), Out.Width, Out.Height, ERGBFormat::BGRA, 8))
    { Out.Error = TEXT("Image encoder unavailable"); return false; }
    Out.Bytes = Wrapper->GetCompressed(bPng ? 0 : Options.JpegQuality);
    Out.Mime = bPng ? TEXT("image/png") : TEXT("image/jpeg");
    if (Out.Bytes.Num() == 0) { Out.Error = TEXT("Image compression failed"); return false; }
    if (Out.Bytes.Num() > Options.MaxBytes)
    {
        Out.Error = FString::Printf(TEXT("Encoded frame is %lld bytes, above max_bytes %lld; lower max_long_edge, use jpeg or capture a region"), (long long)Out.Bytes.Num(), (long long)Options.MaxBytes);
        Out.Bytes.Empty(); return false;
    }
    return true;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    EnsurePaintHook();
    const TArray<FString> Kinds = { TEXT("any"), TEXT("window"), TEXT("modal"), TEXT("overlay"), TEXT("tab"), TEXT("level_viewport") };
    FMCPToolBuilder(Registry, TEXT("list_editor_surfaces"))
        .Description(TEXT("Discover editor-owned windows, modal/overlay windows, dock tabs and level viewports with stable surface IDs, generation, geometry, visibility and honest capture availability. Never activates or reveals UI. Surface IDs are invalidated when their widget or window closes; generation advances when a descriptor changes. Headless/NullRHI processes report no visual backend."))
        .ReadOnly().Idempotent()
        .EnumArg(TEXT("kind"), TEXT("Filter by surface kind (default any)"), Kinds)
        .StringArg(TEXT("title_contains"), TEXT("Case-insensitive substring filter on title/label"))
        .IntArg(TEXT("offset"), TEXT("Skip this many matches (default 0)"))
        .IntArg(TEXT("limit"), TEXT("Maximum surfaces to return: 1..500, default 100"))
        .OutputSchema(SurfaceSchema)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext&)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Surface discovery requires the game thread"));
            const FString Kind = Args->HasField(TEXT("kind")) ? Args->GetStringField(TEXT("kind")) : TEXT("any");
            const FString Title = Args->HasField(TEXT("title_contains")) ? Args->GetStringField(TEXT("title_contains")) : FString();
            const int32 Offset = Args->HasField(TEXT("offset")) ? (int32)Args->GetNumberField(TEXT("offset")) : 0;
            const int32 Limit = Args->HasField(TEXT("limit")) ? (int32)Args->GetNumberField(TEXT("limit")) : 100;
            if (Offset < 0 || Limit < 1 || Limit > 500) return Error(EMCPError::OutOfRange, TEXT("offset must be >= 0 and limit 1..500"));
            const FScan S = Scan();
            const auto Out = ScanJson(S, Kind, Title, Offset, Limit);
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%d editor surfaces (%d matched); visual backend %s."), S.Descriptors.Num(),
                (int32)Out->GetNumberField(TEXT("matched")), S.B.bAvailable ? TEXT("available") : *FString::Printf(TEXT("unavailable: %s"), *S.B.Reason)), Out);
        });

    auto RegionSchema = StringToJson(TEXT(R"({"type":"object","additionalProperties":false,"required":["x","y","width","height"],"properties":{"x":{"type":"integer","minimum":0,"maximum":16384},"y":{"type":"integer","minimum":0,"maximum":16384},"width":{"type":"integer","minimum":1,"maximum":16384},"height":{"type":"integer","minimum":1,"maximum":16384}}})"));
    FMCPToolBuilder(Registry, TEXT("capture_editor_surface"))
        .Description(TEXT("Capture the actual pixels of one discovered editor surface (window, foreground tab content or level viewport) through a synchronous Slate render and readback, returning inline image content plus frame identity, geometry, pixel hash and freshness. Refuses hidden, minimized or background targets instead of returning another surface's pixels; refuses headless/NullRHI processes. Region is in physical pixels relative to the surface origin. Frames are not retained."))
        .ReadOnly()
        .StringArg(TEXT("surface_id"), TEXT("Surface ID from list_editor_surfaces"), true)
        .EnumArg(TEXT("format"), TEXT("png (default, text/graphs) or jpeg (viewports)"), { TEXT("png"), TEXT("jpeg") })
        .IntArg(TEXT("max_long_edge"), TEXT("Downscale so the long edge is at most this many pixels: 64..4096, default 1280; never upscales"))
        .IntArg(TEXT("jpeg_quality"), TEXT("1..100, default 85"))
        .IntArg(TEXT("max_bytes"), TEXT("Encoded byte cap: 16384..8388608, default 1048576; exceeding it fails explicitly"))
        .ObjectArg(TEXT("region"), TEXT("Optional crop in physical pixels relative to the surface origin"), RegionSchema)
        .StringArg(TEXT("after_operation_id"), TEXT("v5: owned operation the frame must follow; fresh only after the operation finished and Slate painted at least once since"))
        .EnumArg(TEXT("wait_policy"), TEXT("v5: none (default) | next_paint (require a paint after the operation) | stable (require the same pixels as the previous capture of this surface)"), { TEXT("none"), TEXT("next_paint"), TEXT("stable") })
        .BoolArg(TEXT("allow_stale"), TEXT("v5: return a frame marked stale/changing instead of not_ready (default false)"))
        .BoolArg(TEXT("retain"), TEXT("v5: keep the frame for 10 minutes as unreal://visual/frames/{frame_id} for comparison and re-reading (default false; 8 per session)"))
        .OutputSchema(FrameSchema)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Capture requires the game thread"));
            const double Started = FPlatformTime::Seconds();
            // v5 increment 23: readiness is decided before any pixels are read, and never by waiting on the game thread.
            const FString AfterOp = Args->HasField(TEXT("after_operation_id")) ? Args->GetStringField(TEXT("after_operation_id")) : FString();
            const FString WaitPolicy = Args->HasField(TEXT("wait_policy")) ? Args->GetStringField(TEXT("wait_policy")) : TEXT("none");
            const bool bAllowStale = Args->HasField(TEXT("allow_stale")) && Args->GetBoolField(TEXT("allow_stale"));
            const bool bRetain = Args->HasField(TEXT("retain")) && Args->GetBoolField(TEXT("retain"));
            FString Freshness = TEXT("fresh"); FReadiness Ready;
            auto NotReady = [&](const FString& Why, const FString& Hint)
            {
                FMCPToolResult E = Error(EMCPError::Timeout, TEXT("not_ready: ") + Why, Hint);
                E.StructuredContent->SetStringField(TEXT("readiness"), TEXT("not_ready")); E.StructuredContent->SetNumberField(TEXT("retry_after_ms"), 250);
                if (!AfterOp.IsEmpty()) { E.StructuredContent->SetStringField(TEXT("after_operation_id"), AfterOp); E.StructuredContent->SetStringField(TEXT("operation_state"), Ready.State); E.StructuredContent->SetNumberField(TEXT("paints_since_operation"), (double)Ready.PaintsSince); }
                return E;
            };
            if (!AfterOp.IsEmpty())
            {
                Ready = ReadinessFor(AfterOp, Context);
                if (!Ready.bKnown) return Error(EMCPError::NotFound, TEXT("Unknown, expired or foreign operation: ") + AfterOp);
                if (!Ready.bFinished) { if (!bAllowStale) return NotReady(TEXT("operation still ") + Ready.State, TEXT("Poll get_editor_operation, then capture again; or pass allow_stale=true for a frame marked stale.")); Freshness = TEXT("stale"); }
                else if (Ready.PaintsSince == 0 && WaitPolicy != TEXT("none")) { if (!bAllowStale) return NotReady(TEXT("no Slate paint since the operation ended"), TEXT("Retry after retry_after_ms; the editor has not painted the change yet.")); Freshness = TEXT("stale"); }
                else if (Ready.PaintsSince == 0) Freshness = TEXT("unknown");
            }
            FEncodeOptions Options;
            if (Args->HasField(TEXT("format"))) Options.Format = Args->GetStringField(TEXT("format"));
            if (Args->HasField(TEXT("max_long_edge"))) Options.MaxLongEdge = (int32)Args->GetNumberField(TEXT("max_long_edge"));
            if (Args->HasField(TEXT("jpeg_quality"))) Options.JpegQuality = (int32)Args->GetNumberField(TEXT("jpeg_quality"));
            if (Args->HasField(TEXT("max_bytes"))) Options.MaxBytes = (int64)Args->GetNumberField(TEXT("max_bytes"));
            if (Options.MaxLongEdge < 64 || Options.MaxLongEdge > 4096 || Options.JpegQuality < 1 || Options.JpegQuality > 100 || Options.MaxBytes < 16 * 1024 || Options.MaxBytes > 8 * 1024 * 1024)
                return Error(EMCPError::OutOfRange, TEXT("max_long_edge 64..4096, jpeg_quality 1..100, max_bytes 16384..8388608"));
            const FString Id = Args->GetStringField(TEXT("surface_id"));
            const FResolved R = Resolve(Id);
            if (!R.Entry) return Error(EMCPError::NotFound, TEXT("Unknown or invalidated surface; call list_editor_surfaces again"));
            const auto Capture = R.Desc->GetObjectField(TEXT("capture"));
            if (!R.B.bAvailable) return Error(EMCPError::Unsupported, TEXT("No visual backend: ") + R.B.Reason, TEXT("Structural discovery still works; no image can be produced in this process."));
            if (!Capture->GetBoolField(TEXT("supported"))) return Error(EMCPError::Unsupported, TEXT("Surface cannot be captured now: ") + Capture->GetStringField(TEXT("reason")), TEXT("Use focus_editor_surface (Scene scope) to reveal it, then capture again."));
            if (Context.IsCancelled()) return Error(EMCPError::Internal, TEXT("Cancelled before capture"));
            const TSharedRef<SWidget> Target = CaptureTarget(R);
            FIntRect Region; bool bRegion = false;
            if (Args->HasField(TEXT("region")))
            {
                Region = ParseRegion(Args->GetObjectField(TEXT("region"))); bRegion = true;
                const FString Bad = CheckRegion(R, Region);
                if (!Bad.IsEmpty()) return Error(EMCPError::OutOfRange, Bad);
            }
            TArray<FColor> Pixels; FIntVector Size(0);
            FSlateApplication& App = FSlateApplication::Get();
            const bool bRead = bRegion ? App.TakeScreenshot(Target, Region, Pixels, Size) : App.TakeScreenshot(Target, Pixels, Size);
            if (!bRead || Size.X <= 0 || Size.Y <= 0) return Error(EMCPError::Internal, TEXT("Slate readback failed; the surface produced no pixels"), TEXT("The window may be unpainted or torn down. Re-list surfaces and retry."));
            FEncodedFrame Frame;
            if (!EncodeFrame(Pixels, Size.X, Size.Y, Options, Frame)) return Error(EMCPError::OutOfRange, Frame.Error);
            double StableChangedPct = -1.0;
            if (WaitPolicy == TEXT("stable"))
            {
                const FPrevPixels* Prev = LastCapturePixels.Find(Id);
                const bool bComparable = Prev && Prev->W == Size.X && Prev->H == Size.Y && Prev->P.Num() == Pixels.Num();
                if (bComparable)
                {
                    int64 ChangedCount = 0;
                    for (int32 i = 0; i < Pixels.Num(); ++i)
                    {
                        const FColor& A = Prev->P[i]; const FColor& B = Pixels[i];
                        if (FMath::Max3(FMath::Abs((int32)A.R - B.R), FMath::Abs((int32)A.G - B.G), FMath::Abs((int32)A.B - B.B)) > 16) ++ChangedCount;
                    }
                    StableChangedPct = 100.0 * ChangedCount / FMath::Max(1, Pixels.Num());
                }
                if ((int64)Pixels.Num() <= MaxStablePixels) { FPrevPixels& Store = LastCapturePixels.FindOrAdd(Id); Store.W = Size.X; Store.H = Size.Y; Store.P = Pixels; }
                else LastCapturePixels.Remove(Id);
                if (!bComparable || StableChangedPct >= 0.5)
                {
                    if (!bAllowStale) return NotReady(bComparable ? FString::Printf(TEXT("%.2f%% of pixels changed since the previous capture of this surface"), StableChangedPct) : TEXT("no comparable previous capture of this surface"), TEXT("Capture again; stable is reached when two consecutive captures differ in less than 0.5% of pixels (threshold 16)."));
                    Freshness = TEXT("changing");
                }
                else if (Freshness == TEXT("fresh")) Freshness = TEXT("unchanged");
            }
            const FString FrameId = TEXT("frm-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
            auto Out = MakeShared<FJsonObject>();
            Out->SetStringField(TEXT("frame_id"), FrameId);
            Out->SetStringField(TEXT("wait_policy"), WaitPolicy); Out->SetNumberField(TEXT("paint_sequence"), (double)PaintSequence);
            if (StableChangedPct >= 0.0) Out->SetNumberField(TEXT("stable_changed_pct"), StableChangedPct);
            if (!AfterOp.IsEmpty()) { Out->SetStringField(TEXT("after_operation_id"), AfterOp); Out->SetStringField(TEXT("operation_state"), Ready.State); Out->SetNumberField(TEXT("paints_since_operation"), (double)Ready.PaintsSince); }
            if (bRetain)
            {
                FStoredFrame SF; SF.FrameId = FrameId; SF.SurfaceId = Id; SF.SurfaceGeneration = R.Entry->Generation; SF.CapturedAt = FDateTime::UtcNow().ToIso8601();
                SF.Mime = Frame.Mime; SF.PixelHash = Frame.PixelHash; SF.Width = Frame.Width; SF.Height = Frame.Height; SF.SourceWidth = Frame.SourceWidth; SF.SourceHeight = Frame.SourceHeight;
                SF.Scale = Frame.Scale; SF.bRegion = bRegion; SF.Region = Region; SF.Bytes = Frame.Bytes; SF.AfterOperationId = AfterOp; SF.Freshness = Freshness; SF.PaintSeq = PaintSequence; SF.Sequence = FrameSequence + 1;
                RetainFrame(MoveTemp(SF), Context, FPlatformTime::Seconds());
                Out->SetBoolField(TEXT("retained"), true); Out->SetStringField(TEXT("resource_uri"), TEXT("unreal://visual/frames/") + FrameId);
            }
            Out->SetNumberField(TEXT("sequence"), (double)++FrameSequence);
            Out->SetStringField(TEXT("surface_id"), Id);
            Out->SetNumberField(TEXT("surface_generation"), R.Entry->Generation);
            Out->SetStringField(TEXT("kind"), R.Entry->Kind);
            Out->SetStringField(TEXT("captured_at"), FDateTime::UtcNow().ToIso8601());
            Out->SetStringField(TEXT("backend"), R.B.Name);
            Out->SetStringField(TEXT("freshness"), Freshness);
            Out->SetStringField(TEXT("readback"), TEXT("synchronous_slate_render"));
            Out->SetNumberField(TEXT("width"), Frame.Width); Out->SetNumberField(TEXT("height"), Frame.Height);
            Out->SetStringField(TEXT("mime"), Frame.Mime); Out->SetNumberField(TEXT("bytes"), (double)Frame.Bytes.Num());
            Out->SetStringField(TEXT("pixel_hash"), Frame.PixelHash);
            auto Source = MakeShared<FJsonObject>(); Source->SetNumberField(TEXT("width"), Frame.SourceWidth); Source->SetNumberField(TEXT("height"), Frame.SourceHeight);
            Out->SetObjectField(TEXT("source"), Source);
            Out->SetObjectField(TEXT("region"), bRegion ? RectJson(Region.Min.X, Region.Min.Y, Region.Width(), Region.Height()) : RectJson(0, 0, Frame.SourceWidth, Frame.SourceHeight));
            Out->SetNumberField(TEXT("scale"), Frame.Scale);
            Out->SetNumberField(TEXT("dpi_scale"), R.Desc->GetNumberField(TEXT("dpi_scale")));
            Out->SetNumberField(TEXT("capture_ms"), (FPlatformTime::Seconds() - Started) * 1000.0);
            TArray<TSharedPtr<FJsonValue>> Warnings;
            if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) Warnings.Add(MakeShared<FJsonValueString>(TEXT("shaders_compiling: rendered content may still change")));
            if (!R.Desc->GetBoolField(TEXT("active"))) Warnings.Add(MakeShared<FJsonValueString>(TEXT("surface_not_active: OS-level occlusion cannot be detected")));
            if (GEditor && GEditor->IsPlaySessionInProgress()) Warnings.Add(MakeShared<FJsonValueString>(TEXT("pie_active: frame is a single temporal sample")));
            Out->SetArrayField(TEXT("warnings"), Warnings);
            Out->SetStringField(TEXT("image_delivery"), TEXT("inline_content"));
            Out->SetObjectField(TEXT("surface"), R.Desc);
            FMCPToolResult Result = FMCPToolResult::WithImage(FString::Printf(TEXT("Captured %s '%s' (%dx%d %s, %lld bytes, hash %s)."), *R.Entry->Kind, *R.Desc->GetStringField(TEXT("title")),
                Frame.Width, Frame.Height, *Frame.Mime, (long long)Frame.Bytes.Num(), *Frame.PixelHash.Left(12)), FBase64::Encode(Frame.Bytes.GetData(), (uint32)Frame.Bytes.Num()), Frame.Mime);
            Result.StructuredContent = Out;
            return Result;
        });

    FMCPToolBuilder(Registry, TEXT("focus_editor_surface"))
        .Description(TEXT("Explicitly reveal a surface. Pass surface_id to activate a background tab, restore a minimized window and bring its window to the front; or pass asset_path to open (or focus) that asset's editor and return the tabs it owns. This is a UI effect requiring Scene scope; it never closes, saves or edits content. Refuses when expected_generation no longer matches. Verify with list_editor_surfaces or capture_editor_surface afterwards."))
        .Idempotent()
        .StringArg(TEXT("surface_id"), TEXT("Surface ID from list_editor_surfaces (or use asset_path)"))
        .StringArg(TEXT("asset_path"), TEXT("Loaded or loadable asset path, e.g. /Game/UI/WBP_HUD; opens its asset editor"))
        .IntArg(TEXT("expected_generation"), TEXT("Refuse if the surface generation changed since discovery"))
        .BoolArg(TEXT("restore_minimized"), TEXT("Restore a minimized window (default true)"))
        .OutputSchema(FocusSchema)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext&)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Focus requires the game thread"));
            if (!Args->HasField(TEXT("surface_id")) && Args->HasField(TEXT("asset_path")))
            {
                FString Path = Args->GetStringField(TEXT("asset_path"));
                if (!Path.Contains(TEXT("."))) Path = FString::Printf(TEXT("%s.%s"), *Path, *FPackageName::GetShortName(Path));
                if (!FPackageName::IsValidObjectPath(Path) || !Path.StartsWith(TEXT("/"))) return Error(EMCPError::InvalidPath, TEXT("asset_path must be an object path such as /Game/UI/WBP_HUD"));
                UAssetEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
                UObject* Asset = LoadObject<UObject>(nullptr, *Path);
                if (!Sub || !Asset) return Error(EMCPError::NotFound, TEXT("Asset not found or not loadable: ") + Path);
                const bool bWasOpen = Sub->FindEditorsForAsset(Asset).Num() > 0;
                if (!Sub->OpenEditorForAsset(Asset)) return Error(EMCPError::Unsupported, TEXT("No asset editor could be opened for ") + Path);
                const FScan S = Scan();
                TArray<TSharedPtr<FJsonValue>> Tabs; TSharedPtr<FJsonObject> Owner;
                for (const auto& D : S.Descriptors)
                {
                    const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
                    if (!D->TryGetArrayField(TEXT("asset_paths"), Assets)) continue;
                    bool bMatch = false; for (const auto& V : *Assets) if (V->AsString() == Asset->GetPathName()) bMatch = true;
                    if (!bMatch) continue;
                    Tabs.Add(MakeShared<FJsonValueObject>(D));
                    if (!Owner.IsValid() || D->GetBoolField(TEXT("owner_tab"))) Owner = D;
                }
                auto Out = MakeShared<FJsonObject>();
                Out->SetStringField(TEXT("surface_id"), Owner.IsValid() ? Owner->GetStringField(TEXT("surface_id")) : FString());
                TArray<TSharedPtr<FJsonValue>> Effects; Effects.Add(MakeShared<FJsonValueString>(bWasOpen ? TEXT("asset_editor_focused") : TEXT("asset_editor_opened")));
                Out->SetArrayField(TEXT("effects"), Effects);
                Out->SetObjectField(TEXT("resolved"), Owner.IsValid() ? Owner : MakeShared<FJsonObject>());
                Out->SetArrayField(TEXT("surfaces"), Tabs);
                Out->SetStringField(TEXT("note"), Tabs.Num() ? TEXT("Asset editor tabs listed; the editor may finish laying out on the next tick.") : TEXT("Asset editor opened but no tab reported this asset yet; call list_editor_surfaces again after a tick."));
                return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%s; %d owned tab(s) discovered."), bWasOpen ? TEXT("Asset editor already open and focused") : TEXT("Asset editor opened"), Tabs.Num()), Out);
            }
            if (!Args->HasField(TEXT("surface_id"))) return Error(EMCPError::OutOfRange, TEXT("Provide surface_id or asset_path"));
            const FString Id = Args->GetStringField(TEXT("surface_id"));
            FResolved R = Resolve(Id);
            if (!R.Entry) return Error(EMCPError::NotFound, TEXT("Unknown or invalidated surface; call list_editor_surfaces again"));
            if (Args->HasField(TEXT("expected_generation")) && (uint32)Args->GetNumberField(TEXT("expected_generation")) != R.Entry->Generation)
                return Error(EMCPError::Unsupported, FString::Printf(TEXT("Surface generation is %u, not the expected value; re-list before revealing"), R.Entry->Generation));
            const bool bRestore = !Args->HasField(TEXT("restore_minimized")) || Args->GetBoolField(TEXT("restore_minimized"));
            TArray<TSharedPtr<FJsonValue>> Effects;
            TSharedPtr<SWindow> Window;
            if (R.Entry->Kind == TEXT("tab"))
            {
                const TSharedPtr<SDockTab> Tab = StaticCastSharedPtr<SDockTab>(R.Widget);
                Window = Tab->GetParentWindow();
                if (!Tab->IsForeground()) { Tab->ActivateInParent(ETabActivationCause::SetDirectly); Effects.Add(MakeShared<FJsonValueString>(TEXT("tab_activated"))); }
            }
            else Window = FSlateApplication::Get().FindWidgetWindow(R.Widget.ToSharedRef());
            if (Window.IsValid())
            {
                if (Window->IsWindowMinimized() && bRestore) { Window->Restore(); Effects.Add(MakeShared<FJsonValueString>(TEXT("window_restored"))); }
                if (!Window->IsActive()) { Window->BringToFront(true); Effects.Add(MakeShared<FJsonValueString>(TEXT("window_brought_to_front"))); }
            }
            const FResolved After = Resolve(Id);
            auto Out = MakeShared<FJsonObject>();
            Out->SetStringField(TEXT("surface_id"), Id);
            Out->SetArrayField(TEXT("effects"), Effects);
            Out->SetObjectField(TEXT("resolved"), After.Desc.IsValid() ? After.Desc : MakeShared<FJsonObject>());
            Out->SetStringField(TEXT("note"), TEXT("UI activation may complete on the next editor tick; verify with list_editor_surfaces or capture_editor_surface."));
            return FMCPToolResult::SuccessStructured(Effects.Num() ? TEXT("Reveal requested; no content was edited or saved.") : TEXT("Surface already revealed; no UI effect required."), Out);
        });

    FMCPToolBuilder(Registry, TEXT("start_editor_observation"))
        .Description(TEXT("Start an owned, leased periodic observer of one discovered surface. The server samples on the editor tick at the negotiated rate (0.2..2 Hz), skips encoding when pixels are unchanged, retains at most `history` frames under a global byte budget, and adapts the rate when capture is slow. Poll with get_editor_observation; clients never receive unsolicited frames. Requires Read scope only, plus observation-resource limits: 2 observers per session, 8 per editor."))
        .ReadOnly()
        .StringArg(TEXT("surface_id"), TEXT("Surface ID from list_editor_surfaces"), true)
        .NumberArg(TEXT("rate_hz"), TEXT("Requested sampling rate 0.2..2 (default 1)"))
        .IntArg(TEXT("lease_seconds"), TEXT("Lease 30..600 seconds (default 300); expiry releases all frames"))
        .IntArg(TEXT("history"), TEXT("Retained frames 1..3 (default 1)"))
        .EnumArg(TEXT("format"), TEXT("png (default) or jpeg"), { TEXT("png"), TEXT("jpeg") })
        .IntArg(TEXT("max_long_edge"), TEXT("64..4096, default 1280"))
        .IntArg(TEXT("max_bytes"), TEXT("Per-frame encoded cap 16384..8388608, default 1048576"))
        .ObjectArg(TEXT("region"), TEXT("Optional crop in physical pixels relative to the surface origin; keeps busy editor chrome out of change detection"), RegionSchema)
        .EnumArg(TEXT("mode"), TEXT("v5: periodic (default: sample at rate, skip identical pixels) | on_change (sample only when Slate painted since the last sample, coalesced to the rate) | after_operation (wait for after_operation_id to finish and paint, capture once tagged with the operation, then continue periodically)"), { TEXT("periodic"), TEXT("on_change"), TEXT("after_operation") })
        .StringArg(TEXT("after_operation_id"), TEXT("v5: owned operation for mode=after_operation"))
        .OutputSchema(ObserverSchema)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Observers require the game thread"));
            const double Now = FPlatformTime::Seconds();
            FObserver O;
            O.RequestedRateHz = Args->HasField(TEXT("rate_hz")) ? Args->GetNumberField(TEXT("rate_hz")) : 1.0;
            const double Lease = Args->HasField(TEXT("lease_seconds")) ? Args->GetNumberField(TEXT("lease_seconds")) : 300.0;
            O.History = Args->HasField(TEXT("history")) ? (int32)Args->GetNumberField(TEXT("history")) : 1;
            if (Args->HasField(TEXT("format"))) O.Encode.Format = Args->GetStringField(TEXT("format"));
            if (Args->HasField(TEXT("max_long_edge"))) O.Encode.MaxLongEdge = (int32)Args->GetNumberField(TEXT("max_long_edge"));
            if (Args->HasField(TEXT("max_bytes"))) O.Encode.MaxBytes = (int64)Args->GetNumberField(TEXT("max_bytes"));
            if (O.RequestedRateHz < MinRateHz || O.RequestedRateHz > MaxRateHz || Lease < 30 || Lease > 600 || O.History < 1 || O.History > MaxHistory
                || O.Encode.MaxLongEdge < 64 || O.Encode.MaxLongEdge > 4096 || O.Encode.MaxBytes < 16 * 1024 || O.Encode.MaxBytes > 8 * 1024 * 1024)
                return Error(EMCPError::OutOfRange, TEXT("rate_hz 0.2..2, lease_seconds 30..600, history 1..3, max_long_edge 64..4096, max_bytes 16384..8388608"));
            TickObservers(Now);
            int32 Owned = 0, Active = 0;
            for (const auto& Pair : Observers)
            {
                if (Pair.Value.State != TEXT("active")) continue;
                ++Active;
                if (Pair.Value.Principal == Context.PrincipalId && Pair.Value.Session == Context.SessionId) ++Owned;
            }
            if (Owned >= MaxObserversPerClient) return Error(EMCPError::Unsupported, TEXT("This session already owns the maximum of 2 active observers; stop one first"));
            if (Active >= MaxObserversGlobal) return Error(EMCPError::Unsupported, TEXT("Editor-wide observer limit reached"));
            const FResolved R = Resolve(Args->GetStringField(TEXT("surface_id")));
            if (!R.Entry) return Error(EMCPError::NotFound, TEXT("Unknown or invalidated surface; call list_editor_surfaces again"));
            if (Args->HasField(TEXT("region")))
            {
                O.bRegion = true; O.Region = ParseRegion(Args->GetObjectField(TEXT("region")));
                const FString Bad = CheckRegion(R, O.Region);
                if (!Bad.IsEmpty()) return Error(EMCPError::OutOfRange, Bad);
            }
            O.Mode = Args->HasField(TEXT("mode")) ? Args->GetStringField(TEXT("mode")) : TEXT("periodic");
            if (Args->HasField(TEXT("after_operation_id"))) O.AfterOperationId = Args->GetStringField(TEXT("after_operation_id"));
            if (O.Mode == TEXT("after_operation"))
            {
                if (O.AfterOperationId.IsEmpty()) return Error(EMCPError::OutOfRange, TEXT("mode=after_operation requires after_operation_id"));
                FString St; double Ended; uint64 PaintAtEnd;
                if (!MCPScenarios::GetOperationMark(O.AfterOperationId, Context, St, Ended, PaintAtEnd)) return Error(EMCPError::NotFound, TEXT("Unknown, expired or foreign operation: ") + O.AfterOperationId);
            }
            EnsurePaintHook();
            O.Id = TEXT("obs-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
            O.Principal = Context.PrincipalId; O.Session = Context.SessionId; O.SurfaceId = Args->GetStringField(TEXT("surface_id"));
            O.ActualRateHz = O.RequestedRateHz; O.StartedAt = Now; O.ExpiresAt = Now + Lease; O.NextSampleAt = Now;
            const FString Id = O.Id;
            Observers.Add(Id, MoveTemp(O));
            EnsureTicker();
            FObserver& Stored = Observers[Id];
            if (Stored.Mode != TEXT("after_operation")) Sample(Stored, Now);
            Stored.NextSampleAt = Now + 1.0 / Stored.ActualRateHz;
            const auto Out = ObserverJson(Stored, Now);
            if (Stored.Frames.Num())
            {
                Out->SetObjectField(TEXT("frame"), FrameMeta(Stored.Frames.Last()));
                return FrameResult(TEXT("Observer started; initial frame attached."), Stored.Frames.Last(), Out);
            }
            return FMCPToolResult::SuccessStructured(Stored.LastError.IsEmpty() ? TEXT("Observer started; no frame yet.") : TEXT("Observer started without an initial frame: ") + Stored.LastError, Out);
        });

    FMCPToolBuilder(Registry, TEXT("get_editor_observation"))
        .Description(TEXT("Poll an owned observer. Returns the newest retained frame inline when its sequence is above after_sequence, otherwise no_change with counters (samples, unchanged, dropped, failures) and the last error. Never blocks the editor; use next_poll_ms as the polling hint. Expired or invalidated observers report their terminal state and hold no frames."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("observer_id"), TEXT("Observer ID from start_editor_observation"), true)
        .IntArg(TEXT("after_sequence"), TEXT("Return a frame only if newer than this sequence (default 0)"))
        .BoolArg(TEXT("include_history"), TEXT("Include metadata for all retained frames (default false)"))
        .IntArg(TEXT("wait_ms"), TEXT("v5: bounded wait budget 0..30000 the client is willing to spend; the server never blocks the editor and answers with retry_after_ms = min(wait_ms, next_poll_ms) when no newer frame exists"))
        .OutputSchema(ObserverSchema)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Observers require the game thread"));
            const double Now = FPlatformTime::Seconds();
            TickObservers(Now);
            FObserver* O = FindOwnedObserver(Args->GetStringField(TEXT("observer_id")), Context);
            if (!O) return Error(EMCPError::NotFound, TEXT("Unknown, expired or foreign observer"));
            const double After = Args->HasField(TEXT("after_sequence")) ? Args->GetNumberField(TEXT("after_sequence")) : 0.0;
            const auto Out = ObserverJson(*O, Now);
            if (Args->HasField(TEXT("include_history")) && Args->GetBoolField(TEXT("include_history")))
            {
                TArray<TSharedPtr<FJsonValue>> H; for (const auto& F : O->Frames) H.Add(MakeShared<FJsonValueObject>(FrameMeta(F)));
                Out->SetArrayField(TEXT("history"), H);
            }
            if (O->Frames.Num() && (double)O->Frames.Last().Sequence > After)
            {
                Out->SetBoolField(TEXT("no_change"), false);
                Out->SetObjectField(TEXT("frame"), FrameMeta(O->Frames.Last()));
                return FrameResult(FString::Printf(TEXT("Observer %s: frame sequence %llu."), *O->State, (unsigned long long)O->Frames.Last().Sequence), O->Frames.Last(), Out);
            }
            Out->SetBoolField(TEXT("no_change"), true);
            const double WaitMs = Args->HasField(TEXT("wait_ms")) ? FMath::Clamp(Args->GetNumberField(TEXT("wait_ms")), 0.0, 30000.0) : 0.0;
            Out->SetNumberField(TEXT("retry_after_ms"), WaitMs > 0 ? FMath::Min(WaitMs, Out->GetNumberField(TEXT("next_poll_ms"))) : Out->GetNumberField(TEXT("next_poll_ms")));
            return FMCPToolResult::SuccessStructured(O->LastError.IsEmpty() ? FString::Printf(TEXT("Observer %s: no newer frame."), *O->State) : FString::Printf(TEXT("Observer %s: no frame; %s"), *O->State, *O->LastError), Out);
        });

    FMCPToolBuilder(Registry, TEXT("get_editor_frame"))
        .Description(TEXT("Return one retained observer frame by frame_id for the owning session, inline. Frames disappear when evicted by history limits, lease expiry, stop or session cleanup."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("frame_id"), TEXT("Frame ID from an observation result"), true)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Observers require the game thread"));
            TickObservers(FPlatformTime::Seconds());
            const FString Id = Args->GetStringField(TEXT("frame_id"));
            SweepRetained(FPlatformTime::Seconds());
            if (const FStoredFrame* F = FindOwnedFrame(Id, Context)) return FrameResult(TEXT("Retained frame."), *F, FrameMeta(*F));
            return Error(EMCPError::NotFound, TEXT("Unknown, evicted or foreign frame"));
        });

    FMCPToolBuilder(Registry, TEXT("stop_editor_observation"))
        .Description(TEXT("Stop an owned observer and release its frames and ticker work. Idempotent: stopping an already released observer succeeds with state unknown. Returns the final counters and last frame metadata."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("observer_id"), TEXT("Observer ID"), true)
        .OutputSchema(TEXT(R"({"type":"object","required":["observer_id","state","released"],"properties":{"observer_id":{"type":"string"},"state":{"type":"string"},"released":{"type":"boolean"},"final":{"type":"object"},"last_frame":{"type":"object"}}})"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Observers require the game thread"));
            const double Now = FPlatformTime::Seconds();
            const FString Id = Args->GetStringField(TEXT("observer_id"));
            auto Out = MakeShared<FJsonObject>(); Out->SetStringField(TEXT("observer_id"), Id);
            FObserver* Existing = Observers.Find(Id);
            if (Existing && (Existing->Principal != Context.PrincipalId || Existing->Session != Context.SessionId))
                return Error(EMCPError::NotFound, TEXT("Unknown or foreign observer"));
            if (!Existing)
            {
                Out->SetStringField(TEXT("state"), TEXT("unknown")); Out->SetBoolField(TEXT("released"), false);
                return FMCPToolResult::SuccessStructured(TEXT("Observer already released."), Out);
            }
            Existing->State = TEXT("stopped");
            Out->SetObjectField(TEXT("final"), ObserverJson(*Existing, Now));
            if (Existing->Frames.Num()) Out->SetObjectField(TEXT("last_frame"), FrameMeta(Existing->Frames.Last()));
            DropFrames(*Existing);
            Observers.Remove(Id);
            if (Observers.IsEmpty()) ReleaseTicker();
            Out->SetStringField(TEXT("state"), TEXT("stopped")); Out->SetBoolField(TEXT("released"), true);
            return FMCPToolResult::SuccessStructured(TEXT("Observer stopped and frames released."), Out);
        });
    FMCPToolBuilder(Registry, TEXT("record_visual_verification"))
        .Description(TEXT("Record the outcome of a visual check separately from capture success. outcome is pass, fail or inconclusive; method is assistant (a vision judgment) or deterministic (a validator/assertion). Reference the frame IDs inspected; the record notes whether each frame was still retained. Records are owned by the session, bounded (64 per session), and never imply editor content changed. Use inconclusive when frames were stale, blocked or unreadable."))
        .ReadOnly()
        .EnumArg(TEXT("outcome"), TEXT("pass | fail | inconclusive"), { TEXT("pass"), TEXT("fail"), TEXT("inconclusive") }, true)
        .EnumArg(TEXT("method"), TEXT("assistant | deterministic"), { TEXT("assistant"), TEXT("deterministic") }, true)
        .StringArg(TEXT("criteria"), TEXT("What was checked, e.g. 'no text overlaps the ammo panel'"), true)
        .StringArrayArg(TEXT("frame_ids"), TEXT("1..8 frame IDs that were inspected"), true)
        .StringArg(TEXT("surface_id"), TEXT("Surface the frames came from"))
        .StringArg(TEXT("notes"), TEXT("Free-form evidence notes, at most 2000 characters"))
        .OutputSchema(VerificationSchema)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Verification records require the game thread"));
            TArray<FString> FrameIds;
            for (const auto& V : Args->GetArrayField(TEXT("frame_ids"))) if (V->Type == EJson::String && !V->AsString().IsEmpty()) FrameIds.Add(V->AsString());
            if (FrameIds.Num() < 1 || FrameIds.Num() > 8) return Error(EMCPError::OutOfRange, TEXT("frame_ids must contain 1..8 non-empty IDs"));
            const FString Criteria = Args->GetStringField(TEXT("criteria"));
            const FString Notes = Args->HasField(TEXT("notes")) ? Args->GetStringField(TEXT("notes")) : FString();
            if (Criteria.IsEmpty() || Criteria.Len() > 500 || Notes.Len() > 2000) return Error(EMCPError::OutOfRange, TEXT("criteria must be 1..500 characters and notes at most 2000"));
            int32 Owned = 0;
            for (const auto& V : Verifications) if (V.Principal == Context.PrincipalId && V.Session == Context.SessionId) ++Owned;
            if (Owned >= MaxVerificationsPerSession || Verifications.Num() >= MaxVerificationsGlobal) return Error(EMCPError::Unsupported, TEXT("Verification record capacity reached for this session or editor"));
            FVerification V;
            V.Id = TEXT("vrf-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
            V.Principal = Context.PrincipalId; V.Session = Context.SessionId;
            V.Outcome = Args->GetStringField(TEXT("outcome")); V.Method = Args->GetStringField(TEXT("method"));
            V.Criteria = Criteria; V.Notes = Notes; V.RecordedAt = FDateTime::UtcNow().ToIso8601();
            if (Args->HasField(TEXT("surface_id"))) V.SurfaceId = Args->GetStringField(TEXT("surface_id"));
            for (const FString& Id : FrameIds) { V.FrameIds.Add(Id); V.Retained.Add(IsRetainedFrame(Id, Context)); }
            const auto Out = VerificationJson(V);
            Verifications.Add(MoveTemp(V));
            return FMCPToolResult::SuccessStructured(TEXT("Visual verification recorded; this is a judgment record, not a change to editor content."), Out);
        });

    FMCPToolBuilder(Registry, TEXT("list_visual_verifications"))
        .Description(TEXT("List this session's visual verification records, newest first, bounded by limit (1..64, default 20)."))
        .ReadOnly().Idempotent()
        .IntArg(TEXT("limit"), TEXT("1..64, default 20"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Verification records require the game thread"));
            const int32 Limit = Args->HasField(TEXT("limit")) ? (int32)Args->GetNumberField(TEXT("limit")) : 20;
            if (Limit < 1 || Limit > 64) return Error(EMCPError::OutOfRange, TEXT("limit must be 1..64"));
            TArray<TSharedPtr<FJsonValue>> Items; int32 Total = 0;
            for (int32 I = Verifications.Num() - 1; I >= 0; --I)
            {
                const auto& V = Verifications[I];
                if (V.Principal != Context.PrincipalId || V.Session != Context.SessionId) continue;
                ++Total; if (Items.Num() < Limit) Items.Add(MakeShared<FJsonValueObject>(VerificationJson(V)));
            }
            auto Out = MakeShared<FJsonObject>(); Out->SetNumberField(TEXT("total"), Total); Out->SetArrayField(TEXT("verifications"), Items);
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%d owned verification record(s)."), Total), Out);
        });
}

void RegisterPrompts(FMCPPromptProvider& Provider)
{
    FMCPPromptDefinition Def;
    Def.Name = TEXT("observe_edit_verify");
    Def.Description = TEXT("Bounded observe -> edit -> capture -> judge -> record -> correct loop for editor UI work using the v5 surface, observer and verification tools.");
    FMCPPromptArgument Goal; Goal.Name = TEXT("goal"); Goal.Description = TEXT("What the visible result must look like"); Goal.bRequired = true; Def.Arguments.Add(Goal);
    FMCPPromptArgument Asset; Asset.Name = TEXT("asset_path"); Asset.Description = TEXT("Asset whose editor should be observed, e.g. /Game/UI/WBP_HUD"); Def.Arguments.Add(Asset);
    FMCPPromptArgument Attempts; Attempts.Name = TEXT("max_attempts"); Attempts.Description = TEXT("Correction attempts before stopping (default 3)"); Def.Arguments.Add(Attempts);
    FMCPPromptGenerator Generator;
    Generator.BindLambda([](const FMCPPromptArgMap& Arguments)
    {
        const FString* Goal = Arguments.Find(TEXT("goal")); const FString* Asset = Arguments.Find(TEXT("asset_path")); const FString* Attempts = Arguments.Find(TEXT("max_attempts"));
        const FString Text = FString::Printf(TEXT(
            "Goal: %s\nTarget asset: %s\nCorrection budget: %s attempts.\n\n"
            "Follow this loop and stop when the goal is met, the budget is spent, or evidence becomes unreliable.\n"
            "1. Reveal: call focus_editor_surface with asset_path (or a surface_id from list_editor_surfaces). Never assume a tab is visible; check capture.supported and its reason.\n"
            "2. Baseline: capture_editor_surface on the exact surface (use region for readable crops). Note frame_id, sequence and pixel_hash.\n"
            "3. Observe: optionally start_editor_observation (1 Hz, history 2, region on the canvas) so later polls return fresh frames; poll get_editor_observation with after_sequence.\n"
            "4. Edit: apply the smallest typed change with the existing tools (widget, blueprint, actor tools). Results report undo_recorded=false during PIE.\n"
            "5. Inspect: look at the newest frame and compare it with the baseline. Also gather deterministic checks separately: validate_widget_layout for overlaps, clipping and out-of-bounds widgets, get_widget_tree and compile_widget_blueprint for structure.\n"
            "6. Record: call record_visual_verification with outcome pass, fail or inconclusive, method assistant for what you saw and deterministic for validator results, the frame_ids inspected and precise criteria. A successful capture is never a pass by itself; stale, blocked or unreadable frames are inconclusive.\n"
            "7. Correct: if fail, change one specific thing and repeat from step 2. If a human changed the surface meanwhile (generation advanced unexpectedly), recapture before judging.\n"
            "8. Finish: stop_editor_observation, then summarise pass/fail/inconclusive records with their frame IDs. Do not claim visual verification for anything you did not see."),
            Goal ? **Goal : TEXT("(not provided)"), Asset && !Asset->IsEmpty() ? **Asset : TEXT("(choose via list_editor_surfaces)"), Attempts && !Attempts->IsEmpty() ? **Attempts : TEXT("3"));
        TArray<FMCPPromptMessage> Messages;
        FMCPPromptMessage Msg; Msg.Role = TEXT("user"); Msg.Content = FMCPContentBlock::MakeText(Text); Messages.Add(Msg);
        return Messages;
    });
    Provider.RegisterPrompt(Def, Generator);
}

void TickObservers(double Now)
{
    check(IsInGameThread());
    const double TickStart = FPlatformTime::Seconds(); bool bOver = false;
    for (auto& Pair : Observers)
    {
        FObserver& O = Pair.Value;
        if (O.State != TEXT("active")) continue;
        // v5 increment 24: one tick may spend at most SampleBudgetMs on captures; the rest sample next tick.
        if (!bOver && (FPlatformTime::Seconds() - TickStart) * 1000.0 > SampleBudgetMs) { bOver = true; ++SampleTicksOverBudget; }
        if (bOver && Now >= O.NextSampleAt) { ++SamplesDeferred; continue; }
        if (Now >= O.ExpiresAt) { O.State = TEXT("expired"); O.LastError = TEXT("Lease expired"); DropFrames(O); continue; }
        if (O.Mode == TEXT("after_operation") && !O.bAfterCaptured)
        {
            FMCPRequestContext Ctx; Ctx.PrincipalId = O.Principal; Ctx.SessionId = O.Session;
            const FReadiness R = ReadinessFor(O.AfterOperationId, Ctx);
            if (!R.bKnown) { O.LastError = TEXT("Operation no longer known; waiting"); continue; }
            if (!R.bReady) { O.LastError = R.bFinished ? TEXT("Operation finished; waiting for a Slate paint") : TEXT("Operation still ") + R.State; continue; }
            const int32 Before = O.Frames.Num(); const uint64 LastSeq = Before ? O.Frames.Last().Sequence : 0;
            O.LastHash.Empty(); // force a retained frame even if pixels match the pre-operation frame
            Sample(O, Now); O.NextSampleAt = Now + 1.0 / O.ActualRateHz;
            if (O.Frames.Num() && O.Frames.Last().Sequence != LastSeq) { O.Frames.Last().AfterOperationId = O.AfterOperationId; O.Frames.Last().Freshness = TEXT("fresh_after_operation"); O.bAfterCaptured = true; O.LastError.Empty(); }
            continue;
        }
        if (Now < O.NextSampleAt) continue;
        if (O.Mode == TEXT("on_change") && PaintSequence == O.LastPaintSeq) continue; // nothing painted since the last sample
        O.NextSampleAt = Now + 1.0 / O.ActualRateHz;
        Sample(O, Now);
    }
    // Terminal observers stay inspectable until their lease would have ended plus a short grace, then vanish.
    for (auto It = Observers.CreateIterator(); It; ++It)
        if (It.Value().State != TEXT("active") && Now >= It.Value().ExpiresAt + 60.0) It.RemoveCurrent();
    if (Observers.IsEmpty()) ReleaseTicker();
}

TSharedPtr<FJsonObject> DiagnosticsJson()
{
    auto Out = MakeShared<FJsonObject>();
    int32 Active = 0, Frames = 0; for (const auto& P : Observers) { if (P.Value.State == TEXT("active")) ++Active; Frames += P.Value.Frames.Num(); }
    Out->SetNumberField(TEXT("observers_total"), Observers.Num()); Out->SetNumberField(TEXT("observers_active"), Active);
    Out->SetNumberField(TEXT("retained_frames"), Frames); Out->SetNumberField(TEXT("retained_bytes"), (double)ObserverBytes);
    Out->SetNumberField(TEXT("retained_bytes_budget"), (double)MaxObserverBytesGlobal);
    Out->SetNumberField(TEXT("catalog_surfaces"), Catalog.Num()); Out->SetNumberField(TEXT("catalog_generation"), CatalogGeneration);
    Out->SetNumberField(TEXT("frame_sequence"), (double)FrameSequence); Out->SetNumberField(TEXT("verification_records"), Verifications.Num());
    Out->SetBoolField(TEXT("ticker_registered"), TickerHandle.IsValid());
    Out->SetNumberField(TEXT("retained_captures"), Retained.Num()); Out->SetNumberField(TEXT("sample_budget_ms"), SampleBudgetMs); Out->SetNumberField(TEXT("sample_ticks_over_budget"), (double)SampleTicksOverBudget); Out->SetNumberField(TEXT("samples_deferred"), (double)SamplesDeferred); Out->SetNumberField(TEXT("paint_sequence"), (double)PaintSequence); Out->SetBoolField(TEXT("paint_hook_registered"), PaintHandle.IsValid());
    return Out;
}

void ClearSession(const FString& SessionId)
{
    check(IsInGameThread());
    for (auto It = Observers.CreateIterator(); It; ++It)
        if (It.Value().Session == SessionId) { DropFrames(It.Value()); It.RemoveCurrent(); }
    for (auto It = Retained.CreateIterator(); It; ++It) if (It.Value().Session == SessionId) { ObserverBytes -= It.Value().Frame.Bytes.Num(); It.RemoveCurrent(); }
    Verifications.RemoveAll([&](const FVerification& V) { return V.Session == SessionId; });
    if (Observers.IsEmpty()) ReleaseTicker();
}

void RegisterResources(FMCPResourceProvider& Provider)
{
    FMCPResourceDefinition Def;
    Def.Uri = TEXT("unreal://visual/surfaces");
    Def.Name = TEXT("Editor surfaces");
    Def.Description = TEXT("Bounded catalog of editor windows, tabs and viewports with capture availability; equivalent to list_editor_surfaces with defaults and no activation.");
    Def.MimeType = TEXT("application/json");
    FMCPResourceReader Reader;
    Reader.BindLambda([](const FString& Uri)
    {
        FMCPResourceContent Content; Content.Uri = Uri; Content.MimeType = TEXT("application/json");
        Content.Text = JsonToString(ScanJson(Scan(), TEXT("any"), FString(), 0, 100));
        return Content;
    });
    Provider.RegisterResource(Def, Reader);

    // v5 increment 15: owned resources now that reads carry caller identity.
    FMCPResourceDefinition Frames; Frames.Uri = TEXT("unreal://visual/frames/{frame_id}"); Frames.Name = TEXT("Retained observer frame");
    Frames.Description = TEXT("Immutable owned image evidence retained by an observer: binary PNG/JPEG blob. Only the owning principal/session can read it; evicted frames are not found."); Frames.MimeType = TEXT("image/png"); Frames.bTemplate = true;
    FMCPResourceReaderCtx FrameReader; FrameReader.BindLambda([](const FString& Uri, const FMCPRequestContext& Context)
    {
        const FString Id = Uri.Mid(FString(TEXT("unreal://visual/frames/")).Len());
        SweepRetained(FPlatformTime::Seconds());
        if (const FStoredFrame* F = FindOwnedFrame(Id, Context)) { FMCPResourceContent C; C.Uri = Uri; C.MimeType = F->Mime; C.Blob = FBase64::Encode(F->Bytes.GetData(), (uint32)F->Bytes.Num()); return C; }
        return FMCPResourceContent::NotFound(Uri, TEXT("Unknown, evicted or foreign frame"));
    });
    Provider.RegisterResourceCtx(Frames, FrameReader);
    FMCPResourceDefinition Obs; Obs.Uri = TEXT("unreal://visual/observers/{observer_id}"); Obs.Name = TEXT("Observer status");
    Obs.Description = TEXT("Owned live-observation status with counters and the newest frame's metadata (no pixels; read unreal://visual/frames/{frame_id})."); Obs.MimeType = TEXT("application/json"); Obs.bTemplate = true;
    FMCPResourceReaderCtx ObsReader; ObsReader.BindLambda([](const FString& Uri, const FMCPRequestContext& Context)
    {
        const FString Id = Uri.Mid(FString(TEXT("unreal://visual/observers/")).Len());
        TickObservers(FPlatformTime::Seconds());
        const FObserver* O = FindOwnedObserver(Id, Context);
        if (!O) return FMCPResourceContent::NotFound(Uri, TEXT("Unknown, expired or foreign observer"));
        auto J = ObserverJson(*O, FPlatformTime::Seconds());
        if (O->Frames.Num()) { J->SetObjectField(TEXT("latest_frame"), FrameMeta(O->Frames.Last())); J->SetStringField(TEXT("latest_frame_uri"), TEXT("unreal://visual/frames/") + O->Frames.Last().FrameId); }
        FMCPResourceContent C; C.Uri = Uri; C.MimeType = TEXT("application/json"); C.Text = JsonToString(J); return C;
    });
    Provider.RegisterResourceCtx(Obs, ObsReader);
}

void Reset()
{
    check(IsInGameThread());
    for (auto& Pair : Observers) DropFrames(Pair.Value);
    Observers.Empty(); ReleaseTicker(); ObserverBytes = 0; Verifications.Empty(); Retained.Empty(); LastCapturePixels.Empty(); SampleTicksOverBudget = SamplesDeferred = 0;
    Catalog.Empty(); CatalogGeneration = 0; FrameSequence = 0;
    if (PaintHandle.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().OnPostTick().Remove(PaintHandle);
    PaintHandle.Reset();
}

uint64 CurrentPaintSequence() { return PaintSequence; }
void AdvancePaintForTest() { ++PaintSequence; }

FString InjectTestFrame(const FMCPRequestContext& Context, const FString& SurfaceId, const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
    check(IsInGameThread());
    FEncodeOptions Options; FEncodedFrame Encoded;
    if (!EncodeFrame(Pixels, Width, Height, Options, Encoded)) return FString();
    FStoredFrame F; F.FrameId = TEXT("frm-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower); F.SurfaceId = SurfaceId; F.Sequence = ++FrameSequence;
    F.CapturedAt = FDateTime::UtcNow().ToIso8601(); F.Mime = Encoded.Mime; F.PixelHash = Encoded.PixelHash; F.Width = Encoded.Width; F.Height = Encoded.Height; F.SourceWidth = Width; F.SourceHeight = Height;
    F.Bytes = MoveTemp(Encoded.Bytes); F.Freshness = TEXT("synthetic_test_frame"); F.PaintSeq = PaintSequence;
    return RetainFrame(MoveTemp(F), Context, FPlatformTime::Seconds());
}

TSharedPtr<FJsonObject> CompareFrames(const FString& BeforeId, const FString& AfterId, const FMCPRequestContext& Context, int32 Threshold, FString& OutError)
{
    check(IsInGameThread());
    SweepRetained(FPlatformTime::Seconds());
    const FStoredFrame* A = FindOwnedFrame(BeforeId, Context); const FStoredFrame* B = FindOwnedFrame(AfterId, Context);
    if (!A || !B) { OutError = TEXT("Unknown, evicted or foreign frame"); return nullptr; }
    auto Decode = [](const FStoredFrame& F, TArray64<uint8>& Raw, int32& W, int32& H) -> bool
    {
        IImageWrapperModule& M = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        TSharedPtr<IImageWrapper> Wr = M.CreateImageWrapper(F.Mime == TEXT("image/jpeg") ? EImageFormat::JPEG : EImageFormat::PNG);
        if (!Wr.IsValid() || !Wr->SetCompressed(F.Bytes.GetData(), F.Bytes.Num())) return false;
        W = Wr->GetWidth(); H = Wr->GetHeight();
        return Wr->GetRaw(ERGBFormat::BGRA, 8, Raw);
    };
    TArray64<uint8> RA, RB; int32 WA = 0, HA = 0, WB = 0, HB = 0;
    if (!Decode(*A, RA, WA, HA) || !Decode(*B, RB, WB, HB)) { OutError = TEXT("Frame decode failed"); return nullptr; }
    if (WA != WB || HA != HB) { OutError = FString::Printf(TEXT("Frame sizes differ (%dx%d vs %dx%d); compare frames captured with the same region and scale"), WA, HA, WB, HB); return nullptr; }
    const int32 T = FMath::Clamp(Threshold, 0, 255);
    int64 Changed = 0; int32 MinX = WA, MinY = HA, MaxX = -1, MaxY = -1;
    constexpr int32 Grid = 8; TArray<int64> Cells; Cells.SetNumZeroed(Grid * Grid); TArray<int64> CellTotals; CellTotals.SetNumZeroed(Grid * Grid);
    for (int32 y = 0; y < HA; ++y) for (int32 x = 0; x < WA; ++x)
    {
        const int64 I = ((int64)y * WA + x) * 4;
        const int32 D = FMath::Max3(FMath::Abs((int32)RA[I] - (int32)RB[I]), FMath::Abs((int32)RA[I + 1] - (int32)RB[I + 1]), FMath::Abs((int32)RA[I + 2] - (int32)RB[I + 2]));
        const int32 Cell = FMath::Min(Grid - 1, y * Grid / HA) * Grid + FMath::Min(Grid - 1, x * Grid / WA);
        ++CellTotals[Cell];
        if (D > T) { ++Changed; ++Cells[Cell]; MinX = FMath::Min(MinX, x); MinY = FMath::Min(MinY, y); MaxX = FMath::Max(MaxX, x); MaxY = FMath::Max(MaxY, y); }
    }
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("before_frame_id"), BeforeId); Out->SetStringField(TEXT("after_frame_id"), AfterId);
    Out->SetStringField(TEXT("before_captured_at"), A->CapturedAt); Out->SetStringField(TEXT("after_captured_at"), B->CapturedAt);
    Out->SetBoolField(TEXT("same_surface"), A->SurfaceId == B->SurfaceId); Out->SetNumberField(TEXT("width"), WA); Out->SetNumberField(TEXT("height"), HA);
    Out->SetNumberField(TEXT("pixel_threshold"), T); Out->SetNumberField(TEXT("changed_pixels"), (double)Changed);
    Out->SetNumberField(TEXT("changed_pct"), WA * HA ? 100.0 * Changed / ((double)WA * HA) : 0.0); Out->SetBoolField(TEXT("identical"), Changed == 0);
    if (Changed > 0) Out->SetObjectField(TEXT("changed_bbox"), RectJson(MinX, MinY, MaxX - MinX + 1, MaxY - MinY + 1)); else Out->SetField(TEXT("changed_bbox"), MakeShared<FJsonValueNull>());
    auto G = MakeShared<FJsonObject>(); G->SetNumberField(TEXT("cols"), Grid); G->SetNumberField(TEXT("rows"), Grid);
    TArray<TSharedPtr<FJsonValue>> Pct; for (int32 i = 0; i < Grid * Grid; ++i) Pct.Add(MakeShared<FJsonValueNumber>(CellTotals[i] ? 100.0 * Cells[i] / CellTotals[i] : 0.0));
    G->SetArrayField(TEXT("changed_pct"), Pct); Out->SetObjectField(TEXT("grid"), G);
    Out->SetStringField(TEXT("note"), TEXT("Deterministic pixel difference only; whether the change is the intended one is a separate judgment (record_visual_verification)."));
    return Out;
}
}
