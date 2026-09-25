# Changelog

## [5.0.0-beta] — 2026-09-09

> **The verified-editing release, in beta.** v5 rebuilds the server around evidence: scoped permissions and session-owned state, an editor-wide visual observer, deterministic validators for every authoring family, typed patches with revisions, owned operations for long work, and honest performance and generation boundaries. 21 implementation increments, each validated on UE 5.8.2 (Win64 Development Editor) with a fixture project and a 40-test headless suite; the live catalog exports **519 tools**. Beta remains set: see [release status](../validation/RELEASE_STATUS.md) for the open release-checklist items.

### Added

- **Permissions and ownership.** Read / Scene / Destructive scopes on every entry point including nested calls, imported providers and resource templates; principal+session ownership of plans, snapshots, observers, frames, verification records, operations and bundles; `-32002` for foreign or missing owned resources; `resources/templates/list`.
- **Editor surfaces and observers.** `list_editor_surfaces`, `focus_editor_surface`, `capture_editor_surface` (Slate capture with frame identity, DPI and geometry), leased region observers (`start/get/stop_editor_observation`), retained frames as `unreal://visual/frames/{frame_id}`, `record_visual_verification` / `list_visual_verifications` and the `observe_edit_verify` prompt.
- **Validation.** `validate_widget_layout`, structured `validate_blueprint`, `validate_animation_setup`, `validate_material_setup`, `validate_niagara_system`, `validate_sound_setup`, `validate_pcg_graph`, enhanced `validate_assets`, `preview_asset_import`, `get_asset_instance_impact`, all read-only with a shared `issues[]` shape.
- **Typed patches.** `apply_widget_patch` and `apply_blueprint_patch` with preview, revision fingerprints, aliases, atomic preflight and read-back; `plan_actor_transform` / `get_change_plan` / `apply_change_plan`.
- **References, snapshots, worlds.** `resolve_object_refs` (`actor:<world>:<guid>`), `capture_editor_snapshot` / `get_editor_snapshot` / `compare_editor_snapshots` (`unreal://snapshots/{id}`), `list_worlds`, `get_world_partition_info` with actor-descriptor summaries and bounded listings.
- **Operations.** `run_editor_scenario` (tick-driven PIE with cooperative cancellation), `get/cancel/list_editor_operations` including legacy tasks, driver-based operations, `measure_frame_times`, `submit_generation_job` (asynchronous fal queue or `mock` provider; credential isolation; validated downloads; honest provider cancellation).
- **Diagnostics.** `get_server_health` (tool timing telemetry at diagnostic detail), `get_server_capabilities`, `export_diagnostic_bundle` with redaction and SHA-1 manifests (`unreal://bundles/{bundle_id}`); provenance and run metadata on all performance reports.
- **Python boundary.** `execute_python` runs code as a script file with `MCP_DATA` transport and structured results; plugin scripts resolve through the plugin manager.
- **Docs and packaging.** Generated tool reference, packages with per-file checksums and build identity, getting-started guide.
- **Also new.** After-operation readiness via the Slate paint sequence, observer modes (`on_change`, `after_operation`), retained captures and pixel comparison on `compare_editor_snapshots`; honest `initialize` capabilities with the `tasks/*` extension and bounded paged results (`get_result_page`); per-tick scheduler budget; `plan_actor_changes` / `revert_change_plan` with an effect journal; `run_pcg_generation`; import `collision_policy`; reference-aware `rename_asset`; authenticator policy matrix.

### Changed

- Registry transactions are skipped while PIE is active (`undo_recorded=false`) after the transaction-buffer crash fix.
- Duplicate widget names are refused before construction; batch widget operations preflight atomically.
- The old execute-and-cancel preview is removed; previews are explicit non-mutating handlers.
- Performance tools label estimates as estimates; `estimated_cost` is a unitless score.

### Known limitations

See KNOWN_LIMITATIONS.md.

## [4.6.2] — 2026-09-06

> **The UMG "designer-only" release.** An agent reported *"I've hit the same 'Is Variable' limitation on this widget as with the HealthBar — it's not accessible via Python or the MCP tools."* It was right: `UWidget::bIsVariable` is a plain `UPROPERTY()` that `set_editor_property` refuses, and nothing in the plugin exposed it. The audit that followed found a whole class of state the UMG designer can edit but neither Python nor the tools could reach — renames, sub-widget instancing, reparenting, property bindings, animations, designer flags — plus three defects in the shipped widget tools. All of it is closed here. Widget category: 15 → **39 tools**, CommonUI 4 → 5. Registry: 455 → **480 tools**. All engine APIs verified against the installed UE 5.8 headers; the 5.8 `FWidgetBlueprintOperationUtils` editor-free helpers (rename, wrap, replace, index-aware add/move, bound-event creation, WBP instancing) are used wherever they exist so the tools do exactly what the designer does.

### Fixed — defects in shipped widget tools

- **`add_widget` / `move_widget` ignored `index`.** Both declared and documented the argument and never read it; widgets always appended. Now routed through `FWidgetBlueprintOperationUtils::AddWidget` / `MoveWidget` (honours the index, handles an empty tree by making the widget the root, refuses parent/child cycles, fires `OnVariableAdded`). `batch_add_widgets` accepts `index` per entry.
- **`bind_widget_event` ignored `function_name`.** The argument was read into a local and never used although the description promised a call. It now wires the event's exec pin to a `CallFunction` node for that function, creating a custom event of that name first when none exists (skeleton regenerated in between so the call resolves). The bound event itself is now created with the engine's `BindToEventProperty` / `CreateNewBoundEventForClass` path instead of a hand-built node, the widget is promoted to a variable when needed (the designer's `+` does the same), re-runs reuse the existing node, and the result carries compile results.
- **`bind_widget_event` rejected most events.** `event_name` was a 9-value enum although the handler resolved any delegate by reflection, so ListView `OnItemClicked`, ComboBox `OnSelectionChanged` etc. failed schema validation. It is a free string now; the error lists the delegates the widget actually has.
- **Every widget mutator compiled and saved to disk on each call, swallowing compile errors.** All mutators take `save` (default true); pass false while batching and finish with `compile_widget_blueprint`. Compile messages are no longer discarded (see `compile_widget_blueprint`, and the structured result of `bind_widget_event`, `bind_widget_property`, `reparent_widget_blueprint`).

### Fixed — "Is Variable" (and the same class of state) was unreachable

- **`is_variable`** on `add_widget`, `batch_add_widgets`, `set_widget_properties`, `batch_set_widget_properties`; reported by `get_widget_properties` / `get_widget_tree`. Uses `FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable` + structural modification, the designer checkbox's path.
- **`set_widget_property` / `get_widget_property` / `list_widget_properties` / `set_widget_slot_property`** — generic reflection access with nested paths and Unreal text-import syntax. Covers the long tail (CheckBox state, Slider range, hint text, font typeface, button style colours, switcher index, designer flags, `bIsVariable`) that previously forced `execute_python`; `list_widget_properties` makes the names discoverable. `batch_set_widget_properties` accepts `{"property","value"}` per operation.
- **`set_widget_designer_flags`** (hidden / locked / expanded in designer) and **`set_widget_blueprint_defaults`** (UserWidget CDO properties + design-time preview size/mode).

### Added — tree operations the designer has

- **`rename_widget`** — `FWidgetBlueprintOperationUtils::RenameWidget`: member variable, get/set nodes, bound events, property bindings, animation bindings and navigation rules are all retargeted. (Renaming the UObject from Python silently breaks all of them.)
- **`wrap_widget`** ("Wrap With…"), **`replace_widget`** ("Replace With…", plus `replace_with_child`), **`duplicate_widget`** (export/import text round-trip, i.e. copy/paste, with slot layout re-applied).
- **`add_widget` / `batch_add_widgets` `widget_class_path`** — instance another Widget Blueprint inside a tree (`CreateWidgetFromAsset`, with self/circular-reference guards). **`set_named_slot_content`** fills or lists the named slots a sub-widget instance exposes.
- **`set_widget_slot`** now covers Grid (row/column/spans/layer), UniformGrid, WrapBox (fill flags), ScrollBox, SizeBox, Border, ScaleBox, WidgetSwitcher and StackBox slots; anything else via `set_widget_slot_property`.

### Added — class, bindings, verification

- **`create_widget_blueprint` `parent_class`** and **`reparent_widget_blueprint`** — accept `/Script/Module.Class`, a short native class name or another WBP; reparent refreshes nodes, recompiles and returns the messages.
- **`bind_widget_property` / `list_widget_bindings` / `remove_widget_binding`** — `FDelegateEditorBinding` authoring (function or variable kind, member GUIDs resolved the way `UMGDetailCustomizations` does); the Blueprint is compiled immediately so a type mismatch is visible in the result.
- **`compile_widget_blueprint`** — error/warning counts, messages, and the widget / animation member variables present on the generated class. The closed loop for `is_variable`, bindings and reparenting.
- **`set_widget_properties`** takes `text_namespace` / `text_key` so `text` / `tooltip_text` can be localizable instead of `FText::FromString` literals.

### Added — widget animations (5 tools)

- **`create_widget_animation`** (mirrors `AnimationTabSummoner::OnNewAnimationClicked`: `UWidgetAnimation` + `UMovieScene`, display rate, playback range, registered as a member variable), **`add_widget_animation_track`** (Opacity, Transform, Color, Visibility, Margin, Float, Bool; binds the widget, its slot, or the user widget itself, exactly as `UWidgetAnimation::BindPossessableObject` shapes bindings), **`add_widget_animation_key`** (per-channel keys with Auto/Linear/Constant interpolation; sections and playback range grow to fit), **`list_widget_animations`**, **`remove_widget_animation`**.

### Added — runtime verification and CommonUI

- **`pie_add_widget_to_viewport` / `pie_remove_widget` / `pie_list_widgets`** — put a WBP on screen in the running PIE session and `pie_screenshot` it, without authoring a graph first.
- **`create_common_ui_widget`** rewritten in C++ (`FWidgetBlueprintOperationUtils::CreateWidgetBlueprint`; no Python bridge), any CommonUI or game `UUserWidget` subclass as base, `root_widget_type`. **`create_common_ui_style`** creates CommonButtonStyle / CommonTextStyle / CommonBorderStyle Blueprint assets.

## [4.6.0] — 2026-08-29

> **The animation release.** v4.5 could *create* animation assets but not *author* them: a montage had no sections, a state machine state had no pose, a notify could not carry a class or a duration, and a level sequence could not hold an animation clip at all. v4.6 closes that loop — 49 new tools across montage authoring, notify/curve/marker data, AnimGraph node authoring, and skeletal animation in Sequencer — and fixes the defects that made the v4.5 animation tools produce assets that compiled but did nothing. Registry: 406 → 455 tools. All APIs verified against the installed UE 5.8 engine headers.

### Fixed — duplicate asset names crashed the editor; rollback lied about what it undid

- **`create_*` asserted on a name that was already taken.** Every asset-creating tool ran the same unguarded `CreatePackage` → factory sequence, which has two failure modes. If the existing asset was **loaded**, `CreatePackage` returned the live package and `FKismetEditorUtilities::CreateBlueprint` (reached through `UBlueprintFactory::FactoryCreateNew`) hit its `check(FindObject<UBlueprint>(...) == nullptr)` — a hard editor crash, not a recoverable tool error. If the asset was on disk but **not loaded**, nothing asserted and the subsequent `SavePackage` silently overwrote the existing `.uasset`. Both are now refused up front with a structured `already_exists` error. 19 call sites across Blueprint, AnimGraph, Widget, Material, Physics, AI, GAS, GameFramework, EnhancedInput and Sequencer tools moved onto a single guarded helper, **`MCPCommon::CreateAssetPackage`**; `FMCPValidate::AssetDoesNotExist` (which existed but was applied by only a handful of newer tools) now checks in memory as well as on disk, so an asset created earlier in the session whose save failed is caught too.
- **`.Idempotent()` dropped from `create_input_action`, `create_input_mapping_context`, `create_behavior_tree`, `create_blackboard`, `create_eqs_query`.** The annotation was never true: a second call used to overwrite the asset, and now returns `already_exists`.
- **`run_tool_script` reported `rolled_back: true` for scripts whose assets survived.** `GEditor->CancelTransaction` reverts `Modify()`-recorded property and scene edits; it does not delete packages, and `create_blueprint` never opened a transaction or called `Modify()` in the first place. The result claiming a clean rollback was the setup for the crash above — an agent told the asset was gone retries the same name. Failures now report `rollback: "full" | "partial"`, set `rolled_back` only when the rollback really was complete, and list surviving asset paths in `assets_not_rolled_back`. The tool description says so too, rather than promising that "the whole transaction rolls back".
- **`transactions/rollback` had the same defect** and now returns the same three fields (`rollback`, `rolledBack`, `assetsNotRolledBack`). `FMCPTransactionManager::Rollback` takes an optional out-param for the asset paths created while the transaction was open.
- Both rely on a new creation journal in `MCPCommon` that every guarded creation path records into — including the eight tools that already rolled their own existence check (Chaos, MetaSound, PCG, ControlRig, DataTools, MontageTools, SequencerAnimationTracks), so nothing reports a clean rollback while its asset is still on disk.

### Fixed — the v4.5 animation tools produced non-functional assets

- **State machines evaluated to a T-pose.** `add_anim_state` created a `UAnimStateNode` whose `BoundGraph` held only the schema's default Result node; nothing ever put a pose into it, and no tool could. Every state machine this plugin authored rendered as reference pose. Fixed by the new **`set_anim_state_animation`**, which places a Sequence Player or Blend Space Player in the state and wires it to the Result node. `add_anim_state` now says so in its description and its result.
- **Transitions could never fire.** `add_anim_transition` hand-wired pins by scanning for the first input/output it found instead of calling the engine's `UAnimStateTransitionNode::CreateConnections()`, and never authored the rule graph — so `bCanEnterTransition` stayed at its default of `false` and the transition was permanently untakeable. Neither failure surfaced as a compile error. Fixed by the new **`set_anim_transition_rule`**; `add_anim_transition` now uses `CreateConnections`, refuses duplicate edges, and reports that its rule is still empty.
- **`add_anim_notify` wrote malformed notifies.** No `Modify()` (so the edit escaped the registry transaction and `run_tool_script` could not roll it back); no `FAnimNotifyEvent::Link()` (so the notify was never bound to the sequence's frame rate, or on a montage to the segment containing that time); no `SortNotifies()` / `RefreshCacheData()` (so `AnimNotifyTracks` and the compiled notify data drifted from `Notifies[]`); `track_index` written unchecked against the existing tracks; and a forced `UPackage::SavePackage` on every call that defeated both the transaction and any attempt to batch edits. Now routed through `UAnimationBlueprintLibrary` with the index validated against real tracks.
- **`create_anim_montage` produced a montage with an invisible slot.** It set `SlotAnimTracks[0].SlotName` directly and never called `USkeleton::RegisterSlotNode`, so the slot never appeared in the skeleton's slot groups and no AnimGraph Slot node could select it — the montage played into nothing. It now registers the slot, and takes `blend_in_time` / `blend_out_time`.
- **`list_animation_assets` loaded every animation in the project.** It contained a dead `if (bFilterBySkeleton || true)` branch that forced a full `GetAsset()` on every AnimSequence and AnimMontage under the search path just to read a duration. Both it and `list_anim_assets_by_skeleton` (whose description already promised registry-only enumeration) now filter on the Asset Registry's `Skeleton` tag first — the same query Persona's own asset pickers use — and load only the assets that survive.
- **Blend space grids were not rebuilt after the axis ranges changed.** `create_blend_space` and `create_aim_offset` wrote `BlendParameters` and never called `ResampleData()`, so the asset kept the grid built for the factory's default ranges and later samples landed in the wrong cells. `add_blend_space_sample` now also calls `ValidateSampleData()`, which flags degenerate samples `AddSample` accepts silently.
- **`get_control_rig_info` returned no data.** It ran Python that logged `MCP_CR_INFO:{json}` to the Output Log and then returned the literal string *"Control Rig info logged. Check Output Log"* — the agent received nothing it could act on, and `create_control_rig` trusted `ExecPythonCommand`'s bool without confirming the asset existed. Both rebuilt on the result-file pattern already used by the IK retargeting tools; `get_control_rig_info` now returns the rig hierarchy (bones, controls, nulls, curves) and `create_control_rig` verifies the asset on the C++ side.
- **State machine lookup was a substring match.** `add_anim_state` / `add_anim_transition` resolved `machine_name` with `NodeTitle.Contains()`, so `Loco` matched a machine named `Locomotion`. Now exact (graph name or state machine name), matching the v4.6 tools — otherwise a substring hit would silently edit a different machine than the follow-up calls. Relatedly, `get_anim_state_machine_info` now reports the bound graph's name as `name` (the value every `machine_name` argument resolves against) with the node's display title alongside as `display_title`.
- **AnimGraph node creation is now engine-canonical.** `create_anim_state_machine`, `add_anim_state` and `add_anim_transition` used hand-rolled `NewObject` + `AddNode` + `PostPlacedNewNode` + `AllocateDefaultPins`. Both `UAnimGraphNode_StateMachineBase` and `UAnimStateNode` allocate their bound graphs inside `PostPlacedNewNode` behind a `check(… == NULL)`, so the order matters; all three now use `FGraphNodeCreator`, the sequence the engine's own state machine schema uses.
- **No AnimGraph edit was ever compiled.** Every AnimGraph tool stopped at `MarkBlueprintAsStructurallyModified`, so an agent could not verify its own work — the v4 closed-loop principle. **`compile_anim_blueprint`** now reports error/warning counts and the compiler messages, and returns a failed compile as a structured error carrying them.

### Added — montage authoring (14 tools, `Montage` category)

`montage_get_sections`, `montage_add_section`, `montage_remove_section`, `montage_set_section_time`, `montage_link_sections`, `montage_add_slot`, `montage_add_segment`, `montage_remove_segment`, `montage_set_blend_settings`, `montage_set_blend_profile`, `montage_set_sync_group`, `montage_set_rate_scale`, `montage_create_from_sections`, `montage_validate`.

- Sections and their chaining (`NextSectionName`) — the mechanism behind combos and looping montages — over `AddAnimCompositeSection` / `DeleteAnimCompositeSection`. A section's END is implicit (the next section's start), so every section edit reports the whole recomputed layout.
- **Sections are kept sorted by time.** `UAnimMontage::SortAnimCompositeSectionByPos()` is private and `AddAnimCompositeSection()` only appends, yet `GetSectionStartAndEndTime()` derives a section's end from `CompositeSections[Index + 1]` — i.e. it assumes time order. Left unsorted, every section length past an insertion point is wrong. v4.6 sorts after each structural edit and re-resolves by name (the sort invalidates the index `AddAnimCompositeSection` returned).
- Section times are set with `FAnimLinkableElement::Link()` rather than `SetTime()`, so the section is re-bound to whichever segment now contains the new time.
- `montage_add_slot` registers the slot on the skeleton and files it under a slot group.
- `montage_create_from_sections` builds a complete multi-section combo in one call — segments, one section per clip, and the chaining — with all-or-nothing source resolution so a typo in the third path leaves nothing on disk.
- `montage_validate` reports dangling `next_section` links, no section at time 0, unregistered slots, empty slot tracks, segments on the wrong skeleton, notifies past the montage end, and blend-out longer than the montage. Read-only: it reports, never repairs.

### Added — notifies, curves and sync markers (16 tools, `AnimData` category)

`anim_add_notify`, `anim_add_notify_state`, `anim_remove_notify`, `anim_move_notify`, `anim_copy_notifies`, `anim_add_notify_track`, `anim_remove_notify_track`, `anim_list_notify_tracks`, `anim_add_curve`, `anim_remove_curve`, `anim_add_float_curve_keys`, `anim_get_curve_keys`, `anim_set_curve_metadata`, `anim_add_sync_marker`, `anim_list_sync_markers`, `anim_remove_sync_markers`.

- All backed by **`UAnimationBlueprintLibrary`** (`Source/Editor/AnimationBlueprintLibrary`), the engine's own editor-side animation editing surface — new Build.cs dependency, previously unlinked.
- **`anim_add_notify_state`** is the one that was missing entirely: notifies with a duration (Begin/Tick/End) are what combo input windows, invulnerability frames and weapon hit-traces are built from. Overlap on a track is rejected up front, since overlapping states corrupt Begin/End pairing at runtime.
- The library's `Add*` functions are **silent on bad input** — an unknown notify track name logs a warning and adds nothing, with no signal to the caller. Every tool here pre-validates the track and the time and turns a failure into a structured error naming the valid options, then verifies the notify count actually changed.
- A class-less notify is created by the engine with `NotifyName = NAME_None`; v4.6 sets the name afterwards, since that name is what the AnimBP binds `AnimNotify_<name>` to.
- `anim_set_curve_metadata` flags a curve name on the *skeleton* as driving a morph target and/or material parameter — the mechanism that makes a float curve do something with no Blueprint wiring (the basis of facial animation).
- `anim_remove_sync_markers` calls `RefreshSyncMarkerDataFromAuthored()`: `UniqueMarkerNames` is derived from `AuthoredSyncMarkers` and `RefreshCacheData()` does *not* rebuild it, so without this `GetUniqueMarkerNames` keeps reporting names with no markers left.

### Added — AnimGraph node authoring (12 tools, `AnimGraphNodes` category)

`animgraph_add_node`, `animgraph_connect_pose`, `animgraph_describe`, `animgraph_set_node_property`, `animgraph_list_node_types`, `set_anim_state_animation`, `set_anim_transition_rule`, `set_anim_transition_settings`, `set_anim_state_machine_entry`, `remove_anim_state`, `remove_anim_transition`, `compile_anim_blueprint`.

- **The AnimGraph was previously unreachable.** The engine exposes ~130 `UAnimGraphNode_*` classes and none were addressable, so a created state machine could never be connected to the Output Pose node. `animgraph_add_node` takes any of them by class name; `animgraph_connect_pose` wires pose pins (with `to_node: "root"` resolving to the graph's Output Pose or a state's Result node); `animgraph_describe` reads the whole topology back.
- Pose pins are identified via `UAnimationGraphSchema::IsPosePin` rather than by name, so multi-input nodes (LayeredBoneBlend's `BasePose`/`BlendPoses_0`, ApplyAdditive's `Base`/`Additive`) work; the error names the node's actual pose pins when the pin is ambiguous.
- `animgraph_set_node_property` searches the node object *and* its inner `FAnimNode_*` struct, because nearly every anim node setting (a Slot node's `SlotName`, a SequencePlayer's `PlayRate`) lives in the struct — so callers pass the bare name.
- `set_anim_transition_rule` supports five rule shapes: `Always` / `Never` (result pin default), `BoolVariable` (a validated bool variable on the AnimBP, with `invert`), `CurveValue` (`GetCurveValue` → `Greater_DoubleDouble` → result), and `Automatic` (`bAutomaticRuleBasedOnSequencePlayerInState`, the engine's own "state's sequence is nearly done" test). Re-authoring a rule clears the previous graph content rather than accumulating orphan nodes.
- Node creation goes through `FGraphNodeCreator` throughout — never hand-rolled `NewObject` + `PostPlacedNewNode`, the pattern behind the v4.5 `add_spawn_actor_node` editor crash.
- Blend profiles are set via `FBlendProfileInterfaceWrapper::SetSkeletonBlendProfile()`; the wrapper's `BlendProfile` member is private in 5.8.

### Added — skeletal animation in Sequencer (7 tools, `SequencerAnimation` category)

`add_animation_track`, `add_animation_section`, `list_animation_sections`, `set_animation_section_params`, `remove_animation_section`, `bake_sequence_to_anim_sequence`, `link_anim_sequence_to_sequence`.

- **Sequencer previously had no animation track at all** — `add_sequence_track` accepted only `Transform` and `Visibility`, so a level sequence could move a character but never animate one. `UMovieSceneSkeletalAnimationTrack` is in `MovieSceneTracks`, already a dependency.
- Sections carry trim offsets, play rate, montage slot routing, and a mirror data table; clips on separate rows overlap and blend.
- Bindings resolve from either an actor label (auto-bound, and rejected with a clear reason if the actor has no `USkeletalMeshComponent`) or a binding GUID. Clips are skeleton-checked against the bound actor's mesh.
- `bake_sequence_to_anim_sequence` evaluates everything driving the mesh — animation sections, Control Rig, transform tracks — into a new AnimSequence, via `USequencerToolsFunctionLibrary::ExportAnimSequence`. Long-running (pollable). `link_anim_sequence_to_sequence` sets up the association for a clip baked earlier so the bake can be repeated.
- **5.8 API change handled:** `FMovieSceneSkeletalAnimationParams::PlayRate` is no longer a float — it is an `FMovieSceneTimeWarpVariant` that can hold a constant rate or a whole time-warp curve. Reads check `GetType() == EMovieSceneTimeWarpType::FixedPlayRate` before calling `AsFixedPlayRate()`; writes go through `Set(double)`.

### Added — shared animation helper layer

New `Private/Tools/Animation/AnimCommon.{h,cpp}`: skeleton-compatibility checks that name both skeletons and point at the retargeting tools, montage section/slot resolution with did-you-mean lists, notify-track resolution, pose-pin lookup and description, AnimBP graph navigation (AnimGraph, state machines, states, transitions — all exact-match with the available names listed on a miss), compile-and-report, and blend profile resolution. It exists because the engine's animation edit APIs are overwhelmingly *silent* on bad input; pre-validating is what turns them into errors an agent can recover from.

### Added — workflow prompts (18 → 21)

- **`montage_authoring`** — source clips → create (single or multi-section) → slot and its AnimGraph Slot node → sections and flow → notify states for gameplay windows → blend feel → `montage_validate`.
- **`locomotion_state_machine`** — leads with the two silent failure modes (empty states, ruleless transitions), then variables → machine → states with animations → transitions with rules → entry state → connect to Output Pose → compile → PIE verify.
- **`sequencer_animation`** — animation track → clips → per-section trim/retime/mirror/slot → verify → bake back to a gameplay-usable AnimSequence.

### Changed — packaging & plumbing

- `EngineVersion` 5.8.0; `VersionName` 4.6.0; `uplugin` Version 9; `MCPProtocol::ServerVersion` 4.6.0; description rewritten around the animation headline.
- New Build.cs dependencies: `AnimationBlueprintLibrary`, `AnimationCore`, and (in the 5.8 block) `SequencerScriptingEditor`. New `.uplugin` plugin dependency: `SequencerScripting` (engine-shipped, same treatment as `MovieSceneAnimMixer`).
- Four new settings toggles with token estimates: `bEnableMontageTools`, `bEnableAnimDataTools`, `bEnableAnimGraphNodeTools`, `bEnableSequencerAnimationTools`.
- Catalog core tools gain `montage_get_sections`, `animgraph_describe` and `compile_anim_blueprint` — the read-back and verify half of animation authoring, which would otherwise cost a `search_tools` round trip on every edit loop.

### Not built, by design

- **Blend spaces, pose assets, mirror data tables, anim composites** (planned Phase 4), **PIE runtime animation debugging** (Phase 6), and the **native IK Rig / retargeter port** (Phase 7) are scoped but not in 4.6. The IK and retargeting tools remain Python-backed, and `retarget_animations` still relies on a `/Game/<basename>` output heuristic: it calls `duplicate_and_retarget`, then assumes the results land at `/Game/<basename>` and renames them into place, so a name collision can silently retarget nothing.
- **Transition rules based on remaining time** are not offered as a rule template: the engine reaches `GetRelevantAnimTimeRemaining` through `UK2Node_TransitionRuleGetter`, which the compiler injects. The `Automatic` rule type is the supported equivalent and produces better results.

## [4.5.0 hotfix] — 2026-07-08

### Fixed
- **`lighting_set_megalights` always failed** — it set `r.MegaLights.Enable`, a console variable that does not exist in any engine version; the real CVar is `r.MegaLights.EnableForProject` (verified against engine source). The tool now works.
- **`iris_get_status` reported nonexistent CVars** — `net.Iris.AsyncLoading`, `net.Iris.DefaultRootObjectNetUpdateFrequency`, and `net.Iris.Attachments` are not real; replaced with `net.Iris.UseIrisReplication` (the actual enable flag), `net.Iris.DeltaCompressInitialState`, and `net.Iris.Attachments.AllowSendPolicyFlags`.
- **`substrate` status listed nonexistent `r.Substrate.Experimental`** — removed.

## [4.5.0] — 2026-06-22

> **The v4.5 flagship — UE 5.8 native.** Shipped as a separate engine-targeted tree (`UnrealMCPServer_5.8/`); the 5.7 `4.0.0` tree is frozen as a stable baseline. v4.5 ports to Unreal Engine 5.8, closes every remaining tool stub, adds six new 5.8 feature families, upgrades the transport with real Server-Sent-Events streaming and per-token scopes, and interoperates with Epic's new first-party MCP plugin. All UE APIs were verified against the installed 5.8 engine headers.

### Context — why a 5.8 line now
- **UE 5.8 ships Epic's own experimental MCP plugin** (`ModelContextProtocol`, Claude/Gemini/Cursor/Codex over HTTP+SSE). It is intentionally thin: a handful of toolsets, no auth, no resources, no prompts. v4.5 leans into that gap (450+ tools, scope/auth, catalog mode, tasks, transactions, dry-run, tests, resources, prompts) **and** imports Epic's toolsets so a single endpoint serves both.
- **5.8 is the last major UE5 release** before UE6; it carries real breaking changes (MetaSound `FClassInterface` stabilization, WASAPI default audio, unified gizmo, StateTree compiler) — handled in the port.

### Added — closed every prior stub (Phase 1)
- **MetaSound graph mutators (5 tools) implemented** — `metasound_add_node / remove_node / connect_pins / disconnect_pin / set_node_property` now edit the live document via `FMetaSoundFrontendDocumentBuilder` (unblocked by 5.8's stable `FClassInterface`). Edges go through `CanAddEdge` + `AddEdge`; node defaults via `FMetasoundFrontendLiteral`. New Build.cs deps: `MetasoundFrontend/GraphCore/Engine/Editor`.
- **`modeling_polyextrude` per-face selection** — honors a `face_indices` array (`ConvertIndexArrayToMeshSelection`), with range validation; omit to extrude all faces.
- **`chaos_add_field`** — spawns an `AFieldSystemActor` with a persistent construction field (`URadialFalloff` external-strain or `UUniformVector` linear-force) via `AddFieldCommand`. New dep: `FieldSystemEngine`.
- **`chaos_create_cloth_asset`** — creates a real `UChaosClothAsset` through the engine's scripted `UChaosClothAssetFactory::CreateClothAssetFromTemplate`; the sim-mesh Dataflow step is documented in the result. New deps: `ChaosClothAsset(Engine)`.
- **PIE** — `pie_start` now applies `num_players` / window size via `ULevelEditorPlaySettings`; input deprecation resolved (already on `FInputKeyEventArgs::CreateSimulated`).
- `metahuman_import` — kept as an honest, accurate limitation: by-id Quixel Bridge import is interactive in 5.8 (no scriptable entry); the tool points at the scriptable MetaHuman Generator path + `metahuman_list_assets`/`metahuman_attach_to_skeletal_mesh`.

### Added — new 5.8 feature families (Phase 2)
- **Lighting** (`MCPLightingTools`) — `lighting_set_megalights` (MegaLights, production-ready in 5.8), `lighting_set_lumen`, `lighting_get_settings`, via `IConsoleManager`.
- **Morph Targets** (`MCPMorphTargetTools`) — `morph_list_targets / set_weight / get_weights / clear` (complements 5.8's expanded Skeletal Editor morph tooling).
- **Animation Mixer** (`MCPAnimMixerTools`) — `animmixer_add_track / add_layer / add_animation / get_info` over `MovieSceneAnimMixerScripting`.
- **Gizmo** (`MCPGizmoTools`) — `gizmo_set_mode / set_coordinate_system / get_state` via `FEditorModeTools`.
- **Substrate** (`MCPSubstrateTools`) — `substrate_get_status` (AxF/Toon authoring note).
- **Iris** (`MCPIrisTools`) — `iris_get_status` (production-ready replication in 5.8).
- **Not built, by design (faithful):** Mesh Terrain (`MeshTerrainMode` exports no scriptable API — interactive editor mode only) and deep PCG 5.8 extensions (existing PCG tools cover the core).

### Added — workflow prompts (14 → 18)
- Four new built-in prompts for the 5.8 features: **`metasound_graph`** (author a MetaSound Source graph node-by-node with the new mutators), **`animation_mixer`** (layer animation in Sequencer), **`substrate_material`** (Substrate authoring, with an enabled-check), **`lighting_setup`** (MegaLights / Lumen against a frame budget).

### Added — transport & security (Phase 3)
- **Real Server-Sent-Events streaming** — `GET /mcp` now holds an open `text/event-stream` per session and pushes `notifications/progress` live, using 5.8's new `FHttpServerResponse` streaming body (`StreamingBodyQueue`/`StreamingBodyComplete` + `MultipleWriteStream`). The registry chains progress sinks so long-running task progress reaches **both** the SSE stream and `get_task_status`. (Was impossible on 5.7's single-response HttpServer.) Streams are cleaned up on DELETE, idle sweep, and shutdown.
- **Per-token scopes** — `AuthTokenScopes` maps a bearer token to `read` / `scene` / `destructive`; resolved in `MCPAuth`, still capped by the global `bAllowDestructiveScope`. Backward compatible.

### Added — interop with Epic's first-party MCP (Phase 4)
- **`MCPToolsetAdapter`** imports tools registered with the engine's `ModelContextProtocol` module (`IModelContextProtocolModule::GetTools`) into this registry as `epic_<name>`, converting `FModelContextProtocolToolResult` → our result type. Soft dependency (runtime `FModuleManager`, no link) — a clean no-op when Epic's plugin is disabled. Epic tools thereby inherit our catalog mode, scopes, tasks, transactions, resources and prompts.

### Changed — packaging
- `EngineVersion` 5.8.0; `VersionName` 4.5.0; `uplugin` Version 8; `MCPProtocol::ServerVersion` 4.5.0. New optional plugin deps (`Metasound`, `ChaosClothAssetEditor`, `MetaHuman`, `Bridge`). New per-category settings toggles for all six new families + the Epic-interop import.

## [Unreleased]

### Fixed — editor crashes (confirmed via crash-dump callstacks)
- **`add_spawn_actor_node` crashed the editor 100% on UE 5.7**: `UK2Node_SpawnActorFromClass::PostPlacedNewNode()` now reads the ScaleMethod pin via `FindPinChecked()`, but `FGraphNodeCreator::Finalize()` runs `PostPlacedNewNode()` *before* `AllocateDefaultPins()` — zero pins → `check()` assert (`EdGraphNode.h:586`). Replaced with the engine-canonical `UBlueprintNodeSpawner::Invoke()` order (pins first, then PostPlacedNewNode, then AddNode). All four graph-tool crash dumps from field testing were this one bug; `add_custom_event` and `add_variable_get_node` were exonerated by the callstacks.
- **`set_component_property` tripped the engine's `KnownStaticMesh` ensure** ("StaticMesh property overwritten without a call to NotifyIfStaticMeshChanged"): raw `ImportText_Direct` writes bypassed owner notification. New `MCPCommon::SetObjectPropertyWithNotify()` (Modify + PreEditChange + import + PostEditChangeProperty) now routes the write, fixing every state-caching property, not just StaticMesh.
- `add_custom_event` hardened: engine-canonical creation order, `RF_Transactional`, `Graph->Modify()` for correct `run_tool_script` rollback, and a duplicate-event-name guard (same-name custom events put the Blueprint in a compile-error state).

### Changed — settings accuracy & safety cleanup
- **Removed `bEnableDestructiveOperations`** — it was never read anywhere, so unchecking it silently did nothing (the same false-security trap as the old `bAllowRemoteConnections`). The enforced gate is `bAllowDestructiveScope` + per-token scopes, now surfaced in the Safety category.
- **`bEnableConsoleCommands` is now actually enforced** — checked inside `run_console_command` at execution time (takes effect without restart; also covers `run_tool_script` routing).
- Tool preset / exposure tooltips rebuilt from the live 4.0.0 registry: Full = 378 tools across 54 categories (~62K tokens listed up front), Scene Building ≈ 169, Gameplay ≈ 205, Minimal ≈ 39, Catalog exposure = 31 tools (~3K). 17 stale per-category counts corrected (Blueprint said 7 tools; it has 60). Added missing tooltips (port, autostart, rate limit, verbose logging) and a plaintext warning on `FalAIApiKey`.

## [4.0.0] — 2026-06-04

> **The v4 release** — agent-native: catalog mode cuts a fresh session's tool-definition cost ~95%, multi-step tool scripts run as one transaction, long operations become pollable tasks, full graph read-back closes the edit-verify loop, structured outputs everywhere, and a registry-wide test matrix guards all of it. Executes docs/V4_IMPLEMENTATION_PLAN.md Phases 0-5.

*(The sections below consolidate the 4.0.0-alpha.1 / alpha.2 development line.)*

### Added — prompts & packaging (Phase 5)
- **3 new workflow prompts** (14 total): `pcg_workflow` (graph creation → node wiring → execution → density iteration with instance budgets), `statetree_design` (hierarchy sketch → build → verify → pawn wiring, with Behavior-Tree fallback when the plugin is absent), `chaos_destruction` (collection → fracture-method guide → PIE force test, with fragment budgets).
- **`debug_performance` rewritten as a real triage runbook**: per-thread frame budgets (game/render/GPU), triage paths per bound thread, desktop budget numbers (draw calls ≤3K, shadow lights ≤4), one-change-at-a-time discipline with baseline/final reporting.
- **System prompt v4 refresh**: new "v4 Capabilities" section teaching agents the task-polling flow, run_tool_script semantics (incl. the creation-survives-rollback caveat), closed-loop verification habits, structured-result/error usage, and the real mesh-editing tools.
- **Packaging**: version 4.0.0, uplugin description rewritten around the v4 headline features; prompt counts corrected everywhere (14); copyright unified as 'StraySpark Studio 2026' across all 200+ source files, configs, and docs.
- Live-found fix: modeling tools no longer use the on-disk `AssetExists` check (broke create→edit script chains with unsaved assets).

### Added — modeling tools implemented (were schema-only stubs since v3)
- All 5 `modeling_*` tools now work, backed by **GeometryScript** on `UDynamicMesh` (no editor-mode plumbing — the blocker that kept them stubs): `modeling_boolean` (CSG in world arrangement, computed in A-local space, empty-result guard), `modeling_polycut` (world plane → local cut frame, hole fill), `modeling_polyextrude` (all faces along average normals), `modeling_uv_unwrap` (planar/box/cylindrical sized to bounds, or XAtlas auto), `modeling_remesh` (uniform, long-running). Structured results report before/after triangle counts. Edits modify the mesh ASSET (all instances update) — stated in every description.

### Added — chaos creation + fracture implemented (were stubs)
- `chaos_create_geometry_collection`: mirrors the engine factory flow (AppendStaticMesh per source, InitializeMaterials, RebuildRenderData); all-or-nothing source resolution.
- `chaos_fracture`: uniform/voronoi/cluster site scattering or planar cutting via `FFractureEngineFracturing`, select-all transform selection, Dataflow-node-default quality params, reports new piece counts. Long-running + dry-run.
- `chaos_create_cloth_asset` / `chaos_add_field` remain stubs with honest hints (cloth is Dataflow-graph-driven in 5.7; fields need a field graph — both better authored in-editor today).
- New deps: GeometryScriptingCore/GeometryFramework + GeometryCollectionEngine/Chaos/DataflowCore/FractureEngine; uplugin now depends on the engine-shipped GeometryScripting and Fracture plugins.

### Changed — structured output migration (wave 1, mechanical)
- **All 119** `Success(JsonToString(X))` call sites migrated to `SuccessStructured(JsonToString(X), X)` — per MCP spec guidance, structured-returning tools also ship the serialized JSON as text for backward compatibility. Every read tool that built a JSON payload now exposes it as `structuredContent`.
- Remaining LogJson warning sites cleaned (batch/select/duplicate/attach `actor_names`, EngineAPI `class_name`/`pattern`/`header_path`) — TryGet with explicit errors instead of warn-and-default.

### Deferred (documented)
- **MetaSound graph mutators** — UMetaSoundBuilderSubsystem is the right backend but is its own work package; stubs keep honest unsupported messages.
- **Resource subscriptions** — no server-push channel exists in UE's HttpServer (established in alpha.1); poll `resources/read` instead, it is cheap.
- **Screenshot annotations** — post-4.0 polish; requires scene-view projection plumbing.

### — alpha.1 line: progressive disclosure + shared utility layer —

> A fresh agent session now costs ~3K tokens of tool definitions instead of ~60K, with all tools still callable.

### Added — progressive disclosure (catalog mode)
- **`ToolExposureMode` setting** (default **Catalog**): `tools/list` returns 4 meta-tools + ~25 high-frequency core tools; everything else is discovered on demand. Per-request override: `tools/list` params `{"exposure": "full"}`. The response carries a `catalogInfo` block telling agents how to discover the rest.
- **4 new meta-tools** (category `Meta`, always registered):
  - `search_tools(query, category?, limit?)` — relevance-ranked keyword search over the registry with one-line summaries
  - `get_tool_schemas(names[])` — full definitions on demand (≤25 per call)
  - `list_tool_categories()` — the catalog's table of contents
  - `run_tool_script(script)` — restricted multi-step program: sequential tool calls with `save_as`/`$var.path` result references and `foreach` loops, executed in ONE editor transaction (all-or-nothing, rolled back on first failure). Every step is scope-checked like a normal call; nesting is forbidden; limits 100 steps / 1000 invocations. Kills the N-round-trip problem for batch work. Supports `dry_run`.
- **`tools/list` pagination** — `cursor`/`limit` params with `nextCursor` (MCP spec).
- **Tool categories** — every tool now carries its registration category (`category` field in tools/list; drives search/catalog).
- **`.Example()` builder** — concrete invocation examples under the input schema's JSON-Schema `examples` keyword (Anthropic data: +18pp complex-param accuracy). Meta-tools ship examples; more tools in later alphas.
- **Context-aware handlers** (`HandleCtx`) — tools can now receive the caller's scope/session/cancellation context.

### Added — shared utility layer (`Private/Common/`)
- `MCPActorResolver` — label→actor cache invalidated by engine delegates; **O(1) amortized** actor lookup. All 17 duplicated `FindActorByLabel` scan sites now delegate to it (previously a 100-actor batch op in a 10K-actor level cost 1M iterations).
- `MCPEditorContext`, `MCPAssetResolver` (structured NotFound + did-you-mean), `MCPNodeFactory` (K2-node creation template), `MCPGraphSerializer` (graph→JSON with deduplicated edge list), `MCPPropertyIO` (dual-convention property parsing: UE text syntax AND native JSON arrays/maps/structs, with self-correction error messages).

### Added — background tasks for long-running tools (MCP 2025-11-25 Tasks model)
- Tools marked `.LongRunning()` (build_lighting, build_navigation, run_automation_specs, execute_pcg, import_asset, generate_3d_model, image_to_3d_model, generate_ui_image, remove_background) no longer block a network thread for their full duration: the registry runs them on the game thread, waits a 3s grace window (fast runs return normally), and on overrun returns `{task_id, status: "working"}`.
- New meta-tools `get_task_status` (progress + final result), `cancel_task` (cooperative), `list_tasks` — implemented as tools rather than protocol methods so every MCP client can use them without capability negotiation. Finished records kept ~1 hour, then swept.
- Task progress is wired to the existing ProgressSink: long-running tools can report fraction/message and agents see it via polling.
- Design note: SSE/streaming push was evaluated and rejected — UE's FHttpServerModule completes each request with a single response (no chunked streaming), so push would require vendoring an HTTP stack. The 2025-11-25 spec's task-polling model is the sanctioned alternative and fits UE exactly.

### Added — closed-loop graph editing (Phase 2, first batch)
- `describe_graph` — full graph topology (nodes + pins + deduplicated directed edge list + compile status) in ONE call; replaces N x get_node_pins round-trips and lets agents verify their own edits.
- `move_node`, `delete_nodes` (all-or-nothing, refuses protected entry/result nodes with reasons, dry_run), `set_node_comment`, `add_comment_node`, `add_reroute_node`.
- `get_execution_paths` — exec-flow tracing from every event/entry node ("does BeginPlay reach SetActorHidden?").
- `find_orphaned_nodes` — connection-less node detection for cleanup.
- `list_node_types` — reflection-driven catalog of concrete K2 node classes.
- `list_actor_components` / `get_component_info` — component-tree and per-component property inspection (JSON-exported values, class hierarchy, did-you-mean on miss).
- `list_actors`: `offset` pagination (a 50K-actor level previously truncated at 5000 with no way to reach the rest).

### Added — test & docs infrastructure (Phases 3–5, first batch)
- **Editor-crash fix found by MatrixSpec on its first run** — `query_navigation_path` dereferenced `GetDefaultNavDataInstance()` unconditionally; it is null in any level without a built navmesh → access violation. Present since v3. Now returns a structured `not_found` error with the fix hint. The tool also never validated its six declared-required coordinates (silently proceeded with zeros); it validates first now.
- Matrix executions are breadcrumb-logged so any future hard-crasher is named by the last log line.
- **MatrixSpec** — generated tests over the ENTIRE registry: schema contracts (description, object schema, required-args-declared, category) for every tool, empty-args→structured-error for the 200+ tools with required args, scope_denied for every destructive tool under Read scope, dry_run-refusal contract. New tools are covered the moment they register.
- **`export_tool_docs`** — generates the complete tool-reference markdown from the live registry (names, categories, descriptions, argument tables, annotations) into `Saved/MCPDocs/ToolReference.md`. Documentation counts/signatures can no longer drift from the build.
- Python-call helpers deduplicated into `Common/MCPPythonCall` (PyEscape / FindPluginPythonScript / BuildPythonScriptCall) — UIImage and 3DModel tools now share one implementation.
- Output schemas established on the structured flagship tools (`describe_graph`, `search_tools`) as the Phase 3 migration pattern.
- Docs: headline tool counts corrected to 370+ (exact count depends on optional plugins).

### Added — security & protocol plumbing
- **`BindAddress` setting** (default `127.0.0.1`) — actually controls the HTTP listener (via UE's `[HTTPServer.Listeners]` config, set in-memory before listener creation). Non-loopback binding is refused without bearer-token auth and falls back to loopback. The DNS-rebinding Host check is skipped when deliberately bound non-loopback (auth carries security there).
- **Universal transactional contract** — every mutating (non-ReadOnly) tool now runs inside a named `FScopedTransaction` at the registry level (`MCP: <tool>` undo steps). v3 had exactly one tool doing this; multi-step agent edits are now cleanly undoable. Tools' own transactions nest.
- `.OutputSchema()` builder — declare structured-output schemas (serialized as MCP `outputSchema`); mass adoption lands in Phase 3.

### Changed
- `tools/list` serialization is now cached in the registry (rebuilt only when the tool set changes) instead of re-serializing ~360 schemas per request.
- **Host-header validation** (DNS-rebinding defense): requests with a non-loopback `Host` are rejected 403.

## [3.2.0] — 2026-06-04

> Hardening release (v4 Phase 0): closes every known editor-crash / editor-hang path reachable from a tool call, plus correctness fixes across the tool layer. No new tools.

### Added
- **Request body size limit** — `MaxRequestSizeMB` setting (default 8). Oversized POSTs are rejected with HTTP 413 before parsing instead of being read into editor memory.
- **Tool call timeout** — `ToolCallTimeoutSeconds` setting (default 60, 0 = wait forever / v3 behavior). Network threads no longer block indefinitely behind a stalled game thread (modal dialog, long GC); timed-out calls return a structured `timeout` error. Execution marshaling rebuilt on `TPromise`/`TFuture` with value-copied state so a timed-out request can never leave the game thread writing into dead stack memory.
- **Exception guard** — tool handlers now run inside try/catch (`bEnableExceptions=true`); a throwing tool returns a structured `internal` error instead of crashing the editor.
- **Session garbage collection** — `SessionIdleTimeoutMinutes` setting (default 30). Idle sessions are swept once a minute: working set freed, abandoned transactions cancelled (an orphaned open transaction corrupted the undo stack for the whole editor session). POSTs refresh session activity; v3 sessions leaked until an explicit DELETE.
- New `timeout` error code in the structured error taxonomy.

### Fixed
- `connect_pins`: no longer silently retries with swapped source/target (the agent believed A→B while the graph could get B→A). Pin directions are validated explicitly; auto-correction is reported in the result; failures now carry the schema's own reason (type mismatch, cycle, ...).
- `compile_blueprint`: now captures the compiler results log — errors/warnings are returned with per-node messages instead of a bare status word.
- `add_for_each_loop_node` / `add_while_loop_node` (and the third StandardMacros user): macro library located via canonical path + asset-registry fallback (cached) instead of three hardcoded engine paths.
- `batch_transform` / `batch_set_property`: now **all-or-nothing**. Every target is validated before the transaction opens (v3 silently applied whatever subset existed and reported success); a mid-batch apply failure rolls the whole transaction back with per-item errors.
- `align_actors`: unknown `align_mode` values now error instead of reporting success while moving nothing; removed dead locals.
- `paint_foliage` / `erase_foliage`: numeric args clamped in double-space before int casts (out-of-range double→int32 was UB); radius clamped to sane bounds.
- `add_variable`: duplicate variable names now return a structured `already_exists` error instead of an indistinct failure.
- Widget creation: widget type resolution falls back to reflection over all concrete `UWidget` subclasses — previously only ~21 hardcoded types (no ListView, TreeView, MultiLineEditableTextBox, ...). Resolved classes are cached.
- `create_folder` / `move_assets_to_folder`: package paths validated (rejects `..` traversal and malformed paths) before mapping to the filesystem.

### Changed
- **Removed `bAllowRemoteConnections`** — it never changed the bind address (UE's HttpServer listens per its own config), so the flag only created a false sense of a security boundary. A real `BindAddress` setting is planned for v4 Phase 1.
- Docs: corrected prompt count (11, not 12); removed reference to a Python test harness that is not shipped.

## [3.1.0] — 2026-06-04

> Stability release: fixes two editor-crash bugs and the modal-dialog hang reported by customers. First non-alpha release of the v3 line.

### Fixed — `create_user_struct` engine crash: `Tools/MCPDataTools.cpp`

- The struct was created via `IAssetTools::CreateAsset` with a null factory, which falls back to a plain `NewObject<UUserDefinedStruct>` and leaves `EditorData` uninitialized. The first `FStructureEditorUtils::AddVariable` call then hit a fatal `CastChecked` and crashed the editor — 100% reproducible with any `fields_json`. Structs are now created through `UStructureFactory` (the same path the Content Browser uses).
- Float/Double fields registered an invalid pin type (`PC_Real` with no subcategory); they now get `PinSubCategory = PC_Double`.
- The factory's seed Boolean member (`MemberVar_0`) is removed once requested fields are added, so the struct contains exactly the fields asked for. It is kept when no fields are supplied (a UserDefinedStruct must always have at least one variable).

### Fixed — non-modal saves: `Tools/MCPLevelTools.cpp`

- `save_level(save_all=true)`, `new_level`, and `open_level` called `FEditorFileUtils::SaveDirtyPackages` with `bPromptUserToSave=true`, which opened the modal "Save Content" dialog and hung automation clients. All save paths now run non-modal (`bPromptUserToSave=false`, `bCanBeDeclined=false`) under a `GIsRunningUnattendedScript` guard that also suppresses nested source-control checkout prompts. Verified against World Partition / one-file-per-actor external packages.
- The single-level `save_level` path is guarded against the "Save As" file dialog that `FEditorFileUtils::SaveLevel` opens for untitled maps.

### Fixed — automation test tools: `Tools/MCPTestAuthoringTools.cpp`

- `list_automation_specs` / `run_automation_specs` could only see SmokeFilter tests: the framework's `RequestedTestFilter` defaults to `SmokeFilter`, hiding every Engine/Product/Perf/Stress-filter test (including this plugin's own specs). Both tools now widen the filter to `EAutomationTestFlags_FilterMask` before enumerating.
- `run_automation_specs` crashed the editor on any spec-style test: it passed the beautified display name to `StartTestByName` (which never matches, so no test started) and then called `StopTest`, tripping `check(GIsAutomationTesting)`. It now passes `FAutomationTestInfo::GetTestName()` and only stops a test that actually started; a failed start is reported as a failed result instead.
- `run_automation_specs` crashed the editor on specs a second way: spec `It` blocks run as latent commands, and `InternalStopTest` asserts on a non-empty latent queue. The runner now pumps `ExecuteLatentCommands()` to completion before `StopTest`, with a `timeout_sec` deadline that drains the queue via `DequeueAllCommands()` and reports a timeout failure rather than hanging or asserting.

### Tests — `UnrealMCPServerTests`

- New regression spec: `create_user_struct` creates a struct with Float/String/Vector fields through `ExecuteTool`, verifies the exact field set (seed member removed), and force-deletes its artifacts. Self-healing across runs (`ObjectTools::ForceDeleteObjects`; `DeleteAssets` cannot complete unattended).

### Internal

- `Engine/UserDefinedStruct.h` → `StructUtils/UserDefinedStruct.h` (the class moved to CoreUObject in UE 5.5; the old header is an empty stub unless `UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5` is set).

---

## [3.0.0-alpha.12] — 2026-05-02

> Bundles D.6 Modeling + Material Layers, D.7 Chaos / Destruction, D.8 MetaHuman. 341 → 359 tools.

### Added — Phase D.6 (Modeling): `Tools/MCPModelingTools.cpp` (5 tools)

- `modeling_polyextrude`, `modeling_polycut` (Destructive), `modeling_boolean` (Destructive), `modeling_uv_unwrap`, `modeling_remesh`.
- All five register stable schemas and currently return `EMCPError::Unsupported` with a hint pointing at `MeshModelingToolsExp + ModelingComponents + GeometryCore`. The plugin keeps avoiding the include surface until the dep is taken intentionally.

### Added — Phase D.6 (Material Layers): `Tools/MCPMaterialLayerTools.cpp` (4 tools)

Backed by `UMaterialInstance::GetMaterialLayers / SetMaterialLayers` + `FMaterialLayersFunctions`. Mutators are gated behind `WITH_EDITOR` (a no-op stub registers in non-editor builds so `tools/list` stays consistent).

- `mat_layer_get_stack` — return `{layers[], blends[]}` for a material instance.
- `mat_layer_add` — append a layer (and optional blend) onto the stack.
- `mat_layer_remove` (Destructive) — remove a layer at index, plus its matching blend slot.
- `mat_layer_set_blend` — replace or clear the blend at a given index.

### Added — Phase D.7: `Tools/MCPChaosTools.cpp` (5 tools)

- `chaos_create_geometry_collection`, `chaos_fracture` (Destructive), `chaos_add_field`, `chaos_create_cloth_asset` — schemas only; bodies pending `GeometryCollectionEngine + FractureEngine + ChaosCloth`.
- `chaos_apply_force` — implemented: applies an impulse to an actor's primary `UPrimitiveComponent` via `AddImpulse`. No Chaos plugin dep required.

### Added — Phase D.8: `Tools/MCPMetaHumanTools.cpp` (4 tools)

- `metahuman_list_assets` — implemented: filters `USkeletalMesh` AssetRegistry results by `/MetaHumans/` package path.
- `metahuman_set_lod` — implemented: forces a LOD on an actor's `USkeletalMeshComponent` (works for any skeletal mesh, not just MetaHumans).
- `metahuman_attach_to_skeletal_mesh` — implemented: assigns a `USkeletalMesh` asset onto an actor's component.
- `metahuman_import` — registered as `Unsupported` pending the MetaHuman + Quixel Bridge plugin's Python entry points.

### Settings

Four new toggles: `bEnableModelingTools`, `bEnableMaterialLayerTools`, `bEnableChaosTools`, `bEnableMetaHumanTools` (all default `true`).

---

## [3.0.0-alpha.11] — 2026-05-02

> Bundled implementation of D.2 Source Control, D.3 Test Authoring, D.4 Runtime Debug, and D.5 MetaSound Graph parity. 313 → 341 tools.

### Added — Phase D.2: Source Control (`Tools/MCPSourceControlTools.cpp`, 8 tools)

Wraps `ISourceControlModule`; works against whatever provider the project has loaded (Perforce, Git LFS, Plastic, Subversion).

- `sc_provider_status` — `{enabled, available, provider, status_text, project_path}`.
- `sc_check_out`, `sc_revert` (Destructive), `sc_submit` (Destructive, requires `description`).
- `sc_get_history` — per-revision `{revision, author, date, description, action}` for one file.
- `sc_diff_against_revision` — fetches the depot copy of a file at a revision and returns the temp filename.
- `sc_pending_changelist` — categorized arrays of modified / added / deleted / checked-out files under Content/ + Source/.
- `sc_resolve_conflict` — Destructive; modes `accept_yours | accept_theirs | manual`.

Build.cs adds `SourceControl` to private deps.

### Added — Phase D.3: Test Authoring & Run (`Tools/MCPTestAuthoringTools.cpp`, 5 tools)

- `create_automation_spec` — scaffolds `Source/<target_dir>/<Name>Spec.cpp` with a passing `It` skeleton; build is required to register the test.
- `list_automation_specs` — enumerate `FAutomationTestFramework::GetValidTestNames` with optional substring filter.
- `run_automation_specs` — synchronous runner; iterates each matching spec, accumulates pass/fail/duration, persists into a session-scoped last-report buffer.
- `get_last_test_report` — returns the buffered last run's report.
- `add_functional_test_actor` — spawns an `AFunctionalTest` into the editor world (RequiresPieOff).

Build.cs adds `FunctionalTesting` to private deps.

### Added — Phase D.4: Runtime Debug & Introspection (`Tools/MCPDebugTools.cpp`, 7 tools)

Wraps `FKismetDebugUtilities`; hooks `FBlueprintCoreDelegates::OnScriptException` for runtime-error capture.

- `set_blueprint_breakpoint`, `clear_blueprint_breakpoint`, `list_breakpoints`.
- `add_watch`, `get_watches` (per-Blueprint or all loaded).
- `get_last_runtime_error` — most recent BP `OnScriptException` payload.
- `get_call_stack` — top-frame info while paused on a breakpoint hit; otherwise `paused=false`.

### Added — Phase D.5: MetaSound Graph Parity (`Tools/MCPMetaSoundGraphTools.cpp`, 8 tools)

Tools registered with stable schemas. Read-only / save tools work today; mutators return `EMCPError::Unsupported` until a `MetasoundFrontend` module dependency is taken on (intentionally deferred to keep the plugin loadable on projects without MetaSound).

- `metasound_list_node_classes` — discovers MetaSound* UClasses via reflection.
- `metasound_get_graph` — class, package, and reflected top-level UProperties for an asset.
- `metasound_compile` — marks dirty + saves; the MetaSound builder rebuilds the Frontend document on next access.
- `metasound_add_node`, `metasound_remove_node`, `metasound_connect_pins`, `metasound_disconnect_pin`, `metasound_set_node_property` — registered surface; require future `MetasoundFrontend` dep.

### Settings

Four new per-category toggles under `UMCPSettings`:
- `bEnableSourceControlTools` (default `true`; in SceneBuilding + Gameplay presets).
- `bEnableTestAuthoringTools` (default `true`; in SceneBuilding + Gameplay presets).
- `bEnableDebugTools` (default `true`; in Gameplay preset).
- `bEnableMetaSoundGraphTools` (default `true`).

---

## [3.0.0-alpha.7] — 2026-05-02

### Added — Phase D.1: PIE Control

New tool family `Tools/PIE/` (9 tools, set `Settings → Server → Tool Categories → Gameplay → PIE`):

- `pie_start` — start PIE in Selected / Standalone / MobilePreview / VRPreview mode (with `num_players`, `window_width`, `window_height`).
- `pie_stop` — terminate the active PIE session.
- `pie_pause`, `pie_resume`, `pie_step_frame` — pause / resume / single-step.
- `pie_send_input` — synthesize keyboard / mouse / gamepad events.
- `pie_screenshot` — capture the active PIE viewport (returns base64 PNG).
- `pie_get_state` — `{is_running, is_paused, num_players, world_time, fps_estimate}`.
- `pie_attach_player_controller` — possess an actor at runtime.

Backed by `GEditor->RequestPlaySession` / `RequestEndPlayMap` / `SetPIEWorldsPaused` / `PlaySessionSingleStepped` and `APlayerController::InputKey` (via `FInputKeyEventArgs::CreateSimulated`).

### Settings

- `bEnablePIETools` (default `true`; included in the Gameplay preset).

---

## [3.0.0-alpha.6] — 2026-05-02

### Added — Phase C: Agent Ergonomics

- **Structured error taxonomy** (C1). New `EMCPError` enum: `not_found, already_exists, invalid_path, invalid_name, out_of_range, locked, requires_game_thread, requires_pie_off, scope_denied, unsupported, internal`. New factory `FMCPToolResult::ErrorStructured(code, message, hint, did_you_mean)`. Tool error responses now carry a structured `structuredContent` payload alongside the human-readable text.
- **Did-you-mean suggestions** (C2). `FMCPValidate::AssetExists` consults the in-memory search index on miss and returns up to 3 fuzzy matches in `did_you_mean`. The registry's "unknown tool" error also surfaces top-3 substring matches.
- **`dry_run` infrastructure** (C3). When `arguments.dry_run = true`, the registry wraps tools opted-in via `.SupportsDryRun()` in a cancelled `FScopedTransaction` so agents can preview effects. Tools that haven't opted in refuse with `EMCPError::Unsupported`. Per-tool opt-ins ship over time as we touch them.
- **Multi-call transactions** (C4). New JSON-RPC methods `transactions/begin`, `transactions/commit`, `transactions/rollback` keyed by `Mcp-Session-Id`. Wraps `GEditor->BeginTransaction` so a sequence of tool calls collapses to one undo step.
- **Working Set** (C5). New JSON-RPC methods `workingset/get`, `workingset/set`, `workingset/clear` for per-session focus state (selection, current_blueprint_path, current_widget_path, current_level_sequence_path). Storage primitive only — tools opt in to fallback over time.
- **Capability hints** (C6). Two new `FMCPToolDefinition` flags: `bRequiresPieOff` and `bSupportsDryRun`. Builder methods `.RequiresPieOff()` and `.SupportsDryRun()`. Registry refuses PIE-incompatible tools at pre-flight when PIE is active. Surfaces in `tools/list` annotations.

### Internal

- `MCPProtocol.h` adds `EMCPError` enum + `MCPErrorCodeToString`.
- `MCPValidate.h` adds `FMCPValidateResult::FailCoded` factory.
- `MCPToolRegistry.cpp` includes `Editor.h` + `ScopedTransaction.h`; pre-flight runs PIE check, scope check, dry_run intercept.

---

## [3.0.0-alpha.5] — 2026-05-02

### Added — Phase B.2: Cancellation

- **`notifications/cancelled` JSON-RPC method**. Server tracks an atomic cancel flag per in-flight request id; tools that poll `Context.IsCancelled()` between game-thread waits can bail cooperatively.

### Deferred

- **WebSocket transport** (B6). UE 5.7 ships only client-side WebSocket support; server-side requires vendoring Boost.Beast or libwebsockets. Setting `bEnableWebSocket` reserved for a future release.
- **Live progress streaming** (`notifications/progress`). Requires async tool execution, deferred to a future Phase B.3.

---

## [3.0.0-alpha.4] — 2026-05-02

### Added — Phase B: Security & Scope

- **`FMCPRequestContext`** with `EMCPScope { Read, Scene, Destructive }`, `SessionId`, `ProgressSink`, `CancelFlag`. Carried alongside every JSON-RPC request.
- **Bearer-token authentication** (`Authorization: Bearer <token>`). New settings `bRequireAuthToken`, `AuthTokens`, `bAllowDestructiveScope`. Returns 401 with `WWW-Authenticate: Bearer realm="unreal-mcp"` on missing/invalid tokens.
- **Origin allow-list**. New setting `AllowedOrigins` (default `[http://localhost, http://127.0.0.1]`). Returns 403 when the request `Origin` header doesn't match.
- **Scope gate on tool execution**. Tools annotated `bDestructiveHint=true` refuse for callers below `Destructive` scope.

### Changed (BREAKING)

- **CORS**: replaced `Access-Control-Allow-Origin: *` with per-origin echo against `AllowedOrigins`. Browsers loading the plugin from a non-localhost origin must add it explicitly.

---

## [3.0.0-alpha.3] — 2026-05-02

### Changed — Phase A bulk migration

- Migrated **all 298 remaining tool registrations** from the v2 hand-rolled `FMCPToolDefinition` form to the v3 `MCP_TOOL(...).Description(...).Handle(...)` builder. Same behavior; ~3000 LOC eliminated; easier authoring going forward. Both forms still compile.

---

## [3.0.0-alpha.2] — 2026-05-02

### Changed — Phase A file splits

Mega tool files split into sub-domain folders. Pure code organization, no agent-visible behavior change. Sub-file basenames are domain-prefixed to satisfy UBT's per-module unique-filename rule.

| Original | LOC before → after | Now lives in |
|---|---|---|
| `MCPBlueprintTools.cpp` | 4090 → 27 | `Tools/Blueprint/Blueprint{Common,Lifecycle,Variables,Functions,Graph,Nodes,FlowControl,Dispatchers,Input}.{h,cpp}` |
| `MCPWidgetTools.cpp`    | 1975 → 21 | `Tools/Widget/Widget{Common,Lifecycle,Tree,Properties,Events,Batch}.{h,cpp}` |
| `MCPSequencerTools.cpp` | 1309 → 17 | `Tools/Sequencer/Sequencer{Common,Lifecycle,Tracks,SpecialTracks}.{h,cpp}` |
| `MCPAnimGraphTools.cpp` | 1473 → 21 | `Tools/AnimGraph/AnimGraph{Common,Lifecycle,BlendSpaces,Montages,Notifies,StateMachine}.{h,cpp}` |
| `MCPSpatialTools.cpp`   | 1283 → 19 | `Tools/Spatial/Spatial{Common,Bounds,Trace,Placement,Alignment}.{h,cpp}` |

102 tool registrations moved byte-for-byte; 26 file-static helpers consolidated into 5 `*Common.{h,cpp}` modules.

---

## [3.0.0-alpha.1] — 2026-05-02

### Added — Phase A: Architecture & DX

- **`FMCPValidate`** + `BAIL_IF_INVALID` macro. Centralised input validation (`PackagePath`, `AssetName`, `AssetExists`, `AssetDoesNotExist`, `RequiredString`, `InRangeI/F`, `OneOf`). Prevents the `create_user_struct`-class crashes by construction.
- **`FMCPToolBuilder`** + `MCP_TOOL` macro. Fluent declarative tool registration replacing the 25-line hand-rolled `FMCPToolDefinition` boilerplate.
- **`UnrealMCPServerTests`** editor module with three automation specs (`UnrealMCPServer.Validate`, `UnrealMCPServer.ToolBuilder`, `UnrealMCPServer.DataTools`).
- **`Tests/Python/`** pytest harness covering tools/list, list_actors, and create_user_struct happy/duplicate/invalid paths over HTTP.
- **MCPDataTools.cpp** migrated as the pilot for the new builder + validators pattern.

### Fixed

- **Linker bug**: `extern ENGINE_API float GAverageFPS;` declarations were nested inside `namespace MCPBuiltInResources`, making them namespace-scoped symbols the linker couldn't resolve against the Engine module. Hoisted to file scope.
- **`create_user_struct` crashes**: now returns a clean error on invalid path / invalid name / existing asset instead of triggering the UE overwrite modal that broke on retry.

### Behavioral changes

- `add_datatable_row`: row names are now validated against `INVALID_OBJECTNAME_CHARACTERS`.
- `get_struct_info`: now annotated `readOnlyHint=true` (was `idempotentHint` only).

---

## [2.0.2] — earlier

Last v2 release. See the public documentation at `Unreal_MCP_Server_Documentation.md` for the v2 feature set (304 tools, 15 resources, 12 prompts).
