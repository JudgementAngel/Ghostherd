// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.5 (UE 5.8) — Viewport transform-gizmo control.
//
// 5.8 consolidated the legacy gizmos into one Interactive-Tools-based framework.
// For the level-editor transform widget the stable control surface remains
// FEditorModeTools (GLevelEditorModeTools()): SetWidgetMode / SetCoordSystem.
// These tools let an agent switch translate/rotate/scale and world/local space so
// screenshots and manual-feel operations match intent.

#include "Tools/MCPGizmoTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Editor.h"                 // GLevelEditorModeTools()
#include "EditorModeManager.h"      // FEditorModeTools
#include "UnrealWidgetFwd.h"        // UE::Widget::EWidgetMode, ECoordSystem

namespace MCPGizmoTools
{

static const TCHAR* WidgetModeToString(UE::Widget::EWidgetMode M)
{
	switch (M)
	{
	case UE::Widget::WM_Translate:        return TEXT("translate");
	case UE::Widget::WM_Rotate:           return TEXT("rotate");
	case UE::Widget::WM_Scale:            return TEXT("scale");
	case UE::Widget::WM_TranslateRotateZ: return TEXT("translate_rotate_z");
	case UE::Widget::WM_2D:               return TEXT("2d");
	default:                              return TEXT("none");
	}
}

static const TCHAR* CoordToString(ECoordSystem C)
{
	switch (C)
	{
	case COORD_World:  return TEXT("world");
	case COORD_Local:  return TEXT("local");
	case COORD_Parent: return TEXT("parent");
	default:           return TEXT("none");
	}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// gizmo_set_mode
	// ================================================================
	MCP_TOOL(Registry, "gizmo_set_mode")
		.Description(TEXT(
			"Set the level-editor transform gizmo mode (translate / rotate / scale). Affects the active "
			"viewport widget used for manual-feel operations and screenshots."))
		.EnumArg(TEXT("mode"), TEXT("Gizmo mode."),
			{ TEXT("translate"), TEXT("rotate"), TEXT("scale"), TEXT("translate_rotate_z"), TEXT("2d") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Mode;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("mode"), Mode));
			BAIL_IF_INVALID(FMCPValidate::OneOf(Mode,
				{ TEXT("translate"), TEXT("rotate"), TEXT("scale"), TEXT("translate_rotate_z"), TEXT("2d") }, TEXT("mode")));

			UE::Widget::EWidgetMode WM = UE::Widget::WM_Translate;
			if (Mode == TEXT("rotate")) WM = UE::Widget::WM_Rotate;
			else if (Mode == TEXT("scale")) WM = UE::Widget::WM_Scale;
			else if (Mode == TEXT("translate_rotate_z")) WM = UE::Widget::WM_TranslateRotateZ;
			else if (Mode == TEXT("2d")) WM = UE::Widget::WM_2D;

			GLevelEditorModeTools().SetWidgetMode(WM);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("mode"), WidgetModeToString(WM));
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Gizmo mode set to %s."), WidgetModeToString(WM)), R);
		});

	// ================================================================
	// gizmo_set_coordinate_system
	// ================================================================
	MCP_TOOL(Registry, "gizmo_set_coordinate_system")
		.Description(TEXT("Set the transform gizmo coordinate space (world / local / parent)."))
		.EnumArg(TEXT("space"), TEXT("Coordinate space."),
			{ TEXT("world"), TEXT("local"), TEXT("parent") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Space;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("space"), Space));
			BAIL_IF_INVALID(FMCPValidate::OneOf(Space,
				{ TEXT("world"), TEXT("local"), TEXT("parent") }, TEXT("space")));

			ECoordSystem CS = COORD_World;
			if (Space == TEXT("local")) CS = COORD_Local;
			else if (Space == TEXT("parent")) CS = COORD_Parent;

			GLevelEditorModeTools().SetCoordSystem(CS);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("space"), CoordToString(CS));
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Gizmo coordinate space set to %s."), CoordToString(CS)), R);
		});

	// ================================================================
	// gizmo_get_state
	// ================================================================
	MCP_TOOL(Registry, "gizmo_get_state")
		.Description(TEXT("Report the current transform gizmo mode and coordinate space."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FEditorModeTools& Tools = GLevelEditorModeTools();
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("mode"), WidgetModeToString(Tools.GetWidgetMode()));
			R->SetStringField(TEXT("space"), CoordToString(Tools.GetCoordSystem()));
			return FMCPToolResult::SuccessStructured(TEXT("Current gizmo state"), R);
		});
}

} // namespace MCPGizmoTools
