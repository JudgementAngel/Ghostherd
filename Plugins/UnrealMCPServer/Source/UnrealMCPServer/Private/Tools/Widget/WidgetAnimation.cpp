// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.6.2: Widget animation authoring. UWidgetAnimation + UMovieScene are editor-only
// structures with no Python surface; these tools mirror what the UMG Animations tab and
// Sequencer do (AnimationTabSummoner::OnNewAnimationClicked, UWidgetAnimation::BindPossessableObject).

#include "Tools/Widget/WidgetCommon.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Blueprint/UserWidget.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/Widget.h"
#include "Components/PanelSlot.h"
#include "Components/SlateWrapperTypes.h"

#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieSceneMarginTrack.h"
#include "Animation/MovieSceneMarginSection.h"

#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneEnumTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneColorSection.h"
#include "Sections/MovieSceneEnumSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Misc/FrameRate.h"
#include "Misc/FrameTime.h"

namespace MCPWidgetTools::Animation
{

using namespace MCPWidgetTools::Common;

namespace
{
	UWidgetAnimation* FindAnimation(UWidgetBlueprint* WBP, const FString& Name)
	{
		for (UWidgetAnimation* Anim : WBP->Animations)
		{
			if (Anim && (Anim->GetName() == Name || Anim->GetDisplayLabel() == Name)) return Anim;
		}
		return nullptr;
	}

	FFrameNumber SecondsToFrame(UMovieScene* MovieScene, double Seconds)
	{
		return (Seconds * MovieScene->GetTickResolution()).RoundToFrame();
	}

	double FrameToSeconds(UMovieScene* MovieScene, FFrameNumber Frame)
	{
		return MovieScene->GetTickResolution().AsSeconds(FFrameTime(Frame));
	}

	/** Resolve the object to animate: a tree widget, its slot, or the user widget itself ("Self"). */
	struct FAnimTarget
	{
		UObject* Object = nullptr;
		UWidget* Widget = nullptr;
		UPanelSlot* Slot = nullptr;
		bool bIsRoot = false;
	};

	bool ResolveTarget(UWidgetBlueprint* WBP, const FString& WidgetName, const FString& Target, FAnimTarget& Out, FString& OutError)
	{
		if (WidgetName.Equals(TEXT("Self"), ESearchCase::IgnoreCase) || WidgetName.Equals(WBP->GetName()))
		{
			if (!WBP->GeneratedClass) { OutError = TEXT("Widget Blueprint has no generated class"); return false; }
			Out.Object = WBP->GeneratedClass->GetDefaultObject();
			Out.bIsRoot = true;
			return true;
		}
		UWidget* Widget = FindWidgetByName(WBP, WidgetName);
		if (!Widget) { OutError = FString::Printf(TEXT("Widget not found: %s"), *WidgetName); return false; }
		Out.Widget = Widget;
		if (Target.Equals(TEXT("slot"), ESearchCase::IgnoreCase))
		{
			if (!Widget->Slot) { OutError = FString::Printf(TEXT("Widget '%s' has no slot"), *WidgetName); return false; }
			Out.Slot = Widget->Slot;
			Out.Object = Widget->Slot;
		}
		else
		{
			Out.Object = Widget;
		}
		return true;
	}

	/** Find or create the possessable binding for a target inside an animation. */
	FGuid FindOrAddBinding(UWidgetBlueprint* WBP, UWidgetAnimation* Anim, const FAnimTarget& T, bool bCreate)
	{
		const FName WidgetName = T.bIsRoot ? WBP->GeneratedClass->GetFName() : T.Widget->GetFName();
		const FName SlotName = T.Slot ? T.Slot->GetFName() : NAME_None;
		for (const FWidgetAnimationBinding& B : Anim->AnimationBindings)
		{
			if (T.bIsRoot && B.bIsRootWidget) return B.AnimationGuid;
			if (!T.bIsRoot && !B.bIsRootWidget && B.WidgetName == WidgetName && B.SlotWidgetName == SlotName) return B.AnimationGuid;
		}
		if (!bCreate) return FGuid();

		Anim->Modify();
		Anim->MovieScene->Modify();
		const FString DisplayName = T.Slot ? FString::Printf(TEXT("%s (%s)"), *T.Widget->GetName(), *T.Slot->GetClass()->GetName())
			: (T.bIsRoot ? WBP->GetName() : T.Widget->GetName());
		const FGuid Guid = Anim->MovieScene->AddPossessable(DisplayName, T.Object->GetClass());

		// Same shape as UWidgetAnimation::BindPossessableObject (which needs a preview widget we do not have).
		FWidgetAnimationBinding NewBinding;
		NewBinding.AnimationGuid = Guid;
		NewBinding.bIsRootWidget = T.bIsRoot;
		NewBinding.WidgetName = WidgetName;
		NewBinding.SlotWidgetName = SlotName;
		Anim->AnimationBindings.Add(NewBinding);
		return Guid;
	}

	struct FTrackSpec
	{
		UClass* TrackClass = nullptr;
		FName PropertyName;
		FString PropertyPath;
		FString Kind;   // Float | Transform | Color | Visibility | Margin | Bool
	};

	bool BuildTrackSpec(const FString& TrackType, const FString& PropertyOverride, const FAnimTarget& T, FTrackSpec& Out, FString& OutError)
	{
		const FString Type = TrackType;
		if (Type.Equals(TEXT("Opacity"), ESearchCase::IgnoreCase))
		{
			Out.TrackClass = UMovieSceneFloatTrack::StaticClass(); Out.PropertyName = TEXT("RenderOpacity"); Out.PropertyPath = TEXT("RenderOpacity"); Out.Kind = TEXT("Float");
		}
		else if (Type.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
		{
			Out.TrackClass = UMovieScene2DTransformTrack::StaticClass(); Out.PropertyName = TEXT("RenderTransform"); Out.PropertyPath = TEXT("RenderTransform"); Out.Kind = TEXT("Transform");
		}
		else if (Type.Equals(TEXT("Color"), ESearchCase::IgnoreCase))
		{
			Out.TrackClass = UMovieSceneColorTrack::StaticClass();
			FString Prop = PropertyOverride.IsEmpty() ? FString(TEXT("ColorAndOpacity")) : PropertyOverride;
			Out.PropertyName = FName(*Prop); Out.PropertyPath = Prop; Out.Kind = TEXT("Color");
		}
		else if (Type.Equals(TEXT("Visibility"), ESearchCase::IgnoreCase))
		{
			Out.TrackClass = UMovieSceneEnumTrack::StaticClass(); Out.PropertyName = TEXT("Visibility"); Out.PropertyPath = TEXT("Visibility"); Out.Kind = TEXT("Visibility");
		}
		else if (Type.Equals(TEXT("Margin"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("SlotOffsets"), ESearchCase::IgnoreCase))
		{
			Out.TrackClass = UMovieSceneMarginTrack::StaticClass();
			FString Prop = PropertyOverride.IsEmpty() ? FString(T.Slot ? TEXT("LayoutData.Offsets") : TEXT("Padding")) : PropertyOverride;
			FString Leaf = Prop; int32 Dot; if (Prop.FindLastChar('.', Dot)) Leaf = Prop.Mid(Dot + 1);
			Out.PropertyName = FName(*Leaf); Out.PropertyPath = Prop; Out.Kind = TEXT("Margin");
		}
		else if (Type.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
		{
			if (PropertyOverride.IsEmpty()) { OutError = TEXT("track_type Float needs property_name"); return false; }
			Out.TrackClass = UMovieSceneFloatTrack::StaticClass();
			FString Leaf = PropertyOverride; int32 Dot; if (PropertyOverride.FindLastChar('.', Dot)) Leaf = PropertyOverride.Mid(Dot + 1);
			Out.PropertyName = FName(*Leaf); Out.PropertyPath = PropertyOverride; Out.Kind = TEXT("Float");
		}
		else if (Type.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
		{
			if (PropertyOverride.IsEmpty()) { OutError = TEXT("track_type Bool needs property_name"); return false; }
			Out.TrackClass = UMovieSceneBoolTrack::StaticClass();
			FString Leaf = PropertyOverride; int32 Dot; if (PropertyOverride.FindLastChar('.', Dot)) Leaf = PropertyOverride.Mid(Dot + 1);
			Out.PropertyName = FName(*Leaf); Out.PropertyPath = PropertyOverride; Out.Kind = TEXT("Bool");
		}
		else
		{
			OutError = FString::Printf(TEXT("Unknown track_type '%s' (Opacity, Transform, Color, Visibility, Margin, Float, Bool)"), *TrackType);
			return false;
		}

		// Sanity check: the property must exist on the target object.
		FResolvedProperty R; FString Err;
		if (!ResolvePropertyPath(T.Object, Out.PropertyPath, R, Err))
		{
			OutError = FString::Printf(TEXT("%s (target %s)"), *Err, *T.Object->GetClass()->GetName());
			return false;
		}
		return true;
	}

	UMovieScenePropertyTrack* FindPropertyTrack(UMovieScene* MovieScene, const FGuid& Guid, FName PropertyName)
	{
		for (UMovieSceneTrack* Track : MovieScene->FindTracks(UMovieScenePropertyTrack::StaticClass(), Guid))
		{
			UMovieScenePropertyTrack* PT = Cast<UMovieScenePropertyTrack>(Track);
			if (PT && PT->GetPropertyName() == PropertyName) return PT;
		}
		return nullptr;
	}

	void AddFloatKey(FMovieSceneFloatChannel* Channel, FFrameNumber Frame, float Value, const FString& Interp)
	{
		if (!Channel) return;
		if (Interp.Equals(TEXT("Linear"), ESearchCase::IgnoreCase)) Channel->AddLinearKey(Frame, Value);
		else if (Interp.Equals(TEXT("Constant"), ESearchCase::IgnoreCase)) Channel->AddConstantKey(Frame, Value);
		else Channel->AddCubicKey(Frame, Value);
	}

	void EnsureRangeCovers(UMovieScene* MovieScene, UMovieSceneSection* Section, FFrameNumber Frame)
	{
		Section->ExpandToFrame(Frame);
		TRange<FFrameNumber> Playback = MovieScene->GetPlaybackRange();
		if (!Playback.Contains(Frame))
		{
			MovieScene->SetPlaybackRange(TRange<FFrameNumber>::Hull(Playback, TRange<FFrameNumber>(FFrameNumber(0), Frame + 1)));
		}
	}

	TSharedPtr<FJsonObject> DescribeAnimation(UWidgetBlueprint* WBP, UWidgetAnimation* Anim)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Anim->GetName());
		O->SetStringField(TEXT("display_label"), Anim->GetDisplayLabel());
		UMovieScene* MS = Anim->MovieScene;
		if (!MS) return O;
		const TRange<FFrameNumber> Range = MS->GetPlaybackRange();
		O->SetNumberField(TEXT("start_seconds"), FrameToSeconds(MS, Range.GetLowerBoundValue()));
		O->SetNumberField(TEXT("end_seconds"), FrameToSeconds(MS, Range.GetUpperBoundValue()));
		O->SetNumberField(TEXT("display_rate"), MS->GetDisplayRate().AsDecimal());

		TArray<TSharedPtr<FJsonValue>> Bindings;
		for (const FWidgetAnimationBinding& B : Anim->AnimationBindings)
		{
			TSharedPtr<FJsonObject> BO = MakeShared<FJsonObject>();
			BO->SetStringField(TEXT("widget"), B.bIsRootWidget ? TEXT("Self") : B.WidgetName.ToString());
			if (!B.SlotWidgetName.IsNone()) BO->SetStringField(TEXT("slot"), B.SlotWidgetName.ToString());
			BO->SetStringField(TEXT("guid"), B.AnimationGuid.ToString());

			TArray<TSharedPtr<FJsonValue>> Tracks;
			for (UMovieSceneTrack* Track : MS->FindTracks(UMovieScenePropertyTrack::StaticClass(), B.AnimationGuid))
			{
				UMovieScenePropertyTrack* PT = Cast<UMovieScenePropertyTrack>(Track);
				if (!PT) continue;
				TSharedPtr<FJsonObject> TO = MakeShared<FJsonObject>();
				TO->SetStringField(TEXT("type"), PT->GetClass()->GetName());
				TO->SetStringField(TEXT("property"), PT->GetPropertyName().ToString());
				TO->SetStringField(TEXT("property_path"), PT->GetPropertyPath().ToString());
				int32 KeyCount = 0;
				for (UMovieSceneSection* Section : PT->GetAllSections())
				{
					if (!Section) continue;
					FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
					for (const FMovieSceneChannelEntry& Entry : Proxy.GetAllEntries())
					{
						for (FMovieSceneChannel* Ch : Entry.GetChannels())
						{
							if (Ch) KeyCount += Ch->GetNumKeys();
						}
					}
				}
				TO->SetNumberField(TEXT("sections"), PT->GetAllSections().Num());
				TO->SetNumberField(TEXT("keys"), KeyCount);
				Tracks.Add(MakeShared<FJsonValueObject>(TO));
			}
			BO->SetArrayField(TEXT("tracks"), Tracks);
			Bindings.Add(MakeShared<FJsonValueObject>(BO));
		}
		O->SetArrayField(TEXT("bindings"), Bindings);
		return O;
	}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_widget_animation
	// ================================================================
	MCP_TOOL(Registry, "create_widget_animation")
		.Description(TEXT("Create a widget animation (UMG Animations tab '+ Animation') in a Widget Blueprint. The animation becomes a member variable (playable with PlayAnimation). Add tracks with add_widget_animation_track, keys with add_widget_animation_key."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("animation_name"), TEXT("Name of the animation (unique among widgets, variables and animations), e.g. 'FadeIn'"), true)
		.NumberArg(TEXT("length_seconds"), TEXT("Initial playback length in seconds (default 1.0; keys beyond it extend the range)"))
		.NumberArg(TEXT("frame_rate"), TEXT("Display frame rate (default 20, the UMG default)"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, Name;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("animation_name"), Name) || Name.IsEmpty()) return FMCPToolResult::Error(TEXT("animation_name is required"));
			double Length = 1.0; if (Args->HasField(TEXT("length_seconds"))) Length = Args->GetNumberField(TEXT("length_seconds"));
			int32 FrameRate = 20; if (Args->HasField(TEXT("frame_rate"))) FrameRate = FMath::Max(1, (int32)Args->GetNumberField(TEXT("frame_rate")));
			if (Length <= 0.0) return FMCPToolResult::Error(TEXT("length_seconds must be > 0"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			if (FindAnimation(WBP, Name))
				return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists, FString::Printf(TEXT("Animation '%s' already exists"), *Name));
			if (FindWidgetByName(WBP, Name))
				return FMCPToolResult::ErrorStructured(EMCPError::InvalidName, FString::Printf(TEXT("'%s' is already a widget name in this Blueprint"), *Name));
			if (FBlueprintEditorUtils::FindNewVariableIndex(WBP, FName(*Name)) != INDEX_NONE)
				return FMCPToolResult::ErrorStructured(EMCPError::InvalidName, FString::Printf(TEXT("'%s' is already a variable name in this Blueprint"), *Name));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Widget Animation")));
			WBP->Modify();

			// Mirrors AnimationTabSummoner::OnNewAnimationClicked (UE 5.8)
			UWidgetAnimation* Anim = NewObject<UWidgetAnimation>(WBP, FName(*Name), RF_Transactional);
			Anim->SetDisplayLabel(Name);
			Anim->MovieScene = NewObject<UMovieScene>(Anim, FName(*Name), RF_Transactional);
			Anim->MovieScene->SetDisplayRate(FFrameRate(FrameRate, 1));
			const FFrameTime OutFrame = Length * Anim->MovieScene->GetTickResolution();
			Anim->MovieScene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), OutFrame.FrameNumber + 1));
			Anim->MovieScene->GetEditorData().WorkStart = 0.0;
			Anim->MovieScene->GetEditorData().WorkEnd = Length;

			WBP->Animations.Add(Anim);
			WBP->OnVariableAdded(Anim->GetFName());
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

			GEditor->EndTransaction();

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			return FMCPToolResult::SuccessStructured(FString::Printf(
				TEXT("Created animation '%s' (%.2fs @ %d fps) in %s"), *Name, Length, FrameRate, *AssetPath), DescribeAnimation(WBP, Anim));
		});

	// ================================================================
	// add_widget_animation_track
	// ================================================================
	MCP_TOOL(Registry, "add_widget_animation_track")
		.Description(TEXT("Add a property track for a widget (or its slot, or the user widget itself with widget_name='Self') to a widget animation. track_type: Opacity (RenderOpacity), Transform (RenderTransform: translation/rotation/scale/shear), Color (ColorAndOpacity or property_name), Visibility (ESlateVisibility), Margin (slot 'LayoutData.Offsets' or Padding), Float / Bool (any property via property_name). Creates the binding and an empty section spanning the playback range; then add keys."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("animation_name"), TEXT("Animation to edit"), true)
		.StringArg(TEXT("widget_name"), TEXT("Widget to animate, or 'Self' for the user widget"), true)
		.EnumArg(TEXT("track_type"), TEXT("Kind of track"), { TEXT("Opacity"), TEXT("Transform"), TEXT("Color"), TEXT("Visibility"), TEXT("Margin"), TEXT("Float"), TEXT("Bool") }, true)
		.EnumArg(TEXT("target"), TEXT("Animate the widget (default) or its slot (for Margin / canvas offsets)"), { TEXT("widget"), TEXT("slot") })
		.StringArg(TEXT("property_name"), TEXT("Property (or dotted path) for Float/Bool/Color/Margin tracks, e.g. 'RenderOpacity', 'ColorAndOpacity', 'LayoutData.Offsets'"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, AnimName, WidgetName, TrackType, Target, PropertyName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("animation_name"), AnimName)) return FMCPToolResult::Error(TEXT("animation_name is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			if (!Args->TryGetStringField(TEXT("track_type"), TrackType)) return FMCPToolResult::Error(TEXT("track_type is required"));
			Args->TryGetStringField(TEXT("target"), Target);
			Args->TryGetStringField(TEXT("property_name"), PropertyName);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidgetAnimation* Anim = FindAnimation(WBP, AnimName);
			if (!Anim || !Anim->MovieScene) return FMCPToolResult::Error(FString::Printf(TEXT("Animation not found: %s"), *AnimName));

			FAnimTarget T; FString Err;
			if (!ResolveTarget(WBP, WidgetName, Target, T, Err)) return FMCPToolResult::Error(Err);
			FTrackSpec Spec;
			if (!BuildTrackSpec(TrackType, PropertyName, T, Spec, Err)) return FMCPToolResult::Error(Err);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Widget Animation Track")));
			WBP->Modify();
			Anim->Modify();
			Anim->MovieScene->Modify();

			const FGuid Guid = FindOrAddBinding(WBP, Anim, T, /*bCreate*/ true);

			if (UMovieScenePropertyTrack* ExistingTrack = FindPropertyTrack(Anim->MovieScene, Guid, Spec.PropertyName))
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Success(FString::Printf(TEXT("Track for %s.%s already exists in '%s' (%s)"),
					*WidgetName, *Spec.PropertyName.ToString(), *AnimName, *ExistingTrack->GetClass()->GetName()));
			}

			UMovieSceneTrack* NewTrack = Anim->MovieScene->AddTrack(Spec.TrackClass, Guid);
			UMovieScenePropertyTrack* PropTrack = Cast<UMovieScenePropertyTrack>(NewTrack);
			if (!PropTrack)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create the track"));
			}
			PropTrack->SetPropertyNameAndPath(Spec.PropertyName, Spec.PropertyPath);

			if (UMovieSceneEnumTrack* EnumTrack = Cast<UMovieSceneEnumTrack>(PropTrack))
			{
				FResolvedProperty R; FString Dummy;
				if (ResolvePropertyPath(T.Object, Spec.PropertyPath, R, Dummy))
				{
					if (FEnumProperty* EP = CastField<FEnumProperty>(R.Property)) EnumTrack->SetEnum(EP->GetEnum());
					else if (FByteProperty* BP = CastField<FByteProperty>(R.Property)) EnumTrack->SetEnum(BP->Enum);
				}
			}

			UMovieSceneSection* Section = PropTrack->CreateNewSection();
			Section->SetRange(Anim->MovieScene->GetPlaybackRange());
			PropTrack->AddSection(*Section);

			FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
			GEditor->EndTransaction();

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			return FMCPToolResult::SuccessStructured(FString::Printf(
				TEXT("Added %s track (%s) for '%s' to animation '%s'. Add keys with add_widget_animation_key."),
				*Spec.Kind, *Spec.PropertyPath, *WidgetName, *AnimName), DescribeAnimation(WBP, Anim));
		});

	// ================================================================
	// add_widget_animation_key
	// ================================================================
	MCP_TOOL(Registry, "add_widget_animation_key")
		.Description(TEXT("Add a keyframe at time_seconds to an existing track. Opacity/Float: value. Transform: translation_x/y, rotation, scale_x/y, shear_x/y (only provided channels get keys). Color: color_r/g/b/a. Visibility: visibility enum. Margin: left/top/right/bottom. Bool: bool_value. The section and playback range grow to include the key."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("animation_name"), TEXT("Animation to edit"), true)
		.StringArg(TEXT("widget_name"), TEXT("Widget bound in the animation, or 'Self'"), true)
		.EnumArg(TEXT("track_type"), TEXT("Track to key (same as add_widget_animation_track)"), { TEXT("Opacity"), TEXT("Transform"), TEXT("Color"), TEXT("Visibility"), TEXT("Margin"), TEXT("Float"), TEXT("Bool") }, true)
		.EnumArg(TEXT("target"), TEXT("widget (default) or slot"), { TEXT("widget"), TEXT("slot") })
		.StringArg(TEXT("property_name"), TEXT("Property for Float/Bool/Color/Margin tracks (must match the track)"))
		.NumberArg(TEXT("time_seconds"), TEXT("Key time in seconds"), true)
		.EnumArg(TEXT("interpolation"), TEXT("Key interpolation for float channels (default Auto/cubic)"), { TEXT("Auto"), TEXT("Linear"), TEXT("Constant") })
		.NumberArg(TEXT("value"), TEXT("Float value (Opacity / Float)"))
		.NumberArg(TEXT("translation_x"), TEXT("Transform: translation X"))
		.NumberArg(TEXT("translation_y"), TEXT("Transform: translation Y"))
		.NumberArg(TEXT("rotation"), TEXT("Transform: rotation angle in degrees"))
		.NumberArg(TEXT("scale_x"), TEXT("Transform: scale X"))
		.NumberArg(TEXT("scale_y"), TEXT("Transform: scale Y"))
		.NumberArg(TEXT("shear_x"), TEXT("Transform: shear X"))
		.NumberArg(TEXT("shear_y"), TEXT("Transform: shear Y"))
		.NumberArg(TEXT("color_r"), TEXT("Color: red"))
		.NumberArg(TEXT("color_g"), TEXT("Color: green"))
		.NumberArg(TEXT("color_b"), TEXT("Color: blue"))
		.NumberArg(TEXT("color_a"), TEXT("Color: alpha"))
		.EnumArg(TEXT("visibility"), TEXT("Visibility track value"), { TEXT("Visible"), TEXT("Collapsed"), TEXT("Hidden"), TEXT("HitTestInvisible"), TEXT("SelfHitTestInvisible") })
		.NumberArg(TEXT("left"), TEXT("Margin: left"))
		.NumberArg(TEXT("top"), TEXT("Margin: top"))
		.NumberArg(TEXT("right"), TEXT("Margin: right"))
		.NumberArg(TEXT("bottom"), TEXT("Margin: bottom"))
		.BoolArg(TEXT("bool_value"), TEXT("Bool track value"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, AnimName, WidgetName, TrackType, Target, PropertyName, Interp;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("animation_name"), AnimName)) return FMCPToolResult::Error(TEXT("animation_name is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			if (!Args->TryGetStringField(TEXT("track_type"), TrackType)) return FMCPToolResult::Error(TEXT("track_type is required"));
			if (!Args->HasField(TEXT("time_seconds"))) return FMCPToolResult::Error(TEXT("time_seconds is required"));
			Args->TryGetStringField(TEXT("target"), Target);
			Args->TryGetStringField(TEXT("property_name"), PropertyName);
			Args->TryGetStringField(TEXT("interpolation"), Interp);
			const double Time = Args->GetNumberField(TEXT("time_seconds"));
			if (Time < 0.0) return FMCPToolResult::Error(TEXT("time_seconds must be >= 0"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidgetAnimation* Anim = FindAnimation(WBP, AnimName);
			if (!Anim || !Anim->MovieScene) return FMCPToolResult::Error(FString::Printf(TEXT("Animation not found: %s"), *AnimName));

			FAnimTarget T; FString Err;
			if (!ResolveTarget(WBP, WidgetName, Target, T, Err)) return FMCPToolResult::Error(Err);
			FTrackSpec Spec;
			if (!BuildTrackSpec(TrackType, PropertyName, T, Spec, Err)) return FMCPToolResult::Error(Err);

			const FGuid Guid = FindOrAddBinding(WBP, Anim, T, /*bCreate*/ false);
			if (!Guid.IsValid())
				return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not bound in animation '%s'. Call add_widget_animation_track first."), *WidgetName, *AnimName));
			UMovieScenePropertyTrack* Track = FindPropertyTrack(Anim->MovieScene, Guid, Spec.PropertyName);
			if (!Track)
				return FMCPToolResult::Error(FString::Printf(TEXT("No %s track for '%s' in '%s'. Call add_widget_animation_track first."), *Spec.Kind, *WidgetName, *AnimName));
			if (Track->GetAllSections().Num() == 0)
				return FMCPToolResult::Error(TEXT("Track has no section"));
			UMovieSceneSection* Section = Track->GetAllSections()[0];

			const FFrameNumber Frame = SecondsToFrame(Anim->MovieScene, Time);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Widget Animation Key")));
			WBP->Modify();
			Anim->Modify();
			Anim->MovieScene->Modify();
			Section->Modify();

			TArray<FString> Keyed;
			FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();

			auto KeyFloatChannel = [&](int32 ChannelIndex, const TCHAR* ArgName, const TCHAR* Label)
			{
				if (!Args->HasField(ArgName)) return;
				FMovieSceneFloatChannel* Ch = Proxy.GetChannel<FMovieSceneFloatChannel>(ChannelIndex);
				if (!Ch) return;
				AddFloatKey(Ch, Frame, (float)Args->GetNumberField(ArgName), Interp);
				Keyed.Add(Label);
			};

			if (Spec.Kind == TEXT("Float"))
			{
				if (!Args->HasField(TEXT("value"))) { GEditor->EndTransaction(); return FMCPToolResult::Error(TEXT("value is required for this track")); }
				KeyFloatChannel(0, TEXT("value"), TEXT("value"));
			}
			else if (Spec.Kind == TEXT("Transform"))
			{
				// Channel order in UMovieScene2DTransformSection: Translation X/Y, Rotation, Scale X/Y, Shear X/Y
				KeyFloatChannel(0, TEXT("translation_x"), TEXT("translation_x"));
				KeyFloatChannel(1, TEXT("translation_y"), TEXT("translation_y"));
				KeyFloatChannel(2, TEXT("rotation"), TEXT("rotation"));
				KeyFloatChannel(3, TEXT("scale_x"), TEXT("scale_x"));
				KeyFloatChannel(4, TEXT("scale_y"), TEXT("scale_y"));
				KeyFloatChannel(5, TEXT("shear_x"), TEXT("shear_x"));
				KeyFloatChannel(6, TEXT("shear_y"), TEXT("shear_y"));
			}
			else if (Spec.Kind == TEXT("Color"))
			{
				KeyFloatChannel(0, TEXT("color_r"), TEXT("r"));
				KeyFloatChannel(1, TEXT("color_g"), TEXT("g"));
				KeyFloatChannel(2, TEXT("color_b"), TEXT("b"));
				KeyFloatChannel(3, TEXT("color_a"), TEXT("a"));
			}
			else if (Spec.Kind == TEXT("Margin"))
			{
				// UMovieSceneMarginSection: Left, Top, Right, Bottom
				KeyFloatChannel(0, TEXT("left"), TEXT("left"));
				KeyFloatChannel(1, TEXT("top"), TEXT("top"));
				KeyFloatChannel(2, TEXT("right"), TEXT("right"));
				KeyFloatChannel(3, TEXT("bottom"), TEXT("bottom"));
			}
			else if (Spec.Kind == TEXT("Visibility"))
			{
				FString Vis;
				if (!Args->TryGetStringField(TEXT("visibility"), Vis)) { GEditor->EndTransaction(); return FMCPToolResult::Error(TEXT("visibility is required for this track")); }
				const UEnum* VisEnum = StaticEnum<ESlateVisibility>();
				const int64 EnumValue = VisEnum ? VisEnum->GetValueByNameString(Vis) : INDEX_NONE;
				if (EnumValue == INDEX_NONE) { GEditor->EndTransaction(); return FMCPToolResult::Error(FString::Printf(TEXT("Unknown visibility '%s'"), *Vis)); }
				if (FMovieSceneByteChannel* Ch = Proxy.GetChannel<FMovieSceneByteChannel>(0))
				{
					Ch->GetData().AddKey(Frame, (uint8)EnumValue);
					Keyed.Add(TEXT("visibility"));
				}
			}
			else if (Spec.Kind == TEXT("Bool"))
			{
				bool bValue = false;
				if (!Args->TryGetBoolField(TEXT("bool_value"), bValue)) { GEditor->EndTransaction(); return FMCPToolResult::Error(TEXT("bool_value is required for this track")); }
				if (FMovieSceneBoolChannel* Ch = Proxy.GetChannel<FMovieSceneBoolChannel>(0))
				{
					Ch->GetData().AddKey(Frame, bValue);
					Keyed.Add(TEXT("bool_value"));
				}
			}

			if (Keyed.Num() == 0)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("No channel values provided for this track type"));
			}

			EnsureRangeCovers(Anim->MovieScene, Section, Frame);
			FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
			GEditor->EndTransaction();

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Keyed %s on %s.%s at %.3fs (%s) in '%s'"),
				*FString::Join(Keyed, TEXT(",")), *WidgetName, *Spec.PropertyPath, Time,
				Interp.IsEmpty() ? TEXT("Auto") : *Interp, *AnimName));
		});

	// ================================================================
	// list_widget_animations
	// ================================================================
	MCP_TOOL(Registry, "list_widget_animations")
		.Description(TEXT("List the animations of a Widget Blueprint with their length, bindings, tracks and key counts."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			TArray<TSharedPtr<FJsonValue>> Arr;
			for (UWidgetAnimation* Anim : WBP->Animations)
			{
				if (Anim) Arr.Add(MakeShared<FJsonValueObject>(DescribeAnimation(WBP, Anim)));
			}
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetArrayField(TEXT("animations"), Arr);
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%d animation(s) in %s"), Arr.Num(), *WBP->GetName()), Info);
		});

	// ================================================================
	// remove_widget_animation
	// ================================================================
	MCP_TOOL(Registry, "remove_widget_animation")
		.Description(TEXT("Delete a widget animation from a Widget Blueprint."))
		.Destructive()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("animation_name"), TEXT("Animation to delete"), true)
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, AnimName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("animation_name"), AnimName)) return FMCPToolResult::Error(TEXT("animation_name is required"));
			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidgetAnimation* Anim = FindAnimation(WBP, AnimName);
			if (!Anim) return FMCPToolResult::Error(FString::Printf(TEXT("Animation not found: %s"), *AnimName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Remove Widget Animation")));
			WBP->Modify();
			WBP->Animations.Remove(Anim);
			Anim->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
			Anim->MarkAsGarbage();
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
			GEditor->EndTransaction();

			SaveWidgetBlueprint(WBP, WantsSave(Args));
			return FMCPToolResult::Success(FString::Printf(TEXT("Removed animation '%s' from %s"), *AnimName, *WBP->GetName()));
		});
}

} // namespace MCPWidgetTools::Animation
