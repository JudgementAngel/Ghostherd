// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;
class UWidgetBlueprint;
class UWidget;
class UPanelWidget;
class UClass;
class UObject;
class FJsonObject;
class FProperty;
struct FMCPToolResult;

namespace MCPWidgetTools::Common
{
	UWorld*           GetEditorWorld();
	AActor*           FindActorByLabel(UWorld* World, const FString& Label);
	UWidgetBlueprint* FindWidgetBlueprint(const FString& AssetPath);
	UWidget*          FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName);
	UClass*           WidgetClassFromTypeName(const FString& TypeName);

	/** Resolve a UUserWidget subclass from either a script path ("/Script/Game.MyHUD"),
	 *  a short C++ class name ("CommonActivatableWidget") or a Widget Blueprint asset path
	 *  ("/Game/UI/WBP_Base.WBP_Base" → its GeneratedClass). Returns nullptr and fills OutError on failure. */
	UClass*           ResolveUserWidgetClass(const FString& ClassOrAssetPath, FString& OutError);

	/** Construct a widget for the tree, either from a native widget type name or from a
	 *  Widget Blueprint asset path (an instance of that WBP). Exactly one of the two must be set.
	 *  The widget is outered to WBP->WidgetTree but NOT yet parented. */
	UWidget*          ConstructWidgetForTree(UWidgetBlueprint* WBP, const FString& WidgetType,
	                                         const FString& WidgetClassPath, const FString& DesiredName,
	                                         FString& OutError);

	/** v4.6.2: compile always; write the package to disk only when bSave is true. */
	void              SaveWidgetBlueprint(UWidgetBlueprint* WBP, bool bSave = true);

	/** Reads the optional `save` argument (default true). */
	bool              WantsSave(const TSharedPtr<FJsonObject>& Args);

	/** Compile and report: {compiled, error_count, warning_count, messages[], widget_variables[], animation_variables[], saved}. */
	TSharedPtr<FJsonObject> CompileWidgetBlueprint(UWidgetBlueprint* WBP, bool bSave);

	/** Save an arbitrary already-created asset's package to disk. */
	bool              SaveAssetPackage(UObject* Asset, FString& OutError);

	/** Localizable text helper: uses text_namespace/text_key when present, otherwise FText::FromString. */
	FText             MakeTextArg(const TSharedPtr<FJsonObject>& Args, const FString& Value);

	// ---- Reflection helpers (generic property access, nested paths like "Font.Size") ----
	struct FResolvedProperty
	{
		FProperty* Property = nullptr;
		void*      Container = nullptr;   // container the property lives in (object or inner struct)
		UObject*   Owner = nullptr;
	};
	bool              ResolvePropertyPath(UObject* Object, const FString& Path, FResolvedProperty& Out, FString& OutError);
	bool              SetPropertyFromString(UObject* Object, const FString& Path, const FString& Value, FString& OutError);
	bool              GetPropertyAsString(UObject* Object, const FString& Path, FString& OutValue, FString& OutError);
	TSharedPtr<FJsonObject> DescribeProperty(FProperty* Property, UObject* Owner, void* Container);

	TSharedPtr<FJsonObject> SerializeWidget(UWidget* Widget, bool bIncludeProperties);
}
