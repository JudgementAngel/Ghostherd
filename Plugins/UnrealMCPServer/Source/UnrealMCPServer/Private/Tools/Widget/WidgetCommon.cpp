// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/Widget/WidgetCommon.h"
#include "Common/MCPActorResolver.h"

#include "Tools/MCPWidgetTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "UObject/SavePackage.h"

// UMG Widget Blueprint editing
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetToolsModule.h"
#include "Misc/PackageName.h"
#include "Engine/Texture2D.h"

// Blueprint event graph (for bind_widget_event)
#include "K2Node_ComponentBoundEvent.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/CompilerResultsLog.h"
#include "WidgetBlueprintOperationUtils.h"
#include "Animation/WidgetAnimation.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "Internationalization/TextKey.h"

// UMG Widget types
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/GridPanel.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/Border.h"
#include "Components/WrapBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Spacer.h"
#include "Components/RichTextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/PanelWidget.h"

namespace MCPWidgetTools::Common
{

UWorld* GetEditorWorld()
{
	if (GEditor) return GEditor->GetEditorWorldContext().World();
	return nullptr;
}

AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

UWidgetBlueprint* FindWidgetBlueprint(const FString& AssetPath)
{
	return LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
}

UWidget* FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName)
{
	if (!WBP || !WBP->WidgetTree) return nullptr;
	return WBP->WidgetTree->FindWidget(FName(*WidgetName));
}

UClass* WidgetClassFromTypeName(const FString& TypeName)
{
	static TMap<FString, UClass*> ClassMap;
	if (ClassMap.Num() == 0)
	{
		ClassMap.Add(TEXT("CanvasPanel"), UCanvasPanel::StaticClass());
		ClassMap.Add(TEXT("VerticalBox"), UVerticalBox::StaticClass());
		ClassMap.Add(TEXT("HorizontalBox"), UHorizontalBox::StaticClass());
		ClassMap.Add(TEXT("GridPanel"), UGridPanel::StaticClass());
		ClassMap.Add(TEXT("Overlay"), UOverlay::StaticClass());
		ClassMap.Add(TEXT("SizeBox"), USizeBox::StaticClass());
		ClassMap.Add(TEXT("ScaleBox"), UScaleBox::StaticClass());
		ClassMap.Add(TEXT("Border"), UBorder::StaticClass());
		ClassMap.Add(TEXT("WrapBox"), UWrapBox::StaticClass());
		ClassMap.Add(TEXT("UniformGridPanel"), UUniformGridPanel::StaticClass());
		ClassMap.Add(TEXT("ScrollBox"), UScrollBox::StaticClass());
		ClassMap.Add(TEXT("Button"), UButton::StaticClass());
		ClassMap.Add(TEXT("TextBlock"), UTextBlock::StaticClass());
		ClassMap.Add(TEXT("Image"), UImage::StaticClass());
		ClassMap.Add(TEXT("EditableTextBox"), UEditableTextBox::StaticClass());
		ClassMap.Add(TEXT("Slider"), USlider::StaticClass());
		ClassMap.Add(TEXT("ProgressBar"), UProgressBar::StaticClass());
		ClassMap.Add(TEXT("CheckBox"), UCheckBox::StaticClass());
		ClassMap.Add(TEXT("ComboBoxString"), UComboBoxString::StaticClass());
		ClassMap.Add(TEXT("Spacer"), USpacer::StaticClass());
		ClassMap.Add(TEXT("RichTextBlock"), URichTextBlock::StaticClass());
	}
	if (UClass** Found = ClassMap.Find(TypeName))
	{
		return *Found;
	}

	// v4 Phase 0 (bug fix): the curated map covered ~21 of UMG's 50+ widgets
	// (no ListView, TreeView, MultiLineEditableTextBox, Throbber, ...). Fall back
	// to reflection so any concrete UWidget subclass resolves by name.
	auto ResolveWidgetClass = [](const FString& Name) -> UClass*
	{
		UClass* Candidate = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::ExactClass);
		if (!Candidate)
		{
			// UMG classes use the U prefix (ListView -> UListView in C++, but the
			// UClass object itself is named without the prefix; try common variants).
			Candidate = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *Name), EFindFirstObjectOptions::ExactClass);
		}
		if (Candidate
			&& Candidate->IsChildOf(UWidget::StaticClass())
			&& !Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return Candidate;
		}
		return nullptr;
	};

	if (UClass* Reflected = ResolveWidgetClass(TypeName))
	{
		ClassMap.Add(TypeName, Reflected); // cache for next call
		return Reflected;
	}
	return nullptr;
}

bool SaveAssetPackage(UObject* Asset, FString& OutError)
{
	if (!Asset) { OutError = TEXT("Asset is null"); return false; }
	UPackage* Package = Asset->GetPackage();
	if (!Package) { OutError = TEXT("Asset has no package"); return false; }

	Package->MarkPackageDirty();
	FString PackageFilename = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs))
	{
		OutError = FString::Printf(TEXT("SavePackage failed for %s"), *Package->GetName());
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> CompileWidgetBlueprint(UWidgetBlueprint* WBP, bool bSave)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	if (!IsValid(WBP))
	{
		Out->SetBoolField(TEXT("compiled"), false);
		Out->SetStringField(TEXT("error"), TEXT("Widget Blueprint is invalid."));
		return Out;
	}

	Out->SetStringField(TEXT("widget_blueprint"), WBP->GetName());
	Out->SetStringField(TEXT("path"), WBP->GetPathName());

	FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);

	FCompilerResultsLog Results;
	Results.bSilentMode = true;
	FKismetEditorUtilities::CompileBlueprint(WBP, EBlueprintCompileOptions::None, &Results);

	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("severity"),
			Message->GetSeverity() == EMessageSeverity::Error ? TEXT("error") : TEXT("warning"));
		Obj->SetStringField(TEXT("text"), Message->ToText().ToString());
		Messages.Add(MakeShared<FJsonValueObject>(Obj));
		if (Messages.Num() >= 50) break;
	}

	const bool bOk = (Results.NumErrors == 0);
	Out->SetBoolField(TEXT("compiled"), bOk);
	Out->SetNumberField(TEXT("error_count"), Results.NumErrors);
	Out->SetNumberField(TEXT("warning_count"), Results.NumWarnings);
	Out->SetArrayField(TEXT("messages"), Messages);

	// Closed loop: which widgets / animations actually became members of the generated class?
	TArray<TSharedPtr<FJsonValue>> WidgetVars, AnimVars;
	if (UClass* Gen = WBP->GeneratedClass)
	{
		for (TFieldIterator<FObjectProperty> It(Gen, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (!It->PropertyClass) continue;
			if (It->PropertyClass->IsChildOf(UWidgetAnimation::StaticClass()))
				AnimVars.Add(MakeShared<FJsonValueString>(It->GetName()));
			else if (It->PropertyClass->IsChildOf(UWidget::StaticClass()))
				WidgetVars.Add(MakeShared<FJsonValueString>(It->GetName()));
		}
	}
	Out->SetBoolField(TEXT("has_generated_class"), WBP->GeneratedClass != nullptr);
	Out->SetArrayField(TEXT("widget_variables"), WidgetVars);
	Out->SetArrayField(TEXT("animation_variables"), AnimVars);

	bool bSaved = false;
	if (bSave)
	{
		FAssetRegistryModule::AssetCreated(WBP);
		FString SaveErr;
		bSaved = SaveAssetPackage(WBP, SaveErr);
		if (!bSaved) Out->SetStringField(TEXT("save_error"), SaveErr);
	}
	Out->SetBoolField(TEXT("saved"), bSaved);
	return Out;
}

void SaveWidgetBlueprint(UWidgetBlueprint* WBP, bool bSave)
{
	// Kept for the existing call sites; compile results are available through CompileWidgetBlueprint.
	CompileWidgetBlueprint(WBP, bSave);
}

bool WantsSave(const TSharedPtr<FJsonObject>& Args)
{
	bool bSave = true;
	if (Args.IsValid()) Args->TryGetBoolField(TEXT("save"), bSave);
	return bSave;
}

FText MakeTextArg(const TSharedPtr<FJsonObject>& Args, const FString& Value)
{
	FString Namespace, Key;
	if (Args.IsValid()
		&& Args->TryGetStringField(TEXT("text_key"), Key) && !Key.IsEmpty())
	{
		Args->TryGetStringField(TEXT("text_namespace"), Namespace);
		return FText::AsLocalizable_Advanced(FTextKey(Namespace), FTextKey(Key), *Value);
	}
	return FText::FromString(Value);
}

UClass* ResolveUserWidgetClass(const FString& ClassOrAssetPath, FString& OutError)
{
	if (ClassOrAssetPath.IsEmpty()) { OutError = TEXT("Class path is empty"); return nullptr; }

	UClass* Found = nullptr;
	if (ClassOrAssetPath.StartsWith(TEXT("/Script/")))
	{
		Found = LoadObject<UClass>(nullptr, *ClassOrAssetPath);
	}
	else if (ClassOrAssetPath.StartsWith(TEXT("/")))
	{
		// Widget Blueprint asset → generated class. Accept both "/Game/UI/WBP_X" and "/Game/UI/WBP_X.WBP_X".
		FString Path = ClassOrAssetPath;
		if (!Path.Contains(TEXT(".")))
		{
			Path = FString::Printf(TEXT("%s.%s"), *Path, *FPackageName::GetShortName(Path));
		}
		if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path))
		{
			if (!BP->GeneratedClass)
			{
				FKismetEditorUtilities::CompileBlueprint(BP);
			}
			Found = BP->GeneratedClass;
		}
		else
		{
			// Maybe a "_C" class path was given.
			Found = LoadObject<UClass>(nullptr, *Path);
		}
	}
	else
	{
		// Short C++ class name, with or without the U prefix.
		Found = FindFirstObject<UClass>(*ClassOrAssetPath, EFindFirstObjectOptions::ExactClass);
		if (!Found)
		{
			Found = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *ClassOrAssetPath), EFindFirstObjectOptions::ExactClass);
		}
		if (!Found)
		{
			FString Stripped = ClassOrAssetPath;
			if (Stripped.StartsWith(TEXT("U"))) { Stripped.RightChopInline(1); Found = FindFirstObject<UClass>(*Stripped, EFindFirstObjectOptions::ExactClass); }
		}
	}

	if (!Found)
	{
		OutError = FString::Printf(TEXT("Class not found: %s (use '/Script/Module.ClassName', a short native class name, or a Widget Blueprint asset path)"), *ClassOrAssetPath);
		return nullptr;
	}
	if (!Found->IsChildOf(UUserWidget::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Class %s is not a UserWidget subclass"), *Found->GetName());
		return nullptr;
	}
	if (Found->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		OutError = FString::Printf(TEXT("Class %s is deprecated or stale"), *Found->GetName());
		return nullptr;
	}
	return Found;
}

UWidget* ConstructWidgetForTree(UWidgetBlueprint* WBP, const FString& WidgetType,
                                const FString& WidgetClassPath, const FString& DesiredName, FString& OutError)
{
	if (!WBP || !WBP->WidgetTree) { OutError = TEXT("Widget Blueprint has no WidgetTree"); return nullptr; }

	// v5: refuse duplicate names BEFORE constructing. NewObject with an existing
	// name reuses/reconstructs the existing subobject, so the post-construction
	// check below could never fire and the tree ended up with two widgets sharing
	// a name (engine ensure in WidgetBlueprint variable map, corrupted asset).
	if (!DesiredName.IsEmpty() && WBP->WidgetTree->FindWidget(FName(*DesiredName)))
	{
		OutError = FString::Printf(TEXT("A widget named '%s' already exists in this Widget Blueprint"), *DesiredName);
		return nullptr;
	}

	UWidget* NewWidget = nullptr;

	if (!WidgetClassPath.IsEmpty())
	{
		// Instance of another Widget Blueprint (composition). Engine path: FWidgetTemplateBlueprintClass.
		FString Path = WidgetClassPath;
		if (!Path.Contains(TEXT("."))) Path = FString::Printf(TEXT("%s.%s"), *Path, *FPackageName::GetShortName(Path));

		if (Path == WBP->GetPathName())
		{
			OutError = TEXT("A Widget Blueprint cannot contain an instance of itself");
			return nullptr;
		}

		// Make sure it is loaded and compiled so the asset registry / class are valid.
		UWidgetBlueprint* SubWBP = LoadObject<UWidgetBlueprint>(nullptr, *Path);
		if (!SubWBP)
		{
			OutError = FString::Printf(TEXT("Widget Blueprint not found: %s"), *WidgetClassPath);
			return nullptr;
		}
		if (!SubWBP->GeneratedClass) FKismetEditorUtilities::CompileBlueprint(SubWBP);

		// Circular reference guard: the sub-widget must not derive from (or be) this blueprint.
		if (SubWBP->GeneratedClass && WBP->GeneratedClass && SubWBP->GeneratedClass->IsChildOf(WBP->GeneratedClass))
		{
			OutError = TEXT("Circular reference: the sub-widget derives from this Widget Blueprint");
			return nullptr;
		}

		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FAssetData AssetData = AR.GetAssetByObjectPath(FSoftObjectPath(Path));
		if (!AssetData.IsValid())
		{
			AssetData = FAssetData(SubWBP);
		}

		FText Err;
		NewWidget = FWidgetBlueprintOperationUtils::CreateWidgetFromAsset(WBP, AssetData, WBP->WidgetTree, Err);
		if (!NewWidget)
		{
			OutError = Err.IsEmpty() ? FString::Printf(TEXT("Failed to instance %s"), *WidgetClassPath) : Err.ToString();
			return nullptr;
		}
	}
	else
	{
		UClass* WidgetClass = WidgetClassFromTypeName(WidgetType);
		if (!WidgetClass)
		{
			OutError = FString::Printf(TEXT("Unknown widget type: %s"), *WidgetType);
			return nullptr;
		}
		const FString BaseName = DesiredName.IsEmpty() ? WidgetType : DesiredName;
		NewWidget = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*BaseName));
		if (!NewWidget)
		{
			OutError = FString::Printf(TEXT("Failed to construct widget of type: %s"), *WidgetType);
			return nullptr;
		}
	}

	// Apply the requested name (unique within the tree).
	if (!DesiredName.IsEmpty() && NewWidget->GetName() != DesiredName)
	{
		if (UWidget* Existing = WBP->WidgetTree->FindWidget(FName(*DesiredName)))
		{
			if (Existing != NewWidget)
			{
				OutError = FString::Printf(TEXT("A widget named '%s' already exists in this Widget Blueprint"), *DesiredName);
				NewWidget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
				NewWidget->MarkAsGarbage();
				return nullptr;
			}
		}
		NewWidget->Rename(*DesiredName, WBP->WidgetTree, REN_DontCreateRedirectors);
		NewWidget->SetDisplayLabel(DesiredName);
	}

	return NewWidget;
}

// ---------------------------------------------------------------------------
// Reflection helpers
// ---------------------------------------------------------------------------

static FProperty* FindPropertyLoose(UStruct* Struct, const FString& Name)
{
	if (FProperty* Exact = Struct->FindPropertyByName(FName(*Name))) return Exact;
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (It->GetName().Equals(Name, ESearchCase::IgnoreCase)) return *It;
	}
	// Allow "b"-less bool names ("IsVariable" → "bIsVariable") and display names.
	const FString BName = TEXT("b") + Name;
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (It->GetName().Equals(BName, ESearchCase::IgnoreCase)) return *It;
#if WITH_EDITOR
		if (It->GetDisplayNameText().ToString().Replace(TEXT(" "), TEXT("")).Equals(Name, ESearchCase::IgnoreCase)) return *It;
#endif
	}
	return nullptr;
}

bool ResolvePropertyPath(UObject* Object, const FString& Path, FResolvedProperty& Out, FString& OutError)
{
	if (!Object) { OutError = TEXT("Object is null"); return false; }
	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("."), true);
	if (Segments.Num() == 0) { OutError = TEXT("Property path is empty"); return false; }

	UStruct* Struct = Object->GetClass();
	void* Container = Object;
	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		FProperty* Prop = FindPropertyLoose(Struct, Segments[i]);
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *Segments[i], *Struct->GetName());
			return false;
		}
		if (i == Segments.Num() - 1)
		{
			Out.Property = Prop;
			Out.Container = Container;
			Out.Owner = Object;
			return true;
		}
		FStructProperty* StructProp = CastField<FStructProperty>(Prop);
		if (!StructProp)
		{
			OutError = FString::Printf(TEXT("Property '%s' is not a struct; cannot descend into '%s'"), *Segments[i], *Segments[i + 1]);
			return false;
		}
		Container = StructProp->ContainerPtrToValuePtr<void>(Container);
		Struct = StructProp->Struct;
	}
	return false;
}

bool SetPropertyFromString(UObject* Object, const FString& Path, const FString& Value, FString& OutError)
{
	FResolvedProperty R;
	if (!ResolvePropertyPath(Object, Path, R, OutError)) return false;

	// Object references given as asset paths: make sure the target is loaded so ImportText can find it.
	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(R.Property))
	{
		if (Value.StartsWith(TEXT("/")) && !Value.Equals(TEXT("/"), ESearchCase::CaseSensitive))
		{
			FString Path2 = Value;
			Path2.TrimQuotesInline();
			// Strip a leading class prefix like "Texture2D'/Game/..'" if present.
			int32 Quote = Path2.Find(TEXT("'"));
			if (Quote != INDEX_NONE) { Path2 = Path2.Mid(Quote + 1); Path2.RemoveFromEnd(TEXT("'")); }
			LoadObject<UObject>(nullptr, *Path2);
		}
	}

	Object->Modify();
	void* ValuePtr = R.Property->ContainerPtrToValuePtr<void>(R.Container);
	const TCHAR* Result = R.Property->ImportText_Direct(*Value, ValuePtr, R.Owner, PPF_None);
	if (!Result)
	{
		OutError = FString::Printf(TEXT("Could not parse '%s' as %s for property '%s'"),
			*Value, *R.Property->GetCPPType(), *Path);
		return false;
	}

	FPropertyChangedEvent Event(R.Property, EPropertyChangeType::ValueSet);
	Object->PostEditChangeProperty(Event);
	return true;
}

bool GetPropertyAsString(UObject* Object, const FString& Path, FString& OutValue, FString& OutError)
{
	FResolvedProperty R;
	if (!ResolvePropertyPath(Object, Path, R, OutError)) return false;
	OutValue.Reset();
	const void* ValuePtr = R.Property->ContainerPtrToValuePtr<void>(R.Container);
	R.Property->ExportText_Direct(OutValue, ValuePtr, ValuePtr, R.Owner, PPF_None);
	return true;
}

TSharedPtr<FJsonObject> DescribeProperty(FProperty* Property, UObject* Owner, void* Container)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), Property->GetName());
	Obj->SetStringField(TEXT("type"), Property->GetCPPType());
#if WITH_EDITOR
	Obj->SetStringField(TEXT("category"), Property->GetMetaData(TEXT("Category")));
	Obj->SetStringField(TEXT("display_name"), Property->GetDisplayNameText().ToString());
#endif
	Obj->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst));
	Obj->SetBoolField(TEXT("blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
	if (Container)
	{
		FString Value;
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
		Property->ExportText_Direct(Value, ValuePtr, ValuePtr, Owner, PPF_None);
		if (Value.Len() > 512) Value = Value.Left(512) + TEXT("...");
		Obj->SetStringField(TEXT("value"), Value);
	}
	return Obj;
}

TSharedPtr<FJsonObject> SerializeWidget(UWidget* Widget, bool bIncludeProperties)
{
	if (!Widget) return nullptr;

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), Widget->GetName());
	Obj->SetStringField(TEXT("type"), Widget->GetClass()->GetName());

	if (bIncludeProperties)
	{
		Obj->SetStringField(TEXT("visibility"), UEnum::GetValueAsString(Widget->GetVisibility()));
		Obj->SetBoolField(TEXT("is_enabled"), Widget->GetIsEnabled());
		Obj->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);
		Obj->SetNumberField(TEXT("render_opacity"), Widget->GetRenderOpacity());
		Obj->SetBoolField(TEXT("is_user_widget"), Widget->IsA<UUserWidget>());
		if (Widget->Slot) Obj->SetStringField(TEXT("slot_type"), Widget->Slot->GetClass()->GetName());

		if (UTextBlock* TB = Cast<UTextBlock>(Widget))
		{
			Obj->SetStringField(TEXT("text"), TB->GetText().ToString());
		}
		else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
		{
			Obj->SetNumberField(TEXT("percent"), PB->GetPercent());
		}

		// Include slot info if widget has a parent
		if (Widget->Slot)
		{
			if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
				SlotObj->SetStringField(TEXT("type"), TEXT("CanvasPanelSlot"));
				FAnchors Anchors = CS->GetAnchors();
				SlotObj->SetNumberField(TEXT("anchor_min_x"), Anchors.Minimum.X);
				SlotObj->SetNumberField(TEXT("anchor_min_y"), Anchors.Minimum.Y);
				SlotObj->SetNumberField(TEXT("anchor_max_x"), Anchors.Maximum.X);
				SlotObj->SetNumberField(TEXT("anchor_max_y"), Anchors.Maximum.Y);
				FMargin Offsets = CS->GetOffsets();
				SlotObj->SetNumberField(TEXT("offset_left"), Offsets.Left);
				SlotObj->SetNumberField(TEXT("offset_top"), Offsets.Top);
				SlotObj->SetNumberField(TEXT("offset_right"), Offsets.Right);
				SlotObj->SetNumberField(TEXT("offset_bottom"), Offsets.Bottom);
				Obj->SetObjectField(TEXT("slot"), SlotObj);
			}
		}
	}

	// Recurse into children if this is a panel widget
	UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
	if (Panel)
	{
		TArray<TSharedPtr<FJsonValue>> ChildArray;
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			UWidget* Child = Panel->GetChildAt(i);
			TSharedPtr<FJsonObject> ChildObj = SerializeWidget(Child, bIncludeProperties);
			if (ChildObj)
			{
				ChildArray.Add(MakeShared<FJsonValueObject>(ChildObj));
			}
		}
		if (ChildArray.Num() > 0)
		{
			Obj->SetArrayField(TEXT("children"), ChildArray);
		}
	}

	return Obj;
}

} // namespace MCPWidgetTools::Common
