// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 10: deterministic widget layout validation (first slice of V5-23).

#include "Tools/Widget/WidgetCommon.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/Geometry.h"
#include "WidgetBlueprint.h"

namespace MCPWidgetTools::Layout
{
using namespace MCPWidgetTools::Common;

namespace
{
struct FPlaced
{
    UWidget* Widget = nullptr;
    FSlateRect Rect;          // layout bounding rect in viewport pixels
    FVector2D Desired;        // Slate desired size after prepass
    FVector2D Allotted;       // local size the parent gave it
    bool bLeaf = false;
};

void Walk(const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, const TMap<const SWidget*, UWidget*>& Map, TArray<FPlaced>& Out, int32 Depth, int32& Budget)
{
    if (Depth > 128 || --Budget <= 0) return;
    if (UWidget* const* Found = Map.Find(&Widget.Get()))
    {
        FPlaced P; P.Widget = *Found;
        P.Rect = Geometry.GetLayoutBoundingRect();
        P.Desired = FVector2D(Widget->GetDesiredSize());
        P.Allotted = FVector2D(Geometry.GetLocalSize());
        P.bLeaf = !(*Found)->IsA<UPanelWidget>();
        Out.Add(P);
    }
    FArrangedChildren Children(EVisibility::Visible);
    Widget->ArrangeChildren(Geometry, Children);
    for (int32 I = 0; I < Children.Num(); ++I) Walk(Children[I].Widget, Children[I].Geometry, Map, Out, Depth + 1, Budget);
}

UWidget* LowestCommonAncestor(UWidget* A, UWidget* B)
{
    TSet<UWidget*> Chain;
    for (UWidget* W = A ? A->GetParent() : nullptr; W; W = W->GetParent()) Chain.Add(W);
    for (UWidget* W = B ? B->GetParent() : nullptr; W; W = W->GetParent()) if (Chain.Contains(W)) return W;
    return nullptr;
}

TSharedPtr<FJsonObject> RectJson(const FSlateRect& R)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetNumberField(TEXT("x"), R.Left); Out->SetNumberField(TEXT("y"), R.Top);
    Out->SetNumberField(TEXT("width"), FMath::Max(0.f, R.Right - R.Left)); Out->SetNumberField(TEXT("height"), FMath::Max(0.f, R.Bottom - R.Top));
    return Out;
}
void AddIssue(TArray<TSharedPtr<FJsonValue>>& Issues, const FString& Rule, const FString& Severity, const TArray<FString>& Widgets, const FString& Message, TSharedPtr<FJsonObject> Extra = nullptr)
{
    auto I = MakeShared<FJsonObject>();
    I->SetStringField(TEXT("rule"), Rule); I->SetStringField(TEXT("severity"), Severity); I->SetStringField(TEXT("message"), Message);
    TArray<TSharedPtr<FJsonValue>> W; for (const FString& N : Widgets) W.Add(MakeShared<FJsonValueString>(N));
    I->SetArrayField(TEXT("widgets"), W);
    if (Extra.IsValid()) I->SetObjectField(TEXT("data"), Extra);
    Issues.Add(MakeShared<FJsonValueObject>(I));
}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    MCP_TOOL(Registry, "validate_widget_layout")
        .Description(TEXT("Deterministically lay out a compiled Widget Blueprint at an explicit viewport size and report measurable layout issues: leaf widgets overlapping outside an Overlay (overlap), leaf widgets outside the viewport (out_of_bounds), visible leaves with zero area (zero_size), widgets whose desired size exceeds the space they were given (clipped) and leaves extending outside an ancestor's bounds (overflow). Uses Slate prepass and arrangement on a transient instance; it never renders, edits or saves. Results are validator facts, suitable for record_visual_verification with method=deterministic. Requires a compiled generated class and an idle (non-PIE) editor."))
        .ReadOnly().Idempotent().RequiresPieOff()
        .StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
        .IntArg(TEXT("viewport_width"), TEXT("Layout width in pixels, 64..8192 (default 1280)"))
        .IntArg(TEXT("viewport_height"), TEXT("Layout height in pixels, 64..8192 (default 720)"))
        .NumberArg(TEXT("overlap_tolerance_px"), TEXT("Overlaps thinner than this in both axes are ignored (default 1)"))
        .BoolArg(TEXT("include_rects"), TEXT("Return the rect of every placed widget (default false)"))
        .OutputSchema(TEXT(R"({"type":"object","required":["asset_path","viewport","widget_count","leaf_count","issues","counts","deterministic"],"properties":{"asset_path":{"type":"string"},"viewport":{"type":"object"},"widget_count":{"type":"integer"},"leaf_count":{"type":"integer"},"issues":{"type":"array"},"counts":{"type":"object"},"rects":{"type":"array"},"deterministic":{"type":"boolean"},"truncated":{"type":"boolean"}}})"))
        .Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
        {
            if (!IsInGameThread()) return FMCPToolResult::ErrorStructured(EMCPError::RequiresGameThread, TEXT("Layout validation requires the game thread"));
            const FString AssetPath = Args->GetStringField(TEXT("asset_path"));
            const int32 W = Args->HasField(TEXT("viewport_width")) ? (int32)Args->GetNumberField(TEXT("viewport_width")) : 1280;
            const int32 H = Args->HasField(TEXT("viewport_height")) ? (int32)Args->GetNumberField(TEXT("viewport_height")) : 720;
            const double Tol = Args->HasField(TEXT("overlap_tolerance_px")) ? Args->GetNumberField(TEXT("overlap_tolerance_px")) : 1.0;
            if (W < 64 || W > 8192 || H < 64 || H > 8192 || Tol < 0 || Tol > 64) return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, TEXT("viewport 64..8192 per axis; overlap_tolerance_px 0..64"));
            UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
            if (!WBP) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
            UClass* Generated = WBP->GeneratedClass;
            if (!Generated || !Generated->IsChildOf(UUserWidget::StaticClass())) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("Widget Blueprint has no compiled generated class; call compile_widget_blueprint first"));
            UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
            if (!World) return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("No editor world for a transient layout instance"));

            UUserWidget* Instance = CreateWidget<UUserWidget>(World, TSubclassOf<UUserWidget>(Generated));
            if (!Instance || !Instance->WidgetTree) return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("Could not instantiate the widget for layout"));
            TArray<FPlaced> Placed;
            TMap<const SWidget*, UWidget*> Map;
            {
                TSharedRef<SWidget> Root = Instance->TakeWidget();
                Root->SlatePrepass(1.0f);
                Instance->WidgetTree->ForEachWidget([&](UWidget* Widget)
                {
                    if (const TSharedPtr<SWidget> S = Widget ? Widget->GetCachedWidget() : nullptr) Map.Add(S.Get(), Widget);
                });
                int32 Budget = 20000;
                Walk(Root, FGeometry::MakeRoot(FVector2D(W, H), FSlateLayoutTransform()), Map, Placed, 0, Budget);
                Instance->RemoveFromParent();
            }
            Instance->MarkAsGarbage();

            TArray<TSharedPtr<FJsonValue>> Issues;
            int32 Leaves = 0;
            for (const FPlaced& P : Placed)
            {
                if (!P.bLeaf) continue;
                ++Leaves;
                const FString Name = P.Widget->GetName();
                const double Wd = P.Rect.Right - P.Rect.Left, Hd = P.Rect.Bottom - P.Rect.Top;
                if (Wd < 1.0 || Hd < 1.0) AddIssue(Issues, TEXT("zero_size"), TEXT("warning"), { Name }, FString::Printf(TEXT("'%s' is visible but has no area"), *Name), RectJson(P.Rect));
                if (P.Rect.Left < -0.5 || P.Rect.Top < -0.5 || P.Rect.Right > W + 0.5 || P.Rect.Bottom > H + 0.5)
                    AddIssue(Issues, TEXT("out_of_bounds"), TEXT("error"), { Name }, FString::Printf(TEXT("'%s' extends outside the %dx%d viewport"), *Name, W, H), RectJson(P.Rect));
                if (P.Desired.X > P.Allotted.X + 0.5 || P.Desired.Y > P.Allotted.Y + 0.5)
                {
                    auto D = MakeShared<FJsonObject>();
                    D->SetNumberField(TEXT("desired_width"), P.Desired.X); D->SetNumberField(TEXT("desired_height"), P.Desired.Y);
                    D->SetNumberField(TEXT("allotted_width"), P.Allotted.X); D->SetNumberField(TEXT("allotted_height"), P.Allotted.Y);
                    AddIssue(Issues, TEXT("clipped"), TEXT("warning"), { Name }, FString::Printf(TEXT("'%s' wants %.0fx%.0f but was given %.0fx%.0f"), *Name, P.Desired.X, P.Desired.Y, P.Allotted.X, P.Allotted.Y), D);
                }
            }
            // overflow: a leaf extending outside any ancestor panel's bounds (clipped or drawn over neighbours)
            {
                TMap<UWidget*, FSlateRect> RectOf; for (const FPlaced& P : Placed) RectOf.Add(P.Widget, P.Rect);
                for (const FPlaced& P : Placed)
                {
                    if (!P.bLeaf) continue;
                    for (UWidget* Anc = P.Widget->GetParent(); Anc; Anc = Anc->GetParent())
                    {
                        const FSlateRect* AR = RectOf.Find(Anc); if (!AR) continue;
                        if (P.Rect.Left < AR->Left - 0.5 || P.Rect.Top < AR->Top - 0.5 || P.Rect.Right > AR->Right + 0.5 || P.Rect.Bottom > AR->Bottom + 0.5)
                        {
                            auto D = MakeShared<FJsonObject>(); D->SetObjectField(TEXT("widget"), RectJson(P.Rect)); D->SetObjectField(TEXT("ancestor"), RectJson(*AR)); D->SetStringField(TEXT("ancestor_name"), Anc->GetName());
                            AddIssue(Issues, TEXT("overflow"), TEXT("warning"), { P.Widget->GetName(), Anc->GetName() }, FString::Printf(TEXT("'%s' extends outside its ancestor '%s'"), *P.Widget->GetName(), *Anc->GetName()), D);
                            break;
                        }
                    }
                }
            }
            for (int32 I = 0; I < Placed.Num(); ++I)
            {
                if (!Placed[I].bLeaf) continue;
                for (int32 J = I + 1; J < Placed.Num(); ++J)
                {
                    if (!Placed[J].bLeaf) continue;
                    const FSlateRect& A = Placed[I].Rect; const FSlateRect& B = Placed[J].Rect;
                    const double OW = FMath::Min(A.Right, B.Right) - FMath::Max(A.Left, B.Left);
                    const double OH = FMath::Min(A.Bottom, B.Bottom) - FMath::Max(A.Top, B.Top);
                    if (OW <= Tol || OH <= Tol) continue;
                    if (Cast<UOverlay>(LowestCommonAncestor(Placed[I].Widget, Placed[J].Widget))) continue; // intentional layering
                    auto D = MakeShared<FJsonObject>(); D->SetNumberField(TEXT("overlap_width"), OW); D->SetNumberField(TEXT("overlap_height"), OH);
                    D->SetObjectField(TEXT("a"), RectJson(A)); D->SetObjectField(TEXT("b"), RectJson(B));
                    AddIssue(Issues, TEXT("overlap"), TEXT("warning"), { Placed[I].Widget->GetName(), Placed[J].Widget->GetName() },
                        FString::Printf(TEXT("'%s' and '%s' overlap by %.0fx%.0f px outside an Overlay"), *Placed[I].Widget->GetName(), *Placed[J].Widget->GetName(), OW, OH), D);
                }
            }
            auto Counts = MakeShared<FJsonObject>();
            for (const TCHAR* Rule : { TEXT("overlap"), TEXT("out_of_bounds"), TEXT("zero_size"), TEXT("clipped"), TEXT("overflow") })
            {
                int32 N = 0; for (const auto& V : Issues) if (V->AsObject()->GetStringField(TEXT("rule")) == Rule) ++N;
                Counts->SetNumberField(Rule, N);
            }
            auto Out = MakeShared<FJsonObject>();
            Out->SetStringField(TEXT("asset_path"), WBP->GetPathName());
            auto Viewport = MakeShared<FJsonObject>(); Viewport->SetNumberField(TEXT("width"), W); Viewport->SetNumberField(TEXT("height"), H);
            Out->SetObjectField(TEXT("viewport"), Viewport);
            Out->SetNumberField(TEXT("widget_count"), Placed.Num()); Out->SetNumberField(TEXT("leaf_count"), Leaves);
            Out->SetArrayField(TEXT("issues"), Issues); Out->SetObjectField(TEXT("counts"), Counts);
            Out->SetBoolField(TEXT("deterministic"), true);
            Out->SetBoolField(TEXT("truncated"), Placed.Num() >= 20000);
            if (Args->HasField(TEXT("include_rects")) && Args->GetBoolField(TEXT("include_rects")))
            {
                TArray<TSharedPtr<FJsonValue>> Rects;
                for (const FPlaced& P : Placed)
                {
                    auto R = RectJson(P.Rect); R->SetStringField(TEXT("widget"), P.Widget->GetName()); R->SetStringField(TEXT("type"), P.Widget->GetClass()->GetName()); R->SetBoolField(TEXT("leaf"), P.bLeaf);
                    Rects.Add(MakeShared<FJsonValueObject>(R));
                }
                Out->SetArrayField(TEXT("rects"), Rects);
            }
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Layout of %s at %dx%d: %d widgets, %d leaves, %d issue(s)."), *WBP->GetName(), W, H, Placed.Num(), Leaves, Issues.Num()), Out);
        });
}
}
