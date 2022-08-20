// Copyright Epic Games, Inc. All Rights Reserved.

#include "ycExtraAnimNodeEditor.h"

#include "Textures/SlateIcon.h"
// #include "AnimGraphCommands.h"
#include "Modules/ModuleManager.h"
#include "EditorModeRegistry.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"



#define LOCTEXT_NAMESPACE "FycExtraAnimNodeEditor"

void FycExtraAnimNodeEditor::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FycExtraAnimNodeEditor::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FycExtraAnimNodeEditor, ycExtraAnimNodeEditor)