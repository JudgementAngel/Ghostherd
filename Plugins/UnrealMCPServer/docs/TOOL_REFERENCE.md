# Unreal MCP Server — Tool Reference

> Generated from the live registry by `Tests/generate_tool_reference.py`. Server **5.0.0**, engine **5.8.2-56702186+++UE5+Release-5.8**, **523 tools** in 70 categories. Do not edit by hand; regenerate from the latest exported catalog in `docs/validation/`.

Annotations: **read-only** (`readOnlyHint`), **destructive** (`destructiveHint`, needs the Destructive scope), **idempotent**, **long-running** (may return a task or operation handle), **PIE-off** (refused while Play-In-Editor is active), **preview** (`supportsDryRun`). Tools with an output schema are marked **structured result**.

## Categories

- [3DModel](#3dmodel-3-tools) (3)
- [AI](#ai-8-tools) (8)
- [Actor](#actor-17-tools) (17)
- [AnimData](#animdata-16-tools) (16)
- [AnimGraph](#animgraph-14-tools) (14)
- [AnimGraphNodes](#animgraphnodes-13-tools) (13)
- [AnimMixer](#animmixer-4-tools) (4)
- [Animation](#animation-10-tools) (10)
- [Asset](#asset-6-tools) (6)
- [AssetManagement](#assetmanagement-7-tools) (7)
- [Audio](#audio-3-tools) (3)
- [Batch](#batch-3-tools) (3)
- [Blueprint](#blueprint-65-tools) (65)
- [Build](#build-7-tools) (7)
- [ChangePlans](#changeplans-5-tools) (5)
- [Chaos](#chaos-5-tools) (5)
- [CommonUI](#commonui-5-tools) (5)
- [ControlRig](#controlrig-2-tools) (2)
- [Data](#data-6-tools) (6)
- [Debug](#debug-7-tools) (7)
- [Diagnostics](#diagnostics-4-tools) (4)
- [Editor](#editor-7-tools) (7)
- [EngineAPI](#engineapi-3-tools) (3)
- [EnhancedInput](#enhancedinput-6-tools) (6)
- [Environment](#environment-4-tools) (4)
- [EpicToolsets](#epictoolsets-3-tools) (3)
- [Foliage](#foliage-4-tools) (4)
- [GAS](#gas-8-tools) (8)
- [GameFramework](#gameframework-6-tools) (6)
- [GameplayTags](#gameplaytags-3-tools) (3)
- [Gizmo](#gizmo-3-tools) (3)
- [Iris](#iris-1-tools) (1)
- [Landscape](#landscape-3-tools) (3)
- [Level](#level-6-tools) (6)
- [Lighting](#lighting-3-tools) (3)
- [Macro](#macro-6-tools) (6)
- [Material](#material-5-tools) (5)
- [MaterialGraph](#materialgraph-8-tools) (8)
- [MaterialLayer](#materiallayer-4-tools) (4)
- [Meta](#meta-8-tools) (8)
- [MetaHuman](#metahuman-4-tools) (4)
- [MetaSound](#metasound-6-tools) (6)
- [MetaSoundGraph](#metasoundgraph-8-tools) (8)
- [Modeling](#modeling-5-tools) (5)
- [Montage](#montage-14-tools) (14)
- [MorphTarget](#morphtarget-4-tools) (4)
- [Navigation](#navigation-3-tools) (3)
- [Networking](#networking-5-tools) (5)
- [Niagara](#niagara-3-tools) (3)
- [Operations](#operations-6-tools) (6)
- [PCG](#pcg-16-tools) (16)
- [PIE](#pie-10-tools) (10)
- [Performance](#performance-5-tools) (5)
- [Physics](#physics-10-tools) (10)
- [Python](#python-1-tools) (1)
- [Search](#search-2-tools) (2)
- [Sequencer](#sequencer-12-tools) (12)
- [SequencerAnimation](#sequenceranimation-7-tools) (7)
- [Snapshots](#snapshots-4-tools) (4)
- [SourceControl](#sourcecontrol-8-tools) (8)
- [Spatial](#spatial-10-tools) (10)
- [Spline](#spline-7-tools) (7)
- [StateTree](#statetree-5-tools) (5)
- [StaticMesh](#staticmesh-7-tools) (7)
- [Substrate](#substrate-1-tools) (1)
- [TestAuthoring](#testauthoring-5-tools) (5)
- [UIImage](#uiimage-2-tools) (2)
- [Visual](#visual-9-tools) (9)
- [Widget](#widget-41-tools) (41)
- [WorldPartition](#worldpartition-2-tools) (2)

## 3DModel (3 tools)

### `generate_3d_model`

Generate a 3D model from a text prompt using fal.ai and import it into UE as a StaticMesh. Models: meshy-v6 (recommended, textured, supports PBR), meshy-v6-preview (faster/untextured), hunyuan-pro (highest fidelity up to 1.5M faces), hunyuan-rapid (faster), hunyuan-v3. Output is GLB format, imported via UE Interchange/glTF pipeline. NOTE: 3D generation takes 1-10 minutes depending on model. Requires fal.ai API key and PythonScriptPlugin.

*long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `prompt` | string | yes | Text description of the 3D model to generate (e.g., 'medieval wooden treasure chest') |
| `destination_path` | string |  | Content path for imported mesh (default: '/Game/Meshes/Generated/') |
| `model` | enum(meshy-v6\|meshy-v6-preview\|hunyuan-pro\|hunyuan-rapid\|hunyuan-v3) |  | fal.ai text-to-3d model. meshy-v6 is recommended for quality+speed. hunyuan-pro for highest fidelity. |
| `art_style` | enum(realistic\|sculpture) |  | Art style for Meshy models |
| `topology` | enum(triangle\|quad) |  | Mesh topology type |
| `target_polycount` | number |  | Target polygon count (default: 30000). Higher = more detail, larger file. |
| `enable_pbr` | boolean |  | Generate PBR texture maps (base_color, metallic, normal, roughness) |
| `model_name` | string |  | Override the generated mesh asset name (auto-generated if omitted) |

### `image_to_3d_model`

Generate a 3D model from an image using fal.ai and import it into UE as a StaticMesh. Models: trellis-2 (best quality, native 3D generation), meshy-v6-img (supports PBR/rigging/animation), hunyuan-v3-img, hunyuan-pro-img (high fidelity), rodin-v2 (production-ready), tripo-v2 (good stylized output). Provide a publicly accessible image URL. Output is GLB, imported via Interchange. NOTE: 3D generation takes 1-10 minutes. Requires fal.ai API key and PythonScriptPlugin.

*long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `image_url` | string | yes | URL of source image (publicly accessible). Can also be a base64 data URI. |
| `destination_path` | string |  | Content path for imported mesh (default: '/Game/Meshes/Generated/') |
| `model` | enum(trellis-2\|meshy-v6-img\|hunyuan-v3-img\|hunyuan-pro-img\|rodin-v2\|tripo-v2) |  | fal.ai image-to-3d model. trellis-2 is recommended for best quality. meshy-v6-img for PBR/rigging support. |
| `topology` | enum(triangle\|quad) |  | Mesh topology type (Meshy models only) |
| `target_polycount` | number |  | Target polygon/vertex count (default: 30000). Range depends on model. |
| `enable_pbr` | boolean |  | Generate PBR texture maps (Meshy/Hunyuan models) |
| `texture_size` | number |  | Texture resolution for Trellis-2: 1024, 2048, or 4096 (default: 2048) |
| `model_name` | string |  | Override the generated mesh asset name (auto-generated if omitted) |

### `list_3d_models`

List all available fal.ai 3D generation models with their capabilities.

*read-only · idempotent*

_No arguments._

## AI (8 tools)

### `add_blackboard_key`

Add a key to an existing BlackboardData asset. Supports Bool, Float, Int, String, Vector, and Object key types.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `blackboard_path` | string | yes | Content path to the BlackboardData asset |
| `key_name` | string | yes | Name of the key to add |
| `key_type` | enum(Bool\|Float\|Int\|String\|Vector\|Object) | yes | Type of the blackboard key |

### `create_behavior_tree`

Create a new BehaviorTree asset at the specified content path. The tree is saved empty and ready for editing.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new behavior tree (e.g., '/Game/AI/BT_MyTree') |

### `create_blackboard`

Create a new BlackboardData asset at the specified content path. The blackboard is saved empty and ready for key editing.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new blackboard (e.g., '/Game/AI/BB_MyBlackboard') |

### `create_eqs_query`

Create a new Environment Query System (EQS) query asset at the specified content path.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new EQS query (e.g., '/Game/AI/EQS_FindCover') |

### `get_behavior_tree_info`

Get information about a BehaviorTree asset: root node class, linked blackboard, and node count.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the BehaviorTree asset |

### `get_blackboard_info`

Get information about a BlackboardData asset. Returns all defined keys with their names and types.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the BlackboardData asset |

### `list_ai_assets`

List AI-related assets (BehaviorTrees, BlackboardData, EnvQuery) found in the project content browser.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (e.g., '/Game/'). Default: '/Game/' |
| `asset_type` | enum(BehaviorTree\|BlackboardData\|EnvQuery\|All) |  | Type of AI assets to list |

### `set_bt_blackboard`

Link a BlackboardData asset to a BehaviorTree. The behavior tree will use this blackboard for its AI context data.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `bt_path` | string | yes | Content path of the BehaviorTree asset |
| `blackboard_path` | string | yes | Content path of the BlackboardData asset to link |

## Actor (17 tools)

### `attach_actor`

Attach one actor to another as a child. The child actor will follow the parent's transform.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to attach (child) |
| `parent_name` | string | yes | Label of the parent actor to attach to |
| `socket_name` | string |  | Optional socket name to attach to |
| `attach_rule` | enum(KeepRelative\|KeepWorld\|SnapToTarget) |  | Attachment rule |

### `call_function`

Call a BlueprintCallable/native UFunction on an actor (or one of its components) with JSON args and return any output/return values. Works in the editor world or the running PIE world. Replaces many execute_python calls (e.g. read GetHealthPercent, call Heal/TakeDamage).

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `function_name` | string | yes | UFunction name to call (e.g. 'GetHealthPercent') |
| `actor_name` | string |  | Actor label; omit to use the player pawn |
| `player_index` | integer |  | Player index when using the player pawn (default 0) |
| `component` | string |  | Target this component (name/class substring) instead of the actor, e.g. 'Health' |
| `args` | any |  | Function arguments as a JSON object {paramName: value} |
| `world` | enum(editor\|pie) |  | Which world to run in |

### `create_actor`

Spawn a new actor in the current level. Supports all standard UE actor classes including StaticMeshActor, PointLight, SpotLight, DirectionalLight, CameraActor, PlayerStart, etc.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_class` | string | yes | UE class name to spawn (e.g., 'StaticMeshActor', 'PointLight', 'CameraActor', 'PlayerStart') |
| `x` | number |  | X position (default: 0) |
| `y` | number |  | Y position (default: 0) |
| `z` | number |  | Z position (default: 0) |
| `pitch` | number |  | Pitch rotation in degrees (default: 0) |
| `yaw` | number |  | Yaw rotation in degrees (default: 0) |
| `roll` | number |  | Roll rotation in degrees (default: 0) |
| `scale_x` | number |  | X scale (default: 1) |
| `scale_y` | number |  | Y scale (default: 1) |
| `scale_z` | number |  | Z scale (default: 1) |
| `label` | string |  | Actor label in the scene outliner |
| `folder` | string |  | Folder path in the scene outliner |
| `tags` | array<string> |  | Array of tags to apply to the actor |

### `destroy_actors`

Delete one or more actors from the current level by their label names.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_names` | array<string> | yes | Array of actor labels to delete |

### `detach_actor`

Detach an actor from its parent, making it a root-level actor again.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to detach |
| `keep_world_transform` | boolean |  | Keep world transform after detaching (default: true) |

### `duplicate_actors`

Duplicate actors with an optional positional offset. Multiple copies can be created, each offset incrementally from the previous.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_names` | array<string> | yes | Array of actor labels to duplicate |
| `offset_x` | number |  | X offset from original (default: 100) |
| `offset_y` | number |  | Y offset from original (default: 0) |
| `offset_z` | number |  | Z offset from original (default: 0) |
| `copies` | integer |  | Number of copies to create (default: 1) |

### `get_actor_hierarchy`

Get the parent-child hierarchy for an actor: its parent (if any) and all directly attached children.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor |

### `get_actor_properties`

Read UPROPERTY values from an actor. Returns property names, types, and values. Use without property_names to discover available properties.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor |
| `property_names` | array<string> |  | Specific property names to read. If empty, returns all visible properties. |

### `get_component_info`

Inspect one component on an actor: class hierarchy and the values of requested properties (or a curated default set). Property values are exported as JSON.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Actor label |
| `component_name` | string | yes | Component name (from list_actor_components) |
| `properties` | array<string> |  | Specific property names to read (omit for common ones: Mobility, bVisible, bHiddenInGame, ComponentTags) |

### `list_actor_components`

List an actor's full component tree: name, class, attach parent, and (for scene components) relative transform. Use before set_component_property to discover component names.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Actor label |

### `list_actors`

List actors in the current level with optional filtering by class, name, tag, or folder. Returns actor names, classes, transforms, and tags.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `class_filter` | string |  | Filter by class name (e.g., 'StaticMeshActor', 'PointLight'). Empty = all actors. |
| `name_filter` | string |  | Filter by actor label (substring match, case-insensitive) |
| `tag_filter` | string |  | Filter by actor tag |
| `folder_filter` | string |  | Filter by folder path |
| `limit` | integer |  | Maximum number of actors to return (default: 100) |
| `offset` | integer |  | Skip this many matches before returning results — combine with limit to paginate large levels (v4) |

### `select_actors`

Select actors in the editor viewport by their labels. Useful for focusing on specific actors or preparing for batch operations.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_names` | array<string> | yes | Array of actor labels to select |
| `add_to_selection` | boolean |  | If true, add to current selection. If false, replace selection (default: false). |

### `set_actor_hidden`

Show or hide an actor in the editor viewport. Optionally propagates to attached children.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor |
| `hidden` | boolean | yes | True to hide, false to show |
| `propagate_to_children` | boolean |  | Apply to attached children too (default: true) |

### `set_actor_mobility`

Set the mobility of an actor's root component (Static, Stationary, or Movable).

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor |
| `mobility` | enum(Static\|Stationary\|Movable) | yes | Mobility setting |

### `set_actor_property`

Set a UPROPERTY value on an actor. The value is provided as a string and parsed by the UE property system. Use get_actor_properties first to discover property names and current values.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor |
| `property_name` | string | yes | Name of the UPROPERTY to set |
| `property_value` | string | yes | New value as a string (will be parsed by UE property system) |

### `set_actor_tags`

Add, remove, or replace tags on an actor. Default mode is 'replace' which overwrites all existing tags.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor |
| `tags` | array<string> | yes | Array of tags |
| `mode` | enum(replace\|add\|remove) |  | How to apply tags |

### `set_actor_transform`

Set or modify the transform (position, rotation, scale) of an actor. Only provided fields are changed; omitted fields keep their current value.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to transform |
| `x` | number |  | X position |
| `y` | number |  | Y position |
| `z` | number |  | Z position |
| `pitch` | number |  | Pitch rotation in degrees |
| `yaw` | number |  | Yaw rotation in degrees |
| `roll` | number |  | Roll rotation in degrees |
| `scale_x` | number |  | X scale |
| `scale_y` | number |  | Y scale |
| `scale_z` | number |  | Z scale |
| `relative` | boolean |  | If true, values are added to current transform. If false, values are set absolutely (default: false). |

## AnimData (16 tools)

### `anim_add_curve`

Add a named animation curve to an AnimSequence or AnimMontage. Float curves are the general mechanism for driving values from animation — an AnimBP reads them with GetCurveValue, and a curve whose metadata is flagged as a morph target or material parameter drives that directly. Adding the curve creates it empty; add keys with anim_add_float_curve_keys. Set is_metadata_curve=true for a curve that is a constant marker rather than an animated value.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |
| `curve_name` | string | yes | Name of the curve (e.g. 'Jaw_Open', 'DisableFootIK') |
| `curve_type` | enum(Float\|Vector\|Transform) |  | Curve data type (default: 'Float') |
| `is_metadata_curve` | boolean |  | Create as a metadata curve — a constant flag rather than an animated value (default: false) |

### `anim_add_float_curve_keys`

Add float keys to an animation curve, creating the curve if it does not exist. times and values are parallel arrays and must be the same length. This is how a curve gets its shape: two keys (0.0 -> 1.0 at the strike frame, 1.0 -> 0.0 just after) gives you a gate an AnimBP or gameplay code can read with GetCurveValue.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |
| `curve_name` | string | yes | Name of the float curve |
| `times` | string | yes | Comma-separated key times in seconds (e.g. '0.0,0.35,0.5') |
| `values` | string | yes | Comma-separated key values, one per time (e.g. '0.0,1.0,0.0') |
| `create_if_missing` | boolean |  | Create the curve if it does not exist (default: true) |

### `anim_add_notify`

Add a notify to an AnimSequence or AnimMontage at a given time. Two forms:   - Simple named notify (pass notify_name only): fires as an 'AnimNotify_<name>' event the Animation Blueprint can implement — the usual choice for footsteps and hit frames.   - Class notify (pass notify_class): instantiates a UAnimNotify subclass such as AnimNotify_PlaySound or AnimNotify_PlayParticleEffect, which carries its own configurable properties (set them afterwards with set_object_property on the returned notify path). For anything with a DURATION — combo windows, invulnerability frames, hit boxes — use anim_add_notify_state instead.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |
| `time_seconds` | number | yes | Trigger time in seconds, within [0, animation duration] |
| `notify_name` | string |  | Name for a simple notify (e.g. 'FootStep_L'). The AnimBP implements 'AnimNotify_FootStep_L'. Ignored when notify_class is given, since the class supplies the name. |
| `notify_class` | string |  | Optional UAnimNotify subclass, e.g. 'AnimNotify_PlaySound'. Blueprint notify classes need a full path ending in '_C'. |
| `track_name` | string |  | Notify track to place it on (default: the animation's first track, usually named '1') |
| `create_track` | boolean |  | Create the notify track if it does not exist (default: false) |

### `anim_add_notify_state`

Add a notify STATE — a notify with a duration — to an AnimSequence or AnimMontage. Where a plain notify is an instant event, a notify state has Begin / Tick / End, which is what combo input windows, invulnerability frames, weapon hit-traces and root-motion warping windows are built from. Requires a UAnimNotifyState subclass; Blueprint ones need a full path ending in '_C'. Notify states on the same track cannot overlap — put concurrent states on separate tracks.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |
| `notify_state_class` | string | yes | UAnimNotifyState subclass, e.g. 'AnimNotifyState_TimedParticleEffect' or '/Game/Anim/ANS_ComboWindow.ANS_ComboWindow_C' |
| `start_time` | number | yes | Start time in seconds |
| `duration` | number | yes | Duration in seconds. start_time + duration must not exceed the animation length. |
| `track_name` | string |  | Notify track to place it on (default: the animation's first track) |
| `create_track` | boolean |  | Create the notify track if it does not exist (default: false) |

### `anim_add_notify_track`

Add a named notify track to an animation. Separate tracks keep unrelated notify families apart (footsteps on one, combat windows on another) which matters because notify states on the same track cannot overlap. Idempotent: returns success if the track already exists.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |
| `track_name` | string | yes | Name for the new notify track (e.g. 'Combat', 'Footsteps') |

### `anim_add_sync_marker`

Add a sync marker to an AnimSequence. Sync markers are named points (conventionally the footfalls: 'LeftFootDown', 'RightFootDown') that let animations in a sync group align by PHASE rather than by normalised time — the reason a walk can blend into a run without the feet skating, even though the two clips have different lengths. Every animation in the group needs the same marker names in the same order. Markers live on AnimSequences only; a montage inherits them from the sequences in its slot tracks.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence |
| `marker_name` | string | yes | Marker name (e.g. 'LeftFootDown'). Must match across every animation in the sync group. |
| `time_seconds` | number | yes | Marker time in seconds |
| `track_name` | string |  | Notify track to place the marker on (default: the animation's first track) |
| `create_track` | boolean |  | Create the notify track if it does not exist (default: false) |

### `anim_copy_notifies`

Copy every notify from one animation to another, creating notify tracks on the destination as needed. The usual case is propagating a footstep or hit-frame pass from one variant of a move onto its retargeted or mirrored siblings. Notify TIMES are copied verbatim, so if the two animations differ in length some notifies may land past the end of the destination — the result reports how many, and montage_validate / anim_list_notify_tracks will show them.

| Argument | Type | Required | Description |
|---|---|---|---|
| `source_path` | string | yes | Animation to copy notifies from |
| `destination_path` | string | yes | Animation to copy notifies to |
| `replace_existing` | boolean |  | Delete the destination's existing notifies first (default: false, which appends) |

### `anim_get_curve_keys`

Read an animation's curves. With no curve_name, lists every float and transform curve on the asset; with a curve_name, returns that curve's keys as time/value pairs. This is the read-back half of curve authoring — call it after anim_add_float_curve_keys to confirm the shape.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |
| `curve_name` | string |  | Optional: return this curve's keys instead of listing all curve names |

### `anim_list_notify_tracks`

List an animation's notify tracks and the notifies on each. Notify tracks are the horizontal lanes in the animation editor's notify panel; every notify belongs to exactly one. Tracks are addressed by NAME everywhere in the v4.6 notify tools — the default track an animation ships with is named '1', not '0'. Call this before adding notifies so you pass a track that exists.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |

### `anim_list_sync_markers`

List an AnimSequence's sync markers with their times, plus the set of unique marker names. Use it to check that every animation intended for one sync group carries the same marker names in the same order — a mismatch makes the group silently fall back to normalised-time sync.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence |

### `anim_move_notify`

Move a notify to a new time, and optionally change a notify state's duration or move it to a different track. Identifies the notify by name; when several share a name, occurrence_index selects which one in time order (0 = earliest). This is the retiming tool — use it when a hit frame lands early rather than deleting and re-adding the notify, which would lose the notify object's configured properties.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |
| `notify_name` | string | yes | Name of the notify to move |
| `time_seconds` | number | yes | New trigger time in seconds |
| `duration` | number |  | New duration in seconds (notify states only) |
| `track_name` | string |  | Move the notify to this track |
| `occurrence_index` | integer |  | Which occurrence to move when several notifies share the name, in time order (default: 0) |

### `anim_remove_curve`

Remove a named curve from an animation. By default the curve NAME is left registered on the skeleton (other animations may still use it); pass remove_name_from_skeleton=true to unregister it entirely, which affects every animation on that skeleton.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |
| `curve_name` | string | yes | Name of the curve to remove |
| `remove_name_from_skeleton` | boolean |  | Also unregister the curve name from the skeleton, affecting every animation on it (default: false) |

### `anim_remove_notify`

Remove notifies from an animation, by name or by whole track. Removing by name removes EVERY notify with that name (a footstep notify placed four times goes away four times) — the result reports the count. Use anim_list_notify_tracks first to see what is there.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |
| `notify_name` | string |  | Remove every notify with this name. Mutually exclusive with track_name. |
| `track_name` | string |  | Remove every notify on this track (the track itself is kept). Mutually exclusive with notify_name. |

### `anim_remove_notify_track`

Remove a notify track from an animation. Every notify on that track is removed with it — the result reports how many. Refuses to remove the animation's last remaining track.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence or AnimMontage |
| `track_name` | string | yes | Name of the notify track to remove |

### `anim_remove_sync_markers`

Remove sync markers from an AnimSequence — all of them, or only those with a given name. Removing markers from one animation in a sync group breaks phase sync for the whole group, so remove from every member or none.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to an AnimSequence |
| `marker_name` | string |  | Remove only markers with this name. Omit to remove every marker on the animation. |

### `anim_set_curve_metadata`

Flag a curve name on a SKELETON as driving a morph target and/or a material parameter. This is what makes a float curve do something without any Blueprint wiring: a curve flagged as a morph target drives the morph of the same name on every skeletal mesh using that skeleton (the basis of facial animation), and one flagged as a material parameter drives the scalar parameter of that name. The flag lives on the skeleton, so it applies to every animation on it — this tool takes the skeleton path, not an animation path.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `skeleton_path` | string | yes | Content path to the USkeleton that owns the curve name |
| `curve_name` | string | yes | Curve name to flag |
| `morph_target` | boolean |  | Drive the morph target of the same name |
| `material` | boolean |  | Drive the material scalar parameter of the same name |

## AnimGraph (14 tools)

### `add_anim_notify`

Add a simple named notify to an AnimSequence or AnimMontage at a given time. Notifies fire gameplay events — footsteps, hit frames, VFX — as 'AnimNotify_<name>' in the Animation Blueprint. Superseded by anim_add_notify, which also supports UAnimNotify subclasses and addresses notify tracks by name; and by anim_add_notify_state for anything with a duration (combo windows, hit windows). This tool is kept for compatibility and takes a track INDEX.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the AnimSequence or AnimMontage |
| `time_seconds` | number | yes | Time in seconds to place the notify |
| `notify_name` | string | yes | Name for the notify event (e.g., 'FootStep_L', 'AttackHit', 'ComboWindow') |
| `track_index` | integer |  | Notify track index (default: 0). Validated against the animation's existing tracks. |

### `add_anim_state`

Add a state to an existing state machine in an Animation Blueprint. Each state represents an animation state (Idle, Walk, Run, Jump). A new state is EMPTY: its graph contains only a Result node, so until set_anim_state_animation gives it a pose the character T-poses whenever that state is active. The usual sequence is add_anim_state -> set_anim_state_animation -> add_anim_transition -> set_anim_transition_rule.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the AnimBlueprint |
| `machine_name` | string | yes | Name of the state machine (as shown in get_anim_state_machine_info) |
| `state_name` | string | yes | Name for the new state (e.g., 'Idle', 'Walk', 'Jump') |
| `position_x` | number |  | Node X position in the graph (default: 300) |
| `position_y` | number |  | Node Y position in the graph (default: 0) |

### `add_anim_transition`

Add a transition between two states in an Animation Blueprint state machine, with a crossfade duration. A new transition's RULE is empty, which means bCanEnterTransition stays false and the transition can never actually be taken — call set_anim_transition_rule afterwards to say when it fires, then compile_anim_blueprint to verify. Use get_anim_state_machine_info to see available states, and set_anim_transition_settings for blend mode, blend profile and priority.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the AnimBlueprint |
| `machine_name` | string | yes | Name of the state machine |
| `from_state` | string | yes | Source state name |
| `to_state` | string | yes | Target state name |
| `transition_duration` | number |  | Blend duration in seconds (default: 0.2) |

### `add_blend_space_sample`

Add an animation sequence as a sample point to an existing BlendSpace at the specified (X, Y) coordinates. The coordinates must fall within the axis ranges defined when the BlendSpace was created. Multiple calls can be used to populate the blend grid with different animations.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UBlendSpace asset to modify |
| `animation_path` | string | yes | Content path to the UAnimSequence to add as a sample |
| `x` | number | yes | X axis coordinate where this sample is placed in the blend space grid |
| `y` | number | yes | Y axis coordinate where this sample is placed in the blend space grid |

### `create_aim_offset`

Create a UAimOffsetBlendSpace asset targeting a specific skeleton. Aim offsets are specialised 2D blend spaces designed for additive aiming poses. The X axis defaults to horizontal aim angle [-90, 90] and Y axis to vertical aim angle [-90, 90]. Use add_blend_space_sample to add additive AnimSequence poses to the grid.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new AimOffset asset (e.g., '/Game/Characters/AO_Hero') |
| `skeleton_path` | string | yes | Content path to the USkeleton asset (e.g., '/Game/Characters/SK_Hero_Skeleton') |

### `create_anim_blueprint`

Create a new Animation Blueprint (UAnimBlueprint) asset targeting a specific skeleton. Optionally specify a parent class derived from UAnimInstance. The created asset is saved to disk immediately and ready for graph editing.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new AnimBlueprint asset (e.g., '/Game/Characters/ABP_Hero') |
| `skeleton_path` | string | yes | Content path to the USkeleton asset to target (e.g., '/Game/Characters/SK_Hero_Skeleton') |
| `parent_class` | string |  | Short class name of the parent AnimInstance class (default: 'AnimInstance') |

### `create_anim_montage`

Create a UAnimMontage from an existing animation sequence. The animation goes into a named slot track, and the slot is registered on the target skeleton so an AnimGraph Slot node can select it — without that registration the montage plays into nothing. Play it at runtime with PlayAnimMontage() or Montage_Play(). For a multi-section combo built in one call, use montage_create_from_sections instead. After creating, shape the montage with montage_add_section, montage_link_sections and montage_set_blend_settings, then check it with montage_validate.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new AnimMontage asset (e.g., '/Game/Characters/AM_HeroAttack') |
| `animation_path` | string | yes | Content path to the source UAnimSequence asset (e.g., '/Game/Characters/AS_Attack01') |
| `slot_name` | string |  | Name of the slot track in the montage (default: 'DefaultSlot'). Registered on the skeleton if new. |
| `blend_in_time` | number |  | Blend in duration in seconds (default: engine default, 0.25) |
| `blend_out_time` | number |  | Blend out duration in seconds (default: engine default, 0.25) |

### `create_anim_state_machine`

Add a state machine node to an Animation Blueprint's AnimGraph. State machines manage animation states and the transitions between them (Idle -> Walk -> Run -> Jump). The node is created UNCONNECTED and empty. A working machine needs all of: add_anim_state per state, set_anim_state_animation to give each state a pose, add_anim_transition + set_anim_transition_rule per edge, set_anim_state_machine_entry to pick the start state, and animgraph_connect_pose to wire the machine into the Output Pose. Finish with compile_anim_blueprint.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the AnimBlueprint |
| `machine_name` | string | yes | Name for the state machine (e.g., 'Locomotion') |

### `create_blend_space`

Create a 2D UBlendSpace asset targeting a specific skeleton. Configure the X and Y blend axes with display names and value ranges. After creation, use add_blend_space_sample to populate the grid with animation samples.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new BlendSpace asset (e.g., '/Game/Characters/BS_LocomotionGrid') |
| `skeleton_path` | string | yes | Content path to the USkeleton asset (e.g., '/Game/Characters/SK_Hero_Skeleton') |
| `axis_x_name` | string |  | Display name for the horizontal axis (default: 'Speed') |
| `axis_y_name` | string |  | Display name for the vertical axis (default: 'Direction') |
| `axis_x_range_min` | number |  | Minimum value of the X axis (default: 0) |
| `axis_x_range_max` | number |  | Maximum value of the X axis (default: 500) |
| `axis_y_range_min` | number |  | Minimum value of the Y axis (default: -180) |
| `axis_y_range_max` | number |  | Maximum value of the Y axis (default: 180) |

### `get_anim_blueprint_info`

Retrieve detailed information about an existing Animation Blueprint: parent class, target skeleton, anim graph names, state machine names, montage slot groups referenced, and sync groups. Useful for understanding an AnimBP structure before modifying it.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint asset (e.g., '/Game/Characters/ABP_Hero') |

### `get_anim_montage_info`

Retrieve detailed information about an AnimMontage asset: total duration, composite sections with their start times and next section links, slot track names with their animation segments, and anim notify events. Useful for understanding or debugging a montage before runtime playback.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage asset (e.g., '/Game/Characters/AM_HeroAttack') |

### `get_anim_state_machine_info`

Get detailed information about all state machines in an Animation Blueprint: state names, transitions, default state, and connected animations. Use this to understand the structure before modifying.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the AnimBlueprint |

### `list_anim_assets_by_skeleton`

List all animation assets (AnimSequence, AnimMontage, BlendSpace) that use a specific skeleton. Uses the Asset Registry to enumerate assets efficiently without loading them all. Returns asset name, path, type, and for sequences their duration and frame count.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `skeleton_path` | string | yes | Content path to the USkeleton asset to filter by (e.g., '/Game/Characters/SK_Hero_Skeleton') |
| `asset_type` | enum(All\|Sequence\|Montage\|BlendSpace) |  | Filter by animation asset type (default: 'All') |
| `limit` | integer |  | Maximum number of assets to return (default: 100, max: 1000) |

### `list_anim_notifies`

List all animation notifies on an AnimSequence or AnimMontage with their trigger times, names, types, and track indices.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the AnimSequence or AnimMontage |

## AnimGraphNodes (13 tools)

### `animgraph_add_node`

Add a node to an Animation Blueprint's AnimGraph (or, with state_name + machine_name, to the inside of a state). Takes any UAnimGraphNode_* class by name — use animgraph_list_node_types to discover them. For asset-player nodes (SequencePlayer, BlendSpacePlayer) pass animation_path and the asset is assigned during construction so the correct pins are allocated. The node is created unconnected; wire it with animgraph_connect_pose. Returns the new node's id. Adding a node does NOT compile the Blueprint — call compile_anim_blueprint when the graph is complete.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `node_class` | string | yes | AnimGraph node class, e.g. 'AnimGraphNode_SequencePlayer', 'AnimGraphNode_Slot', 'AnimGraphNode_LayeredBoneBlend' |
| `animation_path` | string |  | For asset-player nodes: the UAnimSequence or UBlendSpace to play. Must share the AnimBP's skeleton. |
| `state_name` | string |  | Optional: add the node inside this state instead of the AnimGraph |
| `machine_name` | string |  | State machine containing state_name (required when state_name is given) |
| `position_x` | number |  | Node X position in the graph (default: 0) |
| `position_y` | number |  | Node Y position in the graph (default: 0) |

### `animgraph_connect_pose`

Connect one AnimGraph node's output pose to another's input pose. Node ids come from animgraph_add_node or animgraph_describe; pass 'root' as to_node to connect into the graph's Output Pose node (or, inside a state, its Result node) — which is the connection that makes the graph actually evaluate anything. A pose input accepts exactly one source, so connecting replaces whatever was there. Nodes with several pose inputs (LayeredBoneBlend's BasePose and BlendPoses_0, ApplyAdditive's Base and Additive) need to_pin to disambiguate — animgraph_describe lists the pin names.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `from_node` | string | yes | Node id whose output pose is the source |
| `to_node` | string | yes | Node id to connect into, or 'root' for the graph's Output Pose / Result node |
| `to_pin` | string |  | Name of the destination pose pin, when the target has more than one (e.g. 'BasePose', 'Additive') |
| `from_pin` | string |  | Name of the source pose pin, when the source has more than one |
| `state_name` | string |  | Optional: operate inside this state's graph instead of the AnimGraph |
| `machine_name` | string |  | State machine containing state_name (required when state_name is given) |

### `animgraph_describe`

Read back an Animation Blueprint's graph topology in one call: every node with its id, class, title, position and pins, plus a deduplicated edge list. Pass state_name + machine_name to describe the inside of a state instead of the top-level AnimGraph. This is the verify half of AnimGraph authoring — the node ids it returns are what animgraph_connect_pose and animgraph_set_node_property take.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `state_name` | string |  | Optional: describe this state's inner graph instead of the AnimGraph |
| `machine_name` | string |  | State machine containing state_name (required when state_name is given) |
| `include_hidden_pins` | boolean |  | Include pins hidden in the editor (default: false) |

### `animgraph_list_node_types`

List the AnimGraph node classes available to animgraph_add_node, with their menu category and pose pins. The engine ships ~130 of them — sequence and blend space players, blends (LayeredBoneBlend, ApplyAdditive, BlendListByBool), the Slot node that montages play into, skeletal controls (TwoBoneIK, ModifyBone), Inertialization, Mirror, cached poses, and linked anim layers. Filter with the `filter` argument to keep the response small.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `filter` | string |  | Case-insensitive substring to match against the class name (e.g. 'Blend', 'Slot', 'IK') |
| `limit` | integer |  | Maximum classes to return (default: 60, max: 300) |

### `animgraph_set_node_property`

Set a property on an AnimGraph node. Most anim node settings live on the node's inner `Node` struct (a Slot node's SlotName, a SequencePlayer's PlayRate and bLoopAnimation, a LayeredBoneBlend's LayerSetup), so this tool looks the property up on the node object first and then inside that struct — pass the bare name, e.g. 'SlotName', not 'Node.SlotName'. Accepts both UE text syntax and native JSON values. Use animgraph_describe to see what a node exposes.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `node_id` | string | yes | Node id from animgraph_add_node or animgraph_describe |
| `property_name` | string | yes | Property name, e.g. 'SlotName', 'PlayRate', 'bLoopAnimation' |
| `value` | string | yes | New value. UE text syntax or JSON, e.g. 'UpperBody', '1.5', 'true'. |
| `state_name` | string |  | Optional: the node lives inside this state's graph |
| `machine_name` | string |  | State machine containing state_name (required when state_name is given) |

### `compile_anim_blueprint`

Compile an Animation Blueprint and report the result: error and warning counts plus the compiler messages. None of the AnimGraph editing tools compile on their own — they mark the Blueprint modified and leave compilation to you, so a batch of edits costs one compile instead of ten. Call this at the end of any authoring sequence: it is the only way to know that a state machine is actually valid (unconnected entry, empty states and dangling pose links all show up here). Pass save=true to write the asset to disk when it compiles clean.

*long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `save` | boolean |  | Save the asset when compilation produces no errors (default: false) |

### `remove_anim_state`

Remove a state from a state machine, together with every transition into or out of it. The result reports which transitions went with it. If the removed state was the machine's entry state, the machine is left with no starting state and will not compile until set_anim_state_machine_entry is called.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `machine_name` | string | yes | Name of the state machine |
| `state_name` | string | yes | State to remove |

### `remove_anim_transition`

Remove a transition between two states, leaving both states in place. To disable a transition without losing its rule, use set_anim_transition_rule with rule_type 'Never' instead.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `machine_name` | string | yes | Name of the state machine |
| `from_state` | string | yes | Source state of the transition |
| `to_state` | string | yes | Target state of the transition |

### `set_anim_state_animation`

Put an animation into a state machine state and wire it to the state's Result node. This is the step that makes a state DO something: add_anim_state creates a state whose graph contains only an empty Result node, and a state left that way evaluates to reference pose (the character T-poses in that state). Accepts an AnimSequence (creates a Sequence Player) or a BlendSpace (creates a Blend Space Player). Any existing player node in the state is replaced. Call compile_anim_blueprint afterwards to verify.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `machine_name` | string | yes | Name of the state machine |
| `state_name` | string | yes | Name of the state to fill |
| `animation_path` | string | yes | UAnimSequence or UBlendSpace to play in this state. Must share the AnimBP's skeleton. |
| `loop` | boolean |  | Loop the animation (default: true — the usual choice for locomotion states) |
| `play_rate` | number |  | Play rate multiplier (default: 1.0) |

### `set_anim_state_machine_entry`

Set which state a state machine starts in, by wiring its Entry node to that state. A state machine whose entry is unconnected has no starting state and fails to compile — this is the step that is easy to forget after building states with add_anim_state.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `machine_name` | string | yes | Name of the state machine |
| `state_name` | string | yes | State the machine should start in |

### `set_anim_transition_rule`

Author the rule that decides when a state machine transition is taken. This is the step add_anim_transition cannot do on its own: a freshly created transition's rule graph leaves bCanEnterTransition at false, so the transition never fires no matter what the game does. Rule types:   Always     — always true. Combine with a state whose animation must play out, or with add_anim_transition's blend time, for unconditional flow.   Never      — always false. Useful to disable a transition without deleting it.   BoolVariable — reads a bool variable on the Animation Blueprint (add it with add_variable and drive it from NativeUpdateAnimation / BlueprintUpdateAnimation). The everyday case: 'IsMoving', 'IsFalling'. Set invert=true for 'not'.   CurveValue — true while a named animation curve exceeds threshold; lets the animation itself decide when it may be interrupted.   Automatic  — hands the decision to the engine's automatic rule, which fires as the state's sequence player approaches its end. The right answer for 'play this out, then move on'.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `machine_name` | string | yes | Name of the state machine |
| `from_state` | string | yes | Source state of the transition |
| `to_state` | string | yes | Target state of the transition |
| `rule_type` | enum(Always\|Never\|BoolVariable\|CurveValue\|Automatic) | yes | Which rule to author |
| `variable_name` | string |  | For BoolVariable: the bool variable on the AnimBP to read (e.g. 'IsMoving') |
| `curve_name` | string |  | For CurveValue: the animation curve to read |
| `threshold` | number |  | For CurveValue: the transition is taken while the curve exceeds this (default: 0.5) |
| `invert` | boolean |  | For BoolVariable: take the transition when the variable is FALSE (default: false) |

### `set_anim_transition_settings`

Set a transition's blend properties: crossfade duration, interpolation curve, per-bone blend profile, and priority order (when two transitions out of one state are both true in the same frame, the lower priority number wins). Duration is the single biggest lever on how a state machine feels — 0.1s reads as a snap, 0.3s as a settle.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimBlueprint |
| `machine_name` | string | yes | Name of the state machine |
| `from_state` | string | yes | Source state of the transition |
| `to_state` | string | yes | Target state of the transition |
| `duration` | number |  | Crossfade duration in seconds (e.g. 0.2) |
| `blend_mode` | enum(Linear\|Cubic\|HermiteCubic\|Sinusoidal\|QuadraticInOut\|CubicInOut\|QuarticInOut\|QuinticInOut\|CircularIn\|CircularOut\|CircularInOut\|ExpIn\|ExpOut\|ExpInOut\|Custom) |  | Interpolation curve for the crossfade |
| `blend_profile` | string |  | Per-bone blend profile name from the skeleton, or empty to clear |
| `priority_order` | integer |  | Evaluation priority; lower wins when several transitions are simultaneously true |

### `validate_animation_setup`

Validate a complete animation setup without modifying anything. Provide any of: anim_blueprint, skeletal_mesh, montages[], sequences[]. Reports skeleton_mismatch across all supplied assets (against the first skeleton found, or expected_skeleton), missing_asset paths, anim_blueprint_not_compiled, state machine issues per machine (state_without_animation, transition_without_rule for non-automatic transitions whose rule graph has no logic, unreachable_state from the entry state, no_entry_state, machine_without_states), montage issues (montage_no_slots, montage_no_sections, montage_zero_length, plus montage_validate findings) and sequence_zero_length. Issues carry rule, severity, asset, where and message; counts per rule; deterministic=true. Nothing is compiled or saved.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `anim_blueprint` | string |  | AnimBlueprint asset path |
| `skeletal_mesh` | string |  | Skeletal mesh asset path expected to drive the setup |
| `expected_skeleton` | string |  | Skeleton asset path every asset must target (default: the first skeleton found) |
| `montages` | array<string> |  | AnimMontage asset paths (up to 32) |
| `sequences` | array<string> |  | AnimSequence asset paths (up to 64) |

## AnimMixer (4 tools)

### `animmixer_add_animation`

Add an animation sequence to a layer of the Animation Mixer track, starting at start_seconds. Use animmixer_add_layer to create layers first.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Level sequence asset path. |
| `binding_id` | string | yes | Object binding GUID with a mixer track. |
| `layer_index` | integer | yes | Target layer index (from animmixer_add_layer). |
| `anim_sequence_path` | string | yes | UAnimSequenceBase asset path. |
| `start_seconds` | number |  | Start time in seconds (default 0). |

### `animmixer_add_layer`

Add a new layer to the Animation Mixer track on a binding (layers blend/mask over each other). Returns the new layer index. Requires animmixer_add_track first.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Level sequence asset path. |
| `binding_id` | string | yes | Object binding GUID with a mixer track. |

### `animmixer_add_track`

Add an Animation Mixer track to an object binding in a level sequence (the binding should be a skeletal-mesh actor). Idempotent: returns the existing track if one is already present. Marks the sequence dirty.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Level sequence asset path. |
| `binding_id` | string | yes | Object binding GUID to attach the mixer track to. |

### `animmixer_get_info`

Report the Animation Mixer track on a binding: layer count and the number of sections per layer.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Level sequence asset path. |
| `binding_id` | string | yes | Object binding GUID. |

## Animation (10 tools)

### `add_skeleton_socket`

Add (or overwrite) a named socket on a Skeleton at a given bone, with an optional relative transform. Accepts a Skeleton or a SkeletalMesh content path. Mark the skeleton dirty; save to persist.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of a Skeleton or SkeletalMesh |
| `socket_name` | string | yes | Name for the new socket (e.g. 'SwordSocket') |
| `bone_name` | string | yes | Bone the socket attaches to (e.g. 'hand_r') |
| `loc_x` | number |  | Relative location X (default 0) |
| `loc_y` | number |  | Relative location Y (default 0) |
| `loc_z` | number |  | Relative location Z (default 0) |
| `rot_pitch` | number |  | Relative rotation Pitch (default 0) |
| `rot_yaw` | number |  | Relative rotation Yaw (default 0) |
| `rot_roll` | number |  | Relative rotation Roll (default 0) |

### `create_ik_retargeter`

Create an IK Retargeter linking a source and target IK Rig: sets source/target, assigns rigs to all retarget ops (required, or output is a static T-pose), and fuzzy-maps chains.

| Argument | Type | Required | Description |
|---|---|---|---|
| `source_ik_rig` | string | yes | Source IK Rig content path |
| `target_ik_rig` | string | yes | Target IK Rig content path |
| `output_path` | string | yes | Output IK Retargeter content path |

### `create_ik_rig`

Create an IK Rig from a skeletal mesh with auto-generated retarget chains. Optionally drops the UE5 finger metacarpal chains (recommended when retargeting from UE4 sources to avoid finger claw).

| Argument | Type | Required | Description |
|---|---|---|---|
| `mesh_path` | string | yes | SkeletalMesh content path |
| `output_path` | string | yes | Output IK Rig content path (e.g. /Game/_Game/Animations/Retarget/IK_Hero) |
| `drop_finger_metacarpals` | boolean |  | Remove the 8 metacarpal chains (default true) |

### `get_skeleton_info`

Retrieve structural information about a skeletal mesh asset: total bone count, the first 50 bone names in hierarchy order, socket count, socket names, and the mesh bounding box. Useful for animation rigging and attachment workflows.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `mesh_path` | string | yes | Content path to the USkeletalMesh asset (e.g., '/Game/Characters/SK_Mannequin') |

### `list_animation_assets`

List UAnimSequence and UAnimMontage assets found in the Asset Registry under a given content path. Optionally filter by skeleton asset or name substring. Returns asset name, path, type (Sequence or Montage), duration in seconds, and frame count.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `skeleton_path` | string |  | Optional content path to a USkeleton asset to filter results by skeleton (e.g., '/Game/Characters/SK_Mannequin_Skeleton') |
| `path` | string |  | Content path prefix to search under (default: '/Game/') |
| `name_filter` | string |  | Optional substring filter applied to asset names (case-insensitive) |
| `limit` | integer |  | Maximum number of results to return (default: 100) |

### `list_skeleton_sockets`

List sockets on a Skeleton (and mesh-only sockets if a SkeletalMesh path is given): name, bone, source.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of a Skeleton or SkeletalMesh |

### `play_animation`

Play a single UAnimSequence on an actor's SkeletalMeshComponent using AnimationSingleNode mode. This overrides any AnimBlueprint currently assigned. Useful for previewing animations directly in the editor viewport.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor containing a SkeletalMeshComponent |
| `animation_path` | string | yes | Content path to the UAnimSequence asset (e.g., '/Game/Animations/AM_Run') |
| `looping` | boolean |  | Whether the animation should loop (default: false) |
| `play_rate` | number |  | Playback rate multiplier; 1.0 = normal speed, 2.0 = double speed (default: 1.0) |

### `retarget_animations`

Batch-retarget animation sequences through an IK Retargeter and move the results into an output folder. Handles the duplicate_and_retarget + relocation gotchas automatically.

| Argument | Type | Required | Description |
|---|---|---|---|
| `source_mesh` | string | yes | Source SkeletalMesh content path |
| `target_mesh` | string | yes | Target SkeletalMesh content path |
| `retargeter` | string | yes | IK Retargeter content path |
| `anim_paths` | array<string> | yes | Source AnimSequence content paths to retarget |
| `output_folder` | string | yes | Destination folder for the retargeted anims (e.g. /Game/_Game/Characters/Hero/Anims/Sword) |

### `set_animation_blueprint`

Assign an Animation Blueprint to a SkeletalMeshComponent by loading the UAnimBlueprint asset, extracting its generated class, and calling SetAnimInstanceClass(). This determines how the skeleton is driven at runtime.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor containing a SkeletalMeshComponent |
| `anim_bp_path` | string | yes | Content path to the UAnimBlueprint asset (e.g., '/Game/Characters/ABP_Mannequin') |

### `set_skeletal_mesh`

Set the skeletal mesh asset on an actor's SkeletalMeshComponent. Works on any actor that has a SkeletalMeshComponent (e.g., SkeletalMeshActor, Character). Wrap in an undo transaction.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor containing a SkeletalMeshComponent |
| `mesh_path` | string | yes | Content path to the USkeletalMesh asset (e.g., '/Game/Characters/SK_Mannequin') |

## Asset (6 tools)

### `delete_asset`

Delete an asset from the content browser. By default checks for references to prevent breaking dependencies.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the asset to delete |
| `check_references` | boolean |  | Check for references before deleting (default: true) |

### `duplicate_asset`

Create a copy of an existing asset at a new location with a new name.

| Argument | Type | Required | Description |
|---|---|---|---|
| `source_path` | string | yes | Content path of the asset to duplicate |
| `dest_path` | string | yes | Content path for the duplicate (e.g., '/Game/Meshes/') |
| `new_name` | string | yes | Name for the duplicate |

### `get_asset_info`

Get detailed metadata about an asset: class, package, tags, size, references, and key properties.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Full asset path (e.g., '/Game/Meshes/SM_Chair.SM_Chair') |

### `import_asset`

Import a file from the filesystem into the content browser. Supports FBX, OBJ, PNG, JPG, WAV, and other standard formats.

*long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `source_path` | string | yes | Absolute filesystem path to import (e.g., 'C:/Models/chair.fbx') |
| `destination_path` | string | yes | Content path destination (e.g., '/Game/Meshes/') |

### `list_assets`

List assets in the content browser with filtering by path, class, and name. Returns asset paths, classes, and file sizes.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to browse (e.g., '/Game/', '/Game/Blueprints/'). Default: '/Game/' |
| `class_filter` | string |  | Filter by asset class (e.g., 'StaticMesh', 'Material', 'Blueprint', 'Texture2D') |
| `name_filter` | string |  | Filter by asset name (substring match) |
| `recursive` | boolean |  | Search subdirectories recursively (default: true) |
| `limit` | integer |  | Maximum number of results (default: 100) |

### `rename_asset`

Rename or move an asset to a new path. Automatically fixes up references.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Current asset path |
| `new_path` | string | yes | New path/name for the asset (e.g., '/Game/NewFolder/NewName') |

## AssetManagement (7 tools)

### `create_folder`

Create a new folder in the content browser. Creates all intermediate directories if needed. Does nothing if the folder already exists.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `folder_path` | string | yes | Content folder path to create (e.g., '/Game/UI/Icons', '/Game/Materials/Environment') |

### `find_unused_assets`

Find assets that are not referenced by any other asset in the project. These are candidates for cleanup/deletion. Note: some assets may be referenced at runtime via soft references or data tables.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to scan (default: '/Game/') |
| `class_filter` | string |  | Filter by asset class (e.g., 'Texture2D', 'Material') |
| `limit` | integer |  | Maximum results (default: 50) |

### `get_asset_references`

Get the dependency graph for an asset. 'dependencies' shows what this asset depends ON (textures, materials, meshes it uses). 'referencers' shows what OTHER assets reference this one. Use to understand impact before modifying or deleting.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the asset to query |
| `direction` | enum(dependencies\|referencers) | yes | Query direction |
| `recursive` | boolean |  | Follow the chain recursively (default: false — only direct references) |
| `limit` | integer |  | Maximum results (default: 50) |

### `get_asset_size_report`

Report the largest assets in the project by disk size. Useful for finding optimization targets. Filter by class to focus on textures, meshes, etc.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to scan (default: '/Game/') |
| `class_filter` | string |  | Filter by asset class (e.g., 'Texture2D', 'StaticMesh') |
| `limit` | integer |  | Maximum results (default: 20) |

### `import_asset_with_settings`

Import an asset file (FBX, OBJ, PNG, TGA, WAV, etc.) into the project with detailed import settings. For FBX: control mesh type, material/texture import, mesh combining, and animation import. For textures: control compression and sRGB. Uses Unreal's automated import pipeline.

| Argument | Type | Required | Description |
|---|---|---|---|
| `source_path` | string | yes | Absolute filesystem path to the file to import (e.g., 'C:/Art/character.fbx') |
| `destination_path` | string | yes | Content path for the imported asset (e.g., '/Game/Meshes/') |
| `import_as` | enum(StaticMesh\|SkeletalMesh) |  | For FBX files: import as StaticMesh or SkeletalMesh |
| `import_materials` | boolean |  | Import materials from FBX (default: true) |
| `import_textures` | boolean |  | Import textures from FBX (default: true) |
| `combine_meshes` | boolean |  | Combine all meshes into one (default: false) |
| `import_animations` | boolean |  | Import animations from FBX (default: true) |
| `auto_generate_collision` | boolean |  | Auto-generate collision for static meshes (default: true) |
| `collision_policy` | enum(replace\|skip\|error) |  | v5: when the destination asset already exists: replace (default, previous behaviour), skip (return the existing asset without importing) or error (refuse) |
| `texture_compression` | enum(Default\|NormalMap\|Masks\|HDR\|UserInterface2D) |  | Texture compression after import |
| `srgb` | boolean |  | sRGB for imported textures |

### `move_assets_to_folder`

Bulk move multiple assets to a destination folder with automatic reference fixup. Creates the destination folder if it doesn't exist.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_paths` | array<string> | yes | Array of asset content paths to move |
| `destination_folder` | string | yes | Destination content folder (e.g., '/Game/UI/Icons') |

### `set_texture_settings`

Modify texture settings after import: compression type, sRGB, max resolution, LOD group, and mipmap generation. Only provided fields are changed. The texture is re-saved after modification.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the Texture2D asset |
| `compression` | enum(Default\|NormalMap\|Masks\|HDR\|UserInterface2D\|Alpha\|BC7) |  | Texture compression setting |
| `srgb` | boolean |  | Whether texture uses sRGB color space (true for color textures, false for data/masks) |
| `max_size` | integer |  | Maximum texture dimension (256, 512, 1024, 2048, 4096, 8192) |
| `lod_group` | enum(World\|WorldNormalMap\|WorldSpecular\|Character\|CharacterNormalMap\|Weapon\|UI\|Effects) |  | Texture LOD group |
| `generate_mipmaps` | boolean |  | Whether to generate mipmaps (default: true) |

## Audio (3 tools)

### `get_sound_info`

Get detailed information about a sound asset: type, duration, and for SoundWaves also channel count, sample rate, looping flag, and sound group.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sound_path` | string | yes | Content path to the USoundBase asset (e.g., '/Game/Audio/SFX/SW_Gunshot') |

### `set_audio_properties`

Set audio component properties on any actor that has a UAudioComponent. Only provided parameters are modified. Use sound_path to swap the sound asset.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor containing the AudioComponent |
| `volume_multiplier` | number |  | Volume multiplier to apply to the sound (0.0 - 2.0+) |
| `pitch_multiplier` | number |  | Pitch multiplier to apply to the sound (0.1 - 4.0) |
| `attenuation_distance` | number |  | Override the maximum attenuation distance in cm |
| `is_spatialized` | boolean |  | Whether the sound is spatialized in 3D space |
| `auto_activate` | boolean |  | Whether the sound plays automatically when the actor begins play |
| `sound_path` | string |  | Optional content path to change the sound asset (e.g., '/Game/Audio/SFX/SW_Wind') |

### `spawn_sound`

Spawn an AmbientSound actor in the current level at a given world position. The sound_path must point to a valid USoundBase asset (SoundWave or SoundCue) in the content browser.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sound_path` | string | yes | Content path to the USoundBase asset (e.g., '/Game/Audio/SFX/SW_Ambience') |
| `x` | number |  | X position in the world (default: 0) |
| `y` | number |  | Y position in the world (default: 0) |
| `z` | number |  | Z position in the world (default: 0) |
| `label` | string |  | Optional actor label shown in the scene outliner |
| `volume_multiplier` | number |  | Volume multiplier applied to the sound (default: 1.0) |
| `pitch_multiplier` | number |  | Pitch multiplier applied to the sound (default: 1.0) |
| `auto_activate` | boolean |  | Whether the sound plays automatically when the actor is spawned (default: true) |

## Batch (3 tools)

### `batch_set_property`

Set the same property value on multiple actors at once. Uses UE's property system for value parsing.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_names` | array<string> | yes | Array of actor labels |
| `property_name` | string | yes | Name of the UPROPERTY to set |
| `property_value` | string | yes | New value as a string (parsed by UE property system) |

### `batch_transform`

Apply the same transform change to multiple actors at once. Only provided fields are changed; omitted fields keep their current values.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_names` | array<string> | yes | Array of actor labels to transform |
| `x` | number |  | X position |
| `y` | number |  | Y position |
| `z` | number |  | Z position |
| `pitch` | number |  | Pitch rotation in degrees |
| `yaw` | number |  | Yaw rotation in degrees |
| `roll` | number |  | Roll rotation in degrees |
| `scale_x` | number |  | X scale |
| `scale_y` | number |  | Y scale |
| `scale_z` | number |  | Z scale |
| `relative` | boolean |  | If true, values are added to current transform. If false, values are set absolutely (default: false). |

### `find_actors`

Advanced actor query combining class, name, tag, and proximity filters. More powerful than list_actors for targeted searches.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `class_filter` | string |  | Filter by class name (substring match) |
| `name_pattern` | string |  | Filter by actor label (substring match, case-insensitive) |
| `tag` | string |  | Filter by actor tag (exact match) |
| `near_x` | number |  | Center X for proximity search |
| `near_y` | number |  | Center Y for proximity search |
| `near_z` | number |  | Center Z for proximity search |
| `radius` | number |  | Search radius around near_x/y/z (in cm) |
| `hidden_only` | boolean |  | Only return hidden actors |
| `visible_only` | boolean |  | Only return visible actors |
| `limit` | integer |  | Maximum results (default: 100) |

## Blueprint (65 tools)

### `add_bind_dispatcher_node`

Add a 'Bind Event to Dispatcher' node. This binds a custom event to listen to the dispatcher. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `dispatcher_name` | string | yes | Name of the event dispatcher to bind to |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_branch_node`

Add a Branch (if/else) node. Has an exec input, a boolean 'Condition' input, and 'True'/'False' exec outputs. Returns node ID and pin names.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_break_struct_node`

Add a 'Break [Struct]' node that decomposes a struct into individual member output pins. Works with any UScriptStruct: FVector, FRotator, FTransform, FLinearColor, FPostProcessSettings, FHitResult, etc. Returns node ID and all pins (one struct input + one output per struct member).

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `struct_type` | string | yes | Struct name (e.g., 'FVector', 'FPostProcessSettings', 'FLinearColor', 'FHitResult') |
| `graph_name` | string |  | Graph to add to (default: EventGraph) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_call_dispatcher_node`

Add a 'Call' node for an event dispatcher. This broadcasts the event to all bound listeners. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `dispatcher_name` | string | yes | Name of the event dispatcher to call |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_cast_node`

Add a Cast To node (dynamic cast). Has an Object input, exec input, success/fail exec outputs, and a typed output pin for the cast result. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `target_class` | string | yes | Class name to cast to (e.g., 'Character', 'Pawn', 'MyBlueprintClass') |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_child_component`

Add a component to a Blueprint, parented to ANY component including an INHERITED native one (e.g. CharacterMesh0) which the basic add_component cannot do. Optional attach socket, static mesh, and widget class. Recompiles.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Blueprint content path |
| `component_class` | string | yes | Component class name (e.g. 'StaticMeshComponent', 'WidgetComponent') |
| `component_name` | string | yes | Name for the new component |
| `parent_component` | string | yes | Parent component name (SCS or inherited native, e.g. 'CharacterMesh0') |
| `socket_name` | string |  | Optional bone/socket on the parent to attach to (e.g. 'hand_r') |
| `static_mesh` | string |  | Optional StaticMesh asset path (for StaticMeshComponent) |
| `widget_class` | string |  | Optional UserWidget Blueprint path (for WidgetComponent) |

### `add_comment_node`

Add a comment box to a graph for organization. Position/size it to visually group related nodes.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Graph name (default: EventGraph) |
| `text` | string | yes | Comment text |
| `x` | number |  | X position (default 0) |
| `y` | number |  | Y position (default 0) |
| `width` | number |  | Box width (default 400) |
| `height` | number |  | Box height (default 300) |

### `add_component`

Add a component to a Blueprint's Simple Construction Script (SCS). The component will appear in the Blueprint's component hierarchy.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `component_class` | string | yes | Component class name (e.g., 'StaticMeshComponent', 'PointLightComponent', 'BoxCollisionComponent') |
| `component_name` | string | yes | Name for the new component |
| `parent_component` | string |  | Name of parent component to attach to. If empty, attaches to root. |

### `add_create_widget_node`

Add a CreateWidget node to a Blueprint graph. Creates a UMG widget instance at runtime. Has a Class input (set via widget_class), Owning Player input, exec input/output, and a Return Value pin (the created widget). Use with AddToViewport to display the widget. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `widget_class` | string |  | Content path of the Widget Blueprint to create (e.g., '/Game/UI/WBP_HUD') |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_custom_event`

Add a custom event node to the event graph. Custom events can be called from other parts of the Blueprint. Returns the node ID.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `event_name` | string | yes | Name for the custom event |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_delay_node`

Add a Delay node (latent action). Has exec input/output and a 'Duration' float input. The output exec fires after the specified duration. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `duration` | number |  | Delay duration in seconds (default: 1.0) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_event_dispatcher`

Add an event dispatcher (multicast delegate) variable to a Blueprint. Event dispatchers allow Blueprints to broadcast events that other Blueprints can bind to.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `dispatcher_name` | string | yes | Name for the new event dispatcher |

### `add_event_node`

Add a built-in event node (BeginPlay, Tick, ActorBeginOverlap, ActorEndOverlap) to the event graph. Also accepts any UFunction name directly (e.g., 'ReceiveBeginPlay'). Returns the node ID for wiring.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `event_name` | enum(BeginPlay\|Tick\|ActorBeginOverlap\|ActorEndOverlap) | yes | Built-in event to add |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_flow_control_node`

Add a flow control macro node (DoOnce, FlipFlop, Gate, MultiGate, DoN). These are standard macro instances from the engine's StandardMacros library. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `flow_type` | enum(DoOnce\|FlipFlop\|Gate\|MultiGate\|DoN) | yes | Type of flow control node |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_for_each_loop_node`

Add a ForEachLoop macro instance node. Iterates over an array with 'Array Element' and 'Array Index' outputs, plus 'Loop Body' and 'Completed' exec outputs. Set with_break=true for the variant with a Break input. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `with_break` | boolean |  | If true, use ForEachLoopWithBreak instead of ForEachLoop (default: false) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_function_call_node`

Add a function call node to a Blueprint graph. Specify the target class and function name. Common classes: KismetSystemLibrary (PrintString, Delay), KismetMathLibrary (math ops), GameplayStatics (GetPlayerController, SpawnActor), Actor (SetActorLocation). Use 'Self' for Blueprint's own functions. Returns node ID and pin names for wiring.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph (e.g., 'EventGraph' or function name) |
| `function_name` | string | yes | Name of the function to call (e.g., 'PrintString', 'K2_SetActorLocation', 'Delay') |
| `target` | string | yes | Class owning the function (e.g., 'KismetSystemLibrary', 'Actor', 'GameplayStatics'). Use 'Self' for functions on this Blueprint. Required. |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_function_graph`

Create a new function graph in a Blueprint. Returns the function entry node ID. Use add_function_pin to add input/output parameters, and add nodes to build the function body.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `function_name` | string | yes | Name for the new function |
| `access` | enum(Public\|Protected\|Private) |  | Access specifier (default: Public) |
| `pure` | boolean |  | Whether the function is pure (no exec pins). Default: false |

### `add_function_pin`

Add an input parameter or output return value to a Blueprint function. Input pins become function parameters; Output pins become return values. A FunctionResult node is automatically created if needed for outputs.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `function_name` | string | yes | Name of the function to modify |
| `pin_name` | string | yes | Name for the new parameter |
| `pin_type` | enum(Boolean\|Integer\|Float\|String\|Vector\|Rotator\|Transform\|Object\|Name\|Text\|Byte) | yes | Type of the parameter |
| `direction` | enum(Input\|Output) | yes | Input = function parameter, Output = return value |

### `add_function_return_node`

Add a FunctionResult (return) node to a function graph. Required for functions that return values. Use add_function_pin with direction 'Output' to add return value pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `function_name` | string | yes | Name of the function graph |
| `node_x` | number |  | X position in graph (default: 600) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_get_all_actors_of_class_node`

Add a GetAllActorsOfClass function call node. Returns an array of all actors of the specified class in the world. Has a WorldContextObject input, ActorClass input, and OutActors array output. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `actor_class` | string |  | Optional: Actor class name to pre-fill on the class pin (e.g., 'StaticMeshActor') |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_input_action_event`

Add an Enhanced Input Action event node to a Blueprint graph. This creates an event that fires when the specified InputAction is triggered. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `action_path` | string | yes | Content path to the InputAction asset (e.g., '/Game/Input/IA_Jump') |
| `trigger_event` | enum(Started\|Triggered\|Completed\|Canceled\|Ongoing) |  | Which trigger event to respond to (default: Triggered) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_interface_to_blueprint`

Add a Blueprint Interface to an existing Blueprint, making it implement the interface's functions.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `interface_path` | string | yes | Content path of the Blueprint Interface to implement |

### `add_local_variable`

Add a local variable to a function graph. Local variables are scoped to the function and not visible outside of it.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `function_name` | string | yes | Name of the function to add the local variable to |
| `variable_name` | string | yes | Name of the new local variable |
| `variable_type` | enum(Boolean\|Integer\|Float\|String\|Vector\|Rotator\|Transform\|Object\|Name\|Text) | yes | Type of the variable |

### `add_make_array_node`

Add a Make Array node that constructs an array from individual elements. Connect inputs to define array contents. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_make_struct_node`

Add a 'Make [Struct]' node that constructs a struct from individual member values. Works with any UScriptStruct: FVector, FRotator, FTransform, FLinearColor, FPostProcessSettings, FHitResult, etc. Returns node ID and all pins (one input per struct member + one struct output).

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `struct_type` | string | yes | Struct name (e.g., 'FVector', 'FPostProcessSettings', 'FLinearColor', 'FHitResult') |
| `graph_name` | string |  | Graph to add to (default: EventGraph) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_reroute_node`

Add a reroute (knot) node for tidying long wires. Connect it like any node: one input, one output, type adapts to what you wire in.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Graph name (default: EventGraph) |
| `x` | number |  | X position (default 0) |
| `y` | number |  | Y position (default 0) |

### `add_select_node`

Add a Select node that picks one of several values based on an index or boolean input. Similar to a ternary operator. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_sequence_node`

Add a Sequence node that executes multiple output pins in order. Has an exec input and numbered 'Then' exec outputs (Then_0, Then_1, ...). Useful for organizing sequential logic. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `num_outputs` | integer |  | Number of 'Then' output exec pins (default: 2, max: 64) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_set_struct_fields_node`

Add a 'Set Members in [Struct]' node that sets individual fields of a struct. Unlike Make, this takes an existing struct as input and selectively modifies fields. Has exec pins (not pure). Use this for FPostProcessSettings, FHitResult, and other complex structs. Returns node ID and all pins (exec in/out, struct in/out, one input per struct member).

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `struct_type` | string | yes | Struct name (e.g., 'FPostProcessSettings', 'FVector', 'FLinearColor') |
| `graph_name` | string |  | Graph to add to (default: EventGraph) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_spawn_actor_node`

Add a SpawnActorFromClass node. Has a Class input, SpawnTransform input, exec input/output, and a return value pin for the spawned actor. Optionally pre-fills the actor class. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `actor_class` | string |  | Optional: Actor class name to pre-fill (e.g., 'StaticMeshActor', 'PointLight') |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_switch_on_enum_node`

Add a Switch on Enum node. Creates exec output pins for each enum value plus a Default pin. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph |
| `enum_path` | string | yes | Content path to the enum asset (e.g., '/Game/Enums/E_Elements') |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_switch_on_int_node`

Add a Switch on Integer node. Has an exec input, an integer 'Selection' input, a 'Default' exec output, and numbered case exec outputs. Returns node ID and all pin names.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph |
| `num_cases` | integer |  | Number of integer cases to create (default: 2) |
| `start_index` | integer |  | Starting integer value for cases (default: 0) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_switch_on_string_node`

Add a Switch on String node. Has an exec input, a string 'Selection' input, a 'Default' exec output, and named case exec outputs. Returns node ID and all pin names.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph |
| `cases` | array<string> |  | JSON array of case string values (e.g., ["Idle","Walking","Running"]) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_timeline_node`

Add a Timeline node to a Blueprint graph. Timelines allow you to animate float/vector/color values over time with keyframes. Has Play, PlayFromStart, Stop, Reverse exec inputs, Update/Finished/Direction exec outputs, and a float output for each track. Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `timeline_name` | string | yes | Name for the timeline |
| `length` | number |  | Timeline length in seconds (default: 5.0) |
| `auto_play` | boolean |  | Whether the timeline auto-plays on BeginPlay (default: false) |
| `loop` | boolean |  | Whether the timeline loops (default: false) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_variable`

Add a new variable to a Blueprint with specified type and properties.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `variable_name` | string | yes | Name of the new variable |
| `variable_type` | enum(Boolean\|Integer\|Float\|String\|Vector\|Rotator\|Transform\|Object\|Class\|Name\|Text) | yes | Type of the variable |
| `instance_editable` | boolean |  | Whether the variable is editable per-instance (default: true) |
| `category` | string |  | Category for grouping in the details panel |
| `default_value` | string |  | Default value as a string |

### `add_variable_get_node`

Add a variable getter (Get) node to a Blueprint graph. For self variables, omit target_class. For EXTERNAL class variables (e.g., getting a variable from BP_GameInstance or another Blueprint), provide the target_class name or content path. Returns node ID and output pin name for wiring.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph (e.g., 'EventGraph' or function name) |
| `variable_name` | string | yes | Name of the variable to get |
| `target_class` | string |  | Optional: Class that owns the variable. Use 'Self' or omit for Blueprint's own variables. For external variables, provide class name (e.g., 'BP_GameInstance', 'GameplayStatics', 'CharacterMovementComponent'). Also accepts content paths like '/Game/BP/BP_GameInstance'. |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_variable_set_node`

Add a variable setter (Set) node to a Blueprint graph. Has exec input/output pins and a value input pin. For EXTERNAL class variables, provide target_class. Returns node ID and pin names for wiring.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph (e.g., 'EventGraph' or function name) |
| `variable_name` | string | yes | Name of the variable to set |
| `target_class` | string |  | Optional: Class that owns the variable. Use 'Self' or omit for Blueprint's own variables. For external variables, provide class name or content path (e.g., '/Game/BP/BP_GameInstance'). |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `add_while_loop_node`

Add a WhileLoop macro instance node. Has a 'Condition' boolean input, 'Loop Body' exec output (runs while condition is true), and 'Completed' exec output (runs when condition becomes false). Returns node ID and all pins.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Name of the graph (default: EventGraph) |
| `node_x` | number |  | X position in graph (default: 0) |
| `node_y` | number |  | Y position in graph (default: 0) |

### `apply_blueprint_patch`

Typed Blueprint graph patch with preflight. operations[] (1..64, in order): add_call_function{alias,function,target_class}, add_variable_get/add_variable_set{alias,variable}, add_event{alias,event}, add_custom_event{alias,name}, add_branch{alias}, remove_node{node}, connect{node,pin,to_node,to_pin}, disconnect{node,pin}, set_default{node,pin,value}, move{node,x,y}. Node references are GUIDs or $alias for nodes added earlier in the patch. mode=preview (default, or dry_run=true) validates every reference, function, variable, pin and schema connection against the live graph and returns applicability, problems and the revision from validate_blueprint without changing anything. mode=apply refuses a stale expected_revision, applies through the existing node tools, reads back links/defaults, returns revision_after and aliases, and optionally compiles (compile=true) returning the compiler result. Mid-patch failure lists surviving operations (atomic=false); recovery is editor Undo. Requires Scene scope.

*PIE-off · preview · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Graph name (default EventGraph) |
| `mode` | enum(preview\|apply) |  | preview (default) or apply |
| `expected_revision` | string |  | Revision from validate_blueprint or a previous preview; apply refuses on mismatch |
| `compile` | boolean |  | Compile after a successful apply (default false) |
| `operations` | array<object> | yes | Ordered typed operations |

### `compile_blueprint`

Compile a Blueprint and report any errors or warnings.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint to compile |

### `connect_pins`

Wire two Blueprint node pins together. Connect an output pin on one node to an input pin on another. The schema handles type checking and will report errors for incompatible types. Use get_node_pins to discover available pin names.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph containing both nodes |
| `source_node_id` | string | yes | GUID of the source node (from node creation or get_blueprint_info) |
| `source_pin_name` | string | yes | Name of the output pin on the source node |
| `target_node_id` | string | yes | GUID of the target node |
| `target_pin_name` | string | yes | Name of the input pin on the target node |

### `create_blueprint`

Create a new Blueprint class asset. Specify a content path and parent class. The Blueprint is saved and ready for editing.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new blueprint (e.g., '/Game/Blueprints/BP_MyActor') |
| `parent_class` | string |  | Parent class name (e.g., 'Actor', 'Pawn', 'Character', 'PlayerController'). Default: 'Actor' |

### `create_blueprint_interface`

Create a new Blueprint Interface asset. Blueprint Interfaces define function signatures that multiple Blueprints can implement.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new interface (e.g., '/Game/Interfaces/BPI_Interactable') |

### `create_enum`

Create a UserDefinedEnum asset with optional initial entries. Entries can be added as a JSON array of strings.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new enum (e.g., '/Game/Enums/E_Elements') |
| `entries_json` | array<string> |  | JSON array of enum entry display names (e.g., ["Fire","Ice","Lightning"]) |

### `delete_nodes`

Delete one or more nodes from a graph (all-or-nothing). Function entry/result nodes and other protected nodes are refused with a reason. Connections to deleted nodes are broken cleanly.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Graph name (default: EventGraph) |
| `node_ids` | array<string> | yes | GUIDs of the nodes to delete |

### `describe_graph`

Dump a Blueprint graph's full topology in one call: every node (id, type, title, position, pins) plus a deduplicated directed connection list. Use this to verify your edits instead of calling get_node_pins per node.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Graph name (default: EventGraph) |
| `include_pins` | boolean |  | Include per-node pin details (default true). Set false for a compact node+edges view. |
| `include_hidden_pins` | boolean |  | Include hidden pins (default false) |

### `disconnect_pin`

Break all connections on a specific pin of a node. Use get_node_pins to see current connections before disconnecting.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph |
| `node_id` | string | yes | GUID of the node |
| `pin_name` | string | yes | Name of the pin to disconnect |

### `find_orphaned_nodes`

Find nodes with no connections at all (excluding comments). Candidates for delete_nodes cleanup.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Graph name (default: EventGraph) |

### `get_blueprint_info`

Get comprehensive information about a Blueprint: parent class, components, variables, functions, and event graph structure.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint (e.g., '/Game/Blueprints/BP_MyActor') |

### `get_blueprint_interfaces`

List all interfaces implemented by a Blueprint, including their names, paths, and function signatures.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |

### `get_execution_paths`

Trace execution flow from each event/entry node by following exec pins. Returns the chains of nodes each event reaches — use to verify wiring (e.g. 'does BeginPlay reach SetActorHidden?').

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Graph name (default: EventGraph) |

### `get_function_signature`

Get the exact Blueprint node pin signature for a function. Returns all input and output pin names and types exactly as they will appear when using add_function_call_node + connect_pins. Use list_class_functions first to find the correct function name.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `class_name` | string | yes | UClass owning the function |
| `function_name` | string | yes | Exact function name |

### `get_node_pins`

Get detailed pin information for nodes in a Blueprint graph. Returns pin names, directions (Input/Output), types, default values, and current connections. Essential for discovering pin names before using connect_pins. If node_id is omitted, returns all nodes with their pins.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph |
| `node_id` | string |  | GUID of a specific node (optional - if omitted, returns all nodes with pins) |

### `list_class_functions`

List all callable Blueprint functions on a UClass. Essential for discovering exact function names before using add_function_call_node. Returns function name, whether it's pure/static, return type, and optionally full parameter details. Use name_filter to narrow results (e.g., name_filter='Print' on KismetSystemLibrary).

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `class_name` | string | yes | UClass name to inspect (e.g., 'Actor', 'KismetSystemLibrary', 'Character', 'GameplayStatics'). Searches with A/U prefix fallbacks. |
| `name_filter` | string |  | Filter functions by name (substring match, case-insensitive) |
| `include_parent_classes` | boolean |  | Include inherited functions from parent classes (default: true) |
| `include_parameters` | boolean |  | Include full parameter details for each function (default: false — set true for specific functions) |
| `limit` | integer |  | Maximum results (default: 50, max: 200) |

### `list_node_types`

List the concrete K2 node classes available in this engine (via reflection), optionally filtered by name substring. Note: creation tools exist for the most common types (see search_tools query 'add node'); others can often be created via add_function_call_node or execute_python.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `filter` | string |  | Optional name substring filter (e.g. 'Switch') |
| `limit` | integer |  | Max results (default 100) |

### `move_node`

Move an existing node to a new graph position. Layout-only; no pins or connections change.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Graph name (default: EventGraph) |
| `node_id` | string | yes | GUID of the node |
| `x` | number | yes | New X position |
| `y` | number | yes | New Y position |

### `remove_node`

Remove a node from a Blueprint graph. All connections to/from the node will be broken. Cannot remove the function entry node of a function graph.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph |
| `node_id` | string | yes | GUID of the node to remove |

### `set_component_object_property`

Set an object- or class-reference property on a Blueprint component by asset path (e.g. WidgetComponent.WidgetClass, StaticMeshComponent.StaticMesh). For class properties (TSubclassOf), pass a Blueprint/asset path and its generated class is used. Recompiles the Blueprint.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Blueprint content path |
| `component_name` | string | yes | Component name (e.g. 'HealthBarWidget') |
| `property_name` | string | yes | Property to set (e.g. 'WidgetClass', 'StaticMesh') |
| `value_asset_path` | string | yes | Content path of the asset/Blueprint to assign |

### `set_component_property`

Set a default property value on a component in a Blueprint. Use get_blueprint_info first to discover component names.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `component_name` | string | yes | Name of the component |
| `property_name` | string | yes | Property to set |
| `property_value` | string | yes | Value as string |

### `set_component_socket`

Set a Blueprint component's parent attach socket/bone (e.g. attach a Sword component to 'hand_r'). The component must already be a child of a SkeletalMeshComponent. Pass an empty socket_name to clear it. Requires the Blueprint to be recompiled (done automatically).

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint (e.g. /Game/_Game/Characters/Hero/BP_StraySparkCharacter) |
| `component_name` | string | yes | Name of the component whose attach socket to set (e.g. 'Sword') |
| `socket_name` | string | yes | Bone or socket name on the parent to attach to (e.g. 'hand_r'). Empty clears it. |

### `set_node_comment`

Set or clear the comment bubble on an existing node.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string |  | Graph name (default: EventGraph) |
| `node_id` | string | yes | GUID of the node |
| `comment` | string | yes | Comment text (empty clears) |

### `set_node_enabled`

Enable, disable, or set development-only state on a Blueprint graph node by GUID. Recompiles.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Blueprint content path |
| `graph_name` | string |  | Graph name (default 'EventGraph') |
| `node_id` | string | yes | GUID of the node (from describe_graph) |
| `state` | enum(Enabled\|Disabled\|DevelopmentOnly) | yes | New enabled state |

### `set_pin_default_value`

Set the default (literal) value on an unconnected input pin. Use this to set parameter values like strings, numbers, booleans, vectors, etc. The value is parsed by the UE property system. For Vectors use 'X,Y,Z' format, for Rotators use 'P,Y,R' format.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint |
| `graph_name` | string | yes | Name of the graph |
| `node_id` | string | yes | GUID of the node |
| `pin_name` | string | yes | Name of the pin to set the default value on |
| `default_value` | string | yes | The default value as a string (e.g., 'Hello', '42', 'true', '1.0,2.0,3.0' for Vector) |

### `spawn_blueprint`

Spawn an instance of a Blueprint class in the current level at the specified location.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint to spawn |
| `x` | number |  | X position (default: 0) |
| `y` | number |  | Y position (default: 0) |
| `z` | number |  | Z position (default: 0) |
| `yaw` | number |  | Yaw rotation (default: 0) |
| `label` | string |  | Actor label in scene outliner |

### `validate_blueprint`

Structured, non-compiling Blueprint graph validation. Walks the ubergraph, function, macro and interface graphs (or one graph_name) and reports issues with graph, node GUID, node title and pin: orphan_node (no links at all), exec_in_unconnected (unreachable impure node), exec_out_unconnected (dangling execution output, informational), missing_required_input (object/class reference input with neither link nor default), incompatible_link (existing link the K2 schema no longer accepts), orphaned_pin (pin no longer on the signature), disabled_node (template events are disabled, never orphans) and stale_compile_message (message left by the last compile, not re-evaluated). Returns per-graph and overall revision fingerprints for later patches. Nothing is compiled or modified; use compile_blueprint for compiler diagnostics.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Blueprint to validate |
| `graph_name` | string |  | Validate only this graph (default: all graphs) |
| `limit` | integer |  | Maximum issues to return, 1..2000 (default 500) |

## Build (7 tools)

### `build_lighting`

Trigger a lighting build for the current level. Quality levels: Preview (fast, low quality), Medium, High, Production (slow, best quality). The build runs asynchronously — use get_lighting_build_info to check progress.

*long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `quality` | enum(Preview\|Medium\|High\|Production) |  | Lighting build quality level (default: Preview) |

### `get_build_configuration`

Returns current build configuration: debug/development/shipping, target platform, compiler settings, and key preprocessor defines.

*read-only · idempotent*

_No arguments._

### `get_lighting_build_info`

Check the current lighting build status, quality, and whether the level needs a lighting rebuild. Reports lighting-related warnings.

*read-only · idempotent*

_No arguments._

### `get_map_check_errors`

Runs a map check on the current level and returns errors and warnings. Reports issues such as actors with NULL references, missing meshes, lighting build status, and other common level problems.

*read-only*

_No arguments._

### `get_project_info`

Returns comprehensive project information: project name, engine version, target platforms, build configuration, source modules, content paths, and general project settings.

*read-only · idempotent*

_No arguments._

### `list_project_modules`

Lists all modules in the project (game modules + plugin modules). Shows module name, type, loading phase, and associated plugin if any.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `include_plugins` | boolean |  | Include plugin modules in addition to game modules (default: true) |

### `validate_assets`

Validate assets without modifying them. Scope is either a content path (path + limit, legacy behaviour) or an explicit asset_paths list (1..200). Rules: dependencies (asset-registry dependency existence, no loading), data_validation (loads each asset and runs the engine's UObject::IsDataValid class rules through a validation context), or all (default). Returns per-asset diagnostics {asset_path, class, rule, severity, message}, per-rule counts, loaded_count and unresolved asset paths, plus the legacy issues list. Blueprint compilation and project validators are not run; use compile_blueprint / validate_blueprint for graphs.

*read-only · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to validate (e.g., '/Game/', '/Game/Blueprints/'). Default: '/Game/' when asset_paths is absent |
| `limit` | integer |  | Maximum number of assets to check in path mode (default: 500, max: 5000) |
| `asset_paths` | array<string> |  | Explicit asset object or package paths (1..200); overrides path |
| `rules` | enum(dependencies\|data_validation\|all) |  | dependencies \| data_validation \| all (default) |

## ChangePlans (5 tools)

### `apply_change_plan`

Apply an owned change plan (transform plan or plan_actor_changes plan) only while its targets still match the captured state. Requires expected_hash and a non-empty idempotency_key; retries with the same key return the recorded result without reapplying. For plan_actor_changes plans the result carries an effect journal with before/after read-back per effect; when an operation fails, the earlier effects are rolled back in reverse order and recovery reports verified, partial or failed. Transform plans keep their original behaviour (no automatic recovery).

*idempotent · PIE-off · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `plan_id` | string | yes | Plan identifier |
| `expected_hash` | string | yes | Exact fingerprint returned by planning |
| `idempotency_key` | string | yes | 1..128 character key, unique across this owner's retained plans |

### `get_change_plan`

Inspect an owned change plan (transform plan or plan_actor_changes plan): expiry, current applicability, operations, effect journal and recovery verdict. Plans are local to this editor process and originating principal/session.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `plan_id` | string | yes | Plan identifier |

### `plan_actor_changes`

Plan a typed list of actor changes without editing anything (V5-06). Operations: set_property {actor_path, component?, property, value}, set_transform {actor_path, transform{location,rotation,scale}}, rename {actor_path, label}, create {class, label?, transform}, delete {actor_path}. Planning captures the before-state of every target (for delete: class, label, transform and up to 200 editable properties) and binds the plan to it; apply_change_plan refuses when any target changed. Returns plan_id and expected_hash. Apply with apply_change_plan (Scene scope, expected_hash, idempotency_key); the result carries an effect journal with before/after read-back per effect and a recovery verdict; revert_change_plan applies the recorded inverse. Plans expire after ttl_seconds (default 300, max 600); 32 per session.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `operations` | array<object> | yes | Ordered operations (1..32) |
| `ttl_seconds` | integer |  | Plan lifetime 1..600 (default 300) |

### `plan_actor_transform`

Capture an owner-bound, expiring absolute transform plan without editing the actor. Initial support: exact native movable StaticMeshActor, no physics or attachments/children, in the current editor world. Use an exact actor object path, never a label. Apply requires Scene scope, the returned expected_hash and an idempotency key.

*read-only · PIE-off · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_path` | string | yes | Exact loaded actor object path from list_actors |
| `location` | object | yes | Absolute world location in centimeters |
| `rotation` | object | yes | Absolute rotation in degrees |
| `scale` | object | yes | Absolute positive world scale |
| `ttl_seconds` | integer |  | Plan lifetime: 1..600 seconds, default 300 |

### `revert_change_plan`

Apply the recorded inverse of an applied actor change plan in reverse order and verify each restore (V5-07). Created actors are destroyed; deleted actors are recreated from the captured class, label, transform and editable properties (partial when a property cannot be restored); property, transform and label changes are restored to their captured values. Returns the updated effect journal and a recovery verdict: verified, partial, failed or not_needed. Requires Scene scope; refuses plans that were never applied or already reverted.

*idempotent · PIE-off*

| Argument | Type | Required | Description |
|---|---|---|---|
| `plan_id` | string | yes | Applied plan identifier |

## Chaos (5 tools)

### `chaos_add_field`

Spawn a Chaos Field System actor with a persistent construction field that influences nearby simulating Chaos bodies (e.g. geometry collections) at runtime. 'radial_falloff' applies an external cluster strain inside 'radius' (breaks fractured pieces); 'uniform_vector' applies a constant linear force along +Z. The field acts when physics runs (PIE).

| Argument | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Editor label for the new field actor. |
| `field_type` | enum(radial_falloff\|uniform_vector) | yes | Field type. |
| `magnitude` | number |  | Magnitude / strength (default 1000000 for strain, 10000 for force). |
| `radius` | number |  | Sphere radius for radial_falloff (default 200). |
| `location_x` | number |  | Location X. |
| `location_y` | number |  | Location Y. |
| `location_z` | number |  | Location Z. |

### `chaos_apply_force`

Apply a one-shot impulse to an actor's primary primitive component. The component must have physics simulation enabled. Works in PIE — applies in editor world only as a stress test.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_label` | string | yes | Editor label of the target actor. |
| `force_x` | number | yes | Impulse X (uu * mass). |
| `force_y` | number | yes | Impulse Y. |
| `force_z` | number | yes | Impulse Z. |
| `velocity_change` | boolean |  | If true, treat as velocity change (ignore mass). |

### `chaos_create_cloth_asset`

Create a Chaos Cloth Asset (UChaosClothAsset) via the engine's scripted factory helper. Validates the source skeletal mesh and records it on the new asset. NOTE: the simulation mesh is populated through the cloth Dataflow graph (a 'Skeletal Mesh Import' node referencing the source); open the asset's Dataflow to finish authoring. Creates a real asset (was a stub in v4).

*long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Destination /Game path for the cloth asset. |
| `source_skeletal_mesh` | string | yes | Skeletal mesh asset to drive cloth simulation. |

### `chaos_create_geometry_collection`

Create a UGeometryCollection asset from a list of source static meshes. Wraps GeometryCollectionEngine.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Destination /Game path for the new collection. |
| `source_meshes` | array<string> | yes | Static mesh asset paths to embed. |

### `chaos_fracture`

Fracture a geometry collection using one of the standard methods. Wraps FractureEngine.

*destructive · long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `collection_path` | string | yes | Geometry collection asset path. |
| `method` | enum(uniform\|cluster\|voronoi\|planar) | yes | Fracture method. |
| `num_pieces` | integer |  | Target number of pieces (when applicable). |
| `seed` | integer |  | Random seed for reproducibility. |

## CommonUI (5 tools)

### `configure_common_button`

Configure a CommonButtonBase widget with input actions, styles, and interaction behavior. CommonButtons support gamepad/keyboard navigation, input action bindings, and selectable (toggle) mode. Uses Python bridge for property access.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the Widget Blueprint containing the button |
| `button_name` | string | yes | Name of the CommonButtonBase widget in the WBP |
| `triggering_input_action` | string |  | Input action data asset path that triggers this button (e.g., '/Game/Input/IA_Confirm') |
| `style_path` | string |  | Path to a CommonButtonStyle data asset |
| `is_selectable` | boolean |  | Whether the button can be persistently selected/toggled |
| `is_interactable_when_selected` | boolean |  | Whether button remains interactable when selected |
| `hide_input_action` | boolean |  | Whether to hide the input action widget |

### `create_common_ui_style`

Create a CommonUI style asset: a Blueprint deriving from CommonButtonStyle, CommonTextStyle or CommonBorderStyle (what configure_common_button's style_path and CommonTextBlock's Style expect). Set its properties afterwards with set_blueprint_default-style tools or execute_python on the CDO.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new style asset (e.g., '/Game/UI/Styles/BS_Primary') |
| `style_type` | enum(CommonButtonStyle\|CommonTextStyle\|CommonBorderStyle) | yes | Style base class |

### `create_common_ui_widget`

Create a Widget Blueprint deriving from a CommonUI base class (CommonActivatableWidget = screens/panels with input config and back handling, CommonUserWidget = plain CommonUI widget, CommonButtonBase = button with input actions and styles, CommonActivatableWidgetStack / CommonActivatableWidgetQueue = containers, or any other UUserWidget subclass from CommonUI or your game module). Requires the CommonUI plugin to be enabled.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new widget (e.g., '/Game/UI/WBP_MainMenuScreen') |
| `widget_type` | string | yes | CommonUI base class name (CommonActivatableWidget, CommonUserWidget, CommonButtonBase, ...) or a '/Script/Module.Class' path |
| `root_widget_type` | enum(CanvasPanel\|VerticalBox\|HorizontalBox\|Overlay\|SizeBox\|Border) |  | Root panel widget type (default: CanvasPanel) |

### `list_common_ui_widgets`

List Widget Blueprints that use CommonUI base classes (CommonActivatableWidget, CommonButtonBase, etc.).

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (default: '/Game/') |
| `name_filter` | string |  | Filter by name |
| `limit` | integer |  | Maximum results (default: 50) |

### `set_common_ui_input_mode`

Configure CommonUI input routing mode. Controls whether the UI responds to mouse, gamepad, or both input methods. Uses Python bridge. 'Mouse' = mouse/keyboard only, 'Gamepad' = gamepad/keyboard only, 'All' = all input methods active.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `input_mode` | enum(Mouse\|Gamepad\|All) | yes | Input mode for CommonUI |
| `mouse_capture_mode` | boolean |  | Whether to capture mouse (default: false) |

## ControlRig (2 tools)

### `create_control_rig`

Create a Control Rig Blueprint for a skeleton. Control Rigs drive a skeleton procedurally — IK chains, bone constraints, and the animator-facing controls used in Sequencer. The rig is created with the skeleton's preview mesh assigned; its actual rig graph is authored in the Control Rig editor, which has no scripting surface for node authoring. Requires the ControlRig plugin and the Python bridge. To bake Control Rig work in a level sequence down to a reusable animation, use bake_sequence_to_anim_sequence.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new Control Rig Blueprint (e.g., '/Game/Characters/CR_Hero') |
| `skeleton_path` | string | yes | Content path to the USkeleton asset (e.g., '/Game/Characters/SK_Hero_Skeleton') |

### `get_control_rig_info`

Read a Control Rig Blueprint's structure: preview mesh, and the rig hierarchy broken down into bones, controls and nulls with their names. Controls are the animator-facing handles a Sequencer Control Rig track keys, so this is how you discover what a rig actually exposes before animating it. Requires the ControlRig plugin and the Python bridge.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the Control Rig Blueprint |
| `limit` | integer |  | Maximum hierarchy elements to return per category (default: 100) |

## Data (6 tools)

### `add_datatable_row`

Add a new row to a DataTable. Provide column values as a JSON string with property names matching the row struct.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the DataTable |
| `row_name` | string | yes | Name for the new row |
| `row_json` | string | yes | JSON object with column name/value pairs |

### `create_user_struct`

Create a new UserDefinedStruct (Data Structure) asset. Optionally pre-populate with typed fields. The struct can be used in Blueprints, DataTables, and variables. Supported field types: Boolean, Integer, Float, String, Name, Text, Vector, Rotator, Transform, LinearColor, Object, SoftObject.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new struct (e.g., '/Game/Data/S_WeaponData') |
| `fields_json` | string |  | Optional JSON array of field definitions: [{"name":"Damage","type":"Float"},{"name":"Weapon","type":"Object","class":"/Script/Engine.Actor"}]. Supported types: Boolean, Integer, Float, String, Name, Text, Vector, Rotator, Transform, LinearColor, Object, SoftObject. For Object/SoftObject, optional 'class' specifies the class path (default: /Script/CoreUObject.Object). |

### `get_datatable_rows`

Read rows from a DataTable. Returns row names and all column values as JSON.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the DataTable |
| `row_filter` | string |  | Filter rows by name (substring match) |
| `limit` | integer |  | Maximum rows to return (default: 50) |

### `get_struct_info`

Get detailed info about a UserDefinedStruct or any UScriptStruct: field names, types, and default values. Works for both Blueprint-created structs and engine structs.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the struct asset |

### `list_datatables`

List all DataTable assets in the project. Returns asset paths, row struct type, and row count.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (default: '/Game/') |
| `name_filter` | string |  | Filter by name (substring match) |
| `limit` | integer |  | Maximum results (default: 100) |

### `list_user_structs`

List all UserDefinedStruct assets in the project. Returns name, path, and field count for each struct.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (default: '/Game/') |
| `name_filter` | string |  | Filter by name (substring match) |
| `limit` | integer |  | Maximum results (default: 100) |

## Debug (7 tools)

### `add_watch`

Add a pin watch on a Blueprint pin. pin_id is the FGuid of the pin (preferred) or its name. Watches are surfaced in the Blueprint debugger when running PIE.

| Argument | Type | Required | Description |
|---|---|---|---|
| `blueprint` | string | yes | Blueprint asset path. |
| `pin_id` | string | yes | Pin GUID or pin name. |

### `clear_blueprint_breakpoint`

Remove the breakpoint from a Blueprint node, if any.

| Argument | Type | Required | Description |
|---|---|---|---|
| `blueprint` | string | yes | Blueprint asset path. |
| `node_id` | string | yes | Node GUID or name. |

### `get_call_stack`

Return the Blueprint call stack at the most recent breakpoint hit (only available while paused). When no breakpoint has been hit, returns paused=false.

*read-only*

_No arguments._

### `get_last_runtime_error`

Return the most recent Blueprint runtime error captured since the editor session started. Hooks FBlueprintCoreDelegates::OnScriptException; returns have=false if none.

*read-only · idempotent*

_No arguments._

### `get_watches`

List currently watched pins for one Blueprint, or — when blueprint is omitted — across all loaded Blueprints. Returns {blueprint, pin_id, pin_name, node_title}.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `blueprint` | string |  | Optional Blueprint asset path. |

### `list_breakpoints`

List all breakpoints on a single blueprint, or — if blueprint is omitted — across every loaded Blueprint. Returns {blueprint, node_guid, node_title, enabled} per breakpoint.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `blueprint` | string |  | Optional Blueprint asset path. If omitted, scans all loaded Blueprints. |

### `set_blueprint_breakpoint`

Place a breakpoint on a Blueprint node. node_id may be the node's NodeGuid (preferred) or its object name. Idempotent: if a breakpoint already exists on the node, it is enabled.

| Argument | Type | Required | Description |
|---|---|---|---|
| `blueprint` | string | yes | Blueprint asset path (e.g., '/Game/BP_Foo'). |
| `node_id` | string | yes | Node GUID or object name. |
| `enabled` | boolean |  | Whether the breakpoint should be enabled (default true). |

## Diagnostics (4 tools)

### `export_diagnostic_bundle`

Write a redacted diagnostic bundle for support: server health (diagnostic detail), capabilities, this session's operations, tool timing telemetry and a bounded tail of the editor log, all with API keys, auth tokens and Authorization headers redacted. Files land under Saved/MCPV5/Bundles/<bundle_id>/ (bundle.json, log_tail.txt); the manifest lists each file with size and SHA-1 and is readable as unreal://bundles/{bundle_id} by the owner. Bounded: log_tail_kb 0..2048 (default 128); at most 16 bundles are retained per editor session.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `log_tail_kb` | integer |  | Kilobytes of editor log tail to include, 0..2048 (default 128) |
| `include_operations` | boolean |  | Include this session's operations (default true) |

### `get_server_capabilities`

Report which optional providers and features are actually available in this editor process, with reasons when they are not: visual capture backend, Python, Epic's ModelContextProtocol toolsets, and the optional plugins the tool families depend on (GameplayAbilities, Metasound, MetaHuman, PCG, Niagara, EnhancedInput, GeometryScripting, Fracture, ChaosClothAsset). Also reports tool exposure mode and enabled categories. Nothing is loaded or enabled by this call.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `feature` | string |  | Optional single feature/plugin name to report |

### `get_server_health`

Bounded, redacted server health: listener state and port, ready/busy/degraded classification, uptime, tool/resource/prompt counts, PIE state, active editor transaction, shader compilation, and owned-resource counts for observers, plans and verification records. No secrets, no payloads.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `detail` | enum(basic\|diagnostic) |  | basic (default) or diagnostic |

### `list_worlds`

List the worlds this editor process currently holds with stable ids (object paths), role (editor, pie, game, editor_preview...), map name, PIE instance, actor count and whether each is the current editor world. Use the editor world path for plan_actor_transform and the PIE world for play-time inspection; tools never cross worlds implicitly.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `world_type` | enum(any\|editor\|pie\|game\|editor_preview) |  | Filter: any (default), editor, pie, game, editor_preview |

## Editor (7 tools)

### `focus_viewport`

Focus the editor viewport camera on a specific actor or world position. Provide either actor_name or x/y/z coordinates.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string |  | Focus on this actor by label |
| `x` | number |  | Focus on this X position |
| `y` | number |  | Focus on this Y position |
| `z` | number |  | Focus on this Z position |
| `distance` | number |  | Camera distance from target (default: 500) |

### `get_selection`

Get the list of currently selected actors in the editor.

*read-only · idempotent*

_No arguments._

### `redo`

Redo the last undone operation(s). Equivalent to Ctrl+Y.

| Argument | Type | Required | Description |
|---|---|---|---|
| `count` | integer |  | Number of redo steps (default: 1) |

### `run_console_command`

Execute an Unreal Engine console command. Useful for toggling debug visualizations, changing rendering settings, and more.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `command` | string | yes | Console command to execute (e.g., 'stat fps', 'show collision') |

### `set_viewport_camera`

Set the editor viewport camera position and rotation directly.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `x` | number | yes | Camera X position |
| `y` | number | yes | Camera Y position |
| `z` | number | yes | Camera Z position |
| `pitch` | number |  | Camera pitch (default: 0) |
| `yaw` | number |  | Camera yaw (default: 0) |
| `roll` | number |  | Camera roll (default: 0) |

### `take_screenshot`

Capture the current editor viewport as an image. Returns a base64-encoded JPEG. Useful for visual verification of scene state.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `width` | integer |  | Image width in pixels (default: 1280) |
| `height` | integer |  | Image height in pixels (default: 720) |
| `quality` | integer |  | JPEG quality 1-100 (default: 70) |

### `undo`

Undo the last editor operation(s). Equivalent to Ctrl+Z.

| Argument | Type | Required | Description |
|---|---|---|---|
| `count` | integer |  | Number of undo steps (default: 1) |

## EngineAPI (3 tools)

### `get_engine_header`

Read the contents of a specific Unreal Engine header file. Use search_engine_class or search_engine_api first to find the file path. Supports reading specific line ranges for large files.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `header_path` | string | yes | Path to the header file. Can be relative to Engine/Source (e.g., 'Runtime/Engine/Classes/Engine/StaticMeshActor.h') or an absolute path. |
| `start_line` | integer |  | Start reading from this line number (1-based). Default: 1. |
| `end_line` | integer |  | Stop reading at this line number (inclusive). Default: end of file. |

### `search_engine_api`

Grep-like search through installed Unreal Engine headers. Finds methods, properties, macros, and any text patterns in the engine source. Useful for discovering API signatures, checking method existence, and finding usage patterns.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `pattern` | string | yes | Search pattern (substring or regex) to find in engine headers. E.g., 'UpdateParameter', 'void\s+SetMesh'. |
| `path_filter` | string |  | Only search headers whose path contains this substring. E.g., 'Animation', 'Materials', 'Runtime/Engine'. |
| `file_filter` | string |  | Only search headers whose filename contains this substring. E.g., 'BlendSpace', 'Actor'. |
| `max_results` | integer |  | Maximum number of matches to return. Default: 20. |
| `context_lines` | integer |  | Number of lines of context to include before and after each match. Default: 2. |
| `include_private` | boolean |  | If true, also search Private/ headers. Default: false. |

### `search_engine_class`

Search installed Unreal Engine headers for a class definition and extract its declaration with members. Searches the actual installed engine source, providing accurate API info for the current engine version. Returns file path, line number, and class body.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `class_name` | string | yes | UE class name to search for (e.g., 'UBlendSpace', 'AActor', 'FVector'). Prefix is optional. |
| `include_private` | boolean |  | If true, also search Private/ headers. Default: false (Public/Classes only). |

## EnhancedInput (6 tools)

### `add_action_mapping`

Add a key mapping to a UInputMappingContext, binding an InputAction to a specific key.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `mapping_context_path` | string | yes | Content path of the InputMappingContext asset |
| `action_path` | string | yes | Content path of the InputAction asset to bind |
| `key` | string | yes | Key name (e.g., 'W', 'SpaceBar', 'LeftMouseButton', 'Gamepad_LeftStick_X') |

### `create_input_action`

Create a UInputAction asset with a specified value type (Bool, Axis1D, Axis2D, Axis3D).

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new InputAction (e.g., '/Game/Input/IA_Jump') |
| `value_type` | enum(Bool\|Axis1D\|Axis2D\|Axis3D) | yes | The value type for this action |
| `description` | string |  | Optional description for the input action |

### `create_input_mapping_context`

Create a UInputMappingContext asset for binding input actions to keys.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new InputMappingContext (e.g., '/Game/Input/IMC_Default') |
| `description` | string |  | Optional description for the mapping context |

### `get_input_mapping_info`

Get all key bindings in a UInputMappingContext. Returns action names, keys, modifiers, and triggers for each mapping.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `mapping_context_path` | string | yes | Content path of the InputMappingContext asset |

### `list_input_actions`

List all UInputAction assets in the project. Returns asset names, paths, and value types.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (default: '/Game/') |
| `name_filter` | string |  | Filter by asset name (substring match) |

### `list_input_mapping_contexts`

List all UInputMappingContext assets in the project. Returns asset names and paths.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (default: '/Game/') |
| `name_filter` | string |  | Filter by asset name (substring match) |

## Environment (4 tools)

### `set_fog_settings`

Configure ExponentialHeightFog density, color, falloff, and volumetric fog settings.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the ExponentialHeightFog actor |
| `fog_density` | number |  | Fog density (0-1, default: 0.02) |
| `fog_height_falloff` | number |  | Height falloff (0-2, default: 0.2) |
| `fog_max_opacity` | number |  | Maximum opacity (0-1, default: 1) |
| `start_distance` | number |  | Start distance in cm (default: 0) |
| `color_r` | number |  | Inscattering color red (0-1) |
| `color_g` | number |  | Inscattering color green (0-1) |
| `color_b` | number |  | Inscattering color blue (0-1) |
| `volumetric_fog` | boolean |  | Enable volumetric fog |

### `set_light_properties`

Configure light properties: intensity, color, temperature, shadows, attenuation. Works on PointLight, SpotLight, DirectionalLight, and SkyLight actors.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the light actor |
| `intensity` | number |  | Light intensity (in candelas for point/spot, lux for directional) |
| `color_r` | number |  | Light color red (0-1) |
| `color_g` | number |  | Light color green (0-1) |
| `color_b` | number |  | Light color blue (0-1) |
| `temperature` | number |  | Color temperature in Kelvin (1000-15000) |
| `use_temperature` | boolean |  | Use color temperature instead of color |
| `attenuation_radius` | number |  | Attenuation radius in cm (point/spot lights) |
| `source_radius` | number |  | Source radius for soft shadows (cm) |
| `cast_shadows` | boolean |  | Enable shadow casting |
| `inner_cone_angle` | number |  | Spot light inner cone angle (degrees) |
| `outer_cone_angle` | number |  | Spot light outer cone angle (degrees) |
| `indirect_lighting_intensity` | number |  | Indirect lighting intensity multiplier |

### `set_post_process_settings`

Set bloom, exposure, color grading, AO, and other post-process settings on a PostProcessVolume. Only provided parameters are changed.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the PostProcessVolume actor |
| `bloom_intensity` | number |  | Bloom intensity (0-8, default: 0.675) |
| `bloom_threshold` | number |  | Bloom threshold (-1 to 8) |
| `exposure_compensation` | number |  | Exposure compensation EV (-15 to 15) |
| `exposure_min_brightness` | number |  | Auto exposure min brightness |
| `exposure_max_brightness` | number |  | Auto exposure max brightness |
| `vignette_intensity` | number |  | Vignette intensity (0-1) |
| `grain_intensity` | number |  | Film grain intensity (0-1) |
| `ao_intensity` | number |  | Ambient occlusion intensity (0-1) |
| `ao_radius` | number |  | Ambient occlusion radius in cm |
| `infinite_extent` | boolean |  | Make this an unbound (infinite extent) volume |

### `set_sky_atmosphere`

Configure SkyAtmosphere scattering, absorption, and height parameters for realistic sky rendering.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor with SkyAtmosphereComponent |
| `rayleigh_scattering_r` | number |  | Rayleigh scattering red (default: 0.0058) |
| `rayleigh_scattering_g` | number |  | Rayleigh scattering green (default: 0.01355) |
| `rayleigh_scattering_b` | number |  | Rayleigh scattering blue (default: 0.0331) |
| `rayleigh_exponential_distribution` | number |  | Rayleigh exponential distribution (default: 8) |
| `mie_scattering_scale` | number |  | Mie scattering scale (default: 0.003996) |
| `mie_absorption_scale` | number |  | Mie absorption scale (default: 0.000444) |
| `mie_anisotropy` | number |  | Mie anisotropy (0-0.999, default: 0.8) |
| `atmosphere_height` | number |  | Atmosphere height in km (default: 60) |

## EpicToolsets (3 tools)

### `epic_call_tool`

[Epic MCP toolset] Call a tool by name. Provide toolset_name to call a toolset tool, or omit it to call a top-level MCP tool. Use list_toolsets and describe_toolset to discover available tools and their input schemas.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `toolset_name` | string |  | Optional. Name of the toolset containing the tool. Omit to call a top-level MCP tool. Use list_toolsets to discover toolset names. |
| `tool_name` | string | yes | Name of the tool to call, without toolset prefix. Use describe_toolset to discover tool names and input schemas. |
| `arguments` | object |  | Arguments to pass to the tool. Must match the tool's input schema. Defaults to an empty object. |

### `epic_describe_toolset`

[Epic MCP toolset] Get detailed information about a toolset including all tool names, descriptions, and input schemas.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `toolset_name` | string | yes | Name of the toolset to describe. Use list_toolsets to see available names. |

### `epic_list_toolsets`

[Epic MCP toolset] List all available toolsets with names and descriptions.

*destructive*

_No arguments._

## Foliage (4 tools)

### `add_foliage_type`

Register a static mesh as a foliage type on the level's InstancedFoliageActor. If a foliage type for this mesh already exists it is returned unchanged. Use paint_foliage afterwards to scatter instances.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `mesh_path` | string | yes | Content path to the StaticMesh asset (e.g., '/Game/Foliage/SM_Tree') |

### `erase_foliage`

Remove foliage instances within a spherical radius around a world-space center. If mesh_path is provided only instances of that foliage type are erased; otherwise all foliage types are affected.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `x` | number | yes | Center X position in world space |
| `y` | number | yes | Center Y position in world space |
| `z` | number | yes | Center Z position in world space |
| `radius` | number | yes | Radius in cm within which instances are removed |
| `mesh_path` | string |  | Optional: limit erasure to foliage using this mesh. Leave empty to erase all types. |

### `get_foliage_stats`

Return a summary of all foliage types registered in the current level, including the static mesh name and total instance count for each type. Returns an empty list when no InstancedFoliageActor exists in the level.

*read-only · idempotent*

_No arguments._

### `paint_foliage`

Scatter foliage instances randomly within a radius around a world-space center. The foliage type is found or created automatically from the given mesh. A downward line trace finds the ground surface so instances sit correctly on terrain or geometry.

| Argument | Type | Required | Description |
|---|---|---|---|
| `mesh_path` | string | yes | Content path to the StaticMesh used as the foliage type |
| `x` | number | yes | Center X position in world space |
| `y` | number | yes | Center Y position in world space |
| `z` | number | yes | Center Z position (used as trace start height; ground is found via line trace) |
| `radius` | number |  | Scatter radius in cm around the center point (default: 500) |
| `count` | integer |  | Number of instances to add (default: 10) |
| `random_scale_min` | number |  | Minimum uniform random scale applied to each instance (default: 0.8) |
| `random_scale_max` | number |  | Maximum uniform random scale applied to each instance (default: 1.2) |

## GAS (8 tools)

### `add_ability_component`

Add an AbilitySystemComponent to a Blueprint's component hierarchy (SCS). The component class is resolved dynamically by name from the GameplayAbilities module. Requires the GameplayAbilities plugin to be enabled.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the target Blueprint to add the AbilitySystemComponent to (e.g., '/Game/Blueprints/BP_MyCharacter') |

### `create_attribute_set`

Create a Blueprint inheriting from AttributeSet. Optionally pre-populate it with named Float variables representing gameplay attributes. Requires the GameplayAbilities plugin to be enabled.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new AttributeSet Blueprint (e.g., '/Game/Attributes/AS_CharacterStats') |
| `attributes` | array<string> |  | Array of attribute names to create as Float variables (e.g., ["Health", "Mana", "Stamina"]) |

### `create_gameplay_ability`

Create a Blueprint inheriting from GameplayAbility (or a custom subclass). Requires the GameplayAbilities plugin to be enabled. The parent class is resolved dynamically by name.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new ability Blueprint (e.g., '/Game/Abilities/GA_FireBlast') |
| `ability_name` | string |  | Display name for the ability (set as the Blueprint's asset name if different from path) |
| `parent_class` | string |  | Parent class name (default: 'GameplayAbility'). Can be a custom ability base class. |

### `create_gameplay_effect`

Create a Blueprint inheriting from GameplayEffect. Optionally set the duration policy (Instant, Infinite, or HasDuration). Requires the GameplayAbilities plugin to be enabled.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new effect Blueprint (e.g., '/Game/Effects/GE_DamageOverTime') |
| `duration_policy` | enum(Instant\|Infinite\|HasDuration) |  | Duration policy for the effect |

### `get_gas_info`

Get Gameplay Ability System setup information for an actor in the current level. Reports whether the actor has an AbilitySystemComponent, lists granted abilities, and active gameplay effects. Requires the GameplayAbilities plugin to be enabled.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor in the current level to inspect for GAS setup |

### `list_attribute_sets`

List all Blueprint assets that inherit from AttributeSet. Searches the Asset Registry under the specified path. Requires the GameplayAbilities plugin to be enabled.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (e.g., '/Game/'). Default: '/Game/' |
| `name_filter` | string |  | Filter by asset name (substring match, case-insensitive) |

### `list_gameplay_abilities`

List all Blueprint assets that inherit from GameplayAbility. Searches the Asset Registry under the specified path. Requires the GameplayAbilities plugin to be enabled.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (e.g., '/Game/'). Default: '/Game/' |
| `name_filter` | string |  | Filter by asset name (substring match, case-insensitive) |

### `list_gameplay_effects`

List all Blueprint assets that inherit from GameplayEffect. Searches the Asset Registry under the specified path. Requires the GameplayAbilities plugin to be enabled.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (e.g., '/Game/'). Default: '/Game/' |
| `name_filter` | string |  | Filter by asset name (substring match, case-insensitive) |

## GameFramework (6 tools)

### `create_game_mode`

Create a new GameModeBase Blueprint. Optionally set the default pawn class. The GameMode controls match rules, spawning, and game flow.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new GameMode Blueprint (e.g., '/Game/Blueprints/BP_MyGameMode') |
| `display_name` | string |  | Optional display name for the Blueprint |
| `default_pawn_class` | string |  | Content path to the default pawn class Blueprint (e.g., '/Game/Blueprints/BP_MyPawn') |

### `create_game_state`

Create a new GameStateBase Blueprint. The GameState holds replicated game-wide state such as scores, match timers, and team info.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new GameState Blueprint (e.g., '/Game/Blueprints/BP_MyGameState') |

### `create_hud`

Create a new HUD Blueprint. The HUD class handles drawing canvas-based UI elements and managing the player's heads-up display.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new HUD Blueprint (e.g., '/Game/Blueprints/BP_MyHUD') |

### `create_player_controller`

Create a new PlayerController Blueprint. The PlayerController handles player input, camera management, and HUD interaction.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new PlayerController Blueprint (e.g., '/Game/Blueprints/BP_MyPC') |

### `create_player_state`

Create a new PlayerState Blueprint. The PlayerState holds replicated per-player data such as player name, score, and team assignment.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new PlayerState Blueprint (e.g., '/Game/Blueprints/BP_MyPlayerState') |

### `get_game_framework_info`

Get the current world's game framework configuration. Returns the GameMode class and its configured default pawn, player controller, player state, HUD, and game state classes.

*read-only*

_No arguments._

## GameplayTags (3 tools)

### `add_gameplay_tags`

Register new Gameplay Tags in the project's tag hierarchy. Tags use dot-separated paths (e.g., 'Character.State.Stunned', 'Weapon.Type.Rifle'). Parent tags are automatically created. Tags persist in the project's DefaultGameplayTags.ini.

| Argument | Type | Required | Description |
|---|---|---|---|
| `tags` | array<string> | yes | Array of tag strings to register (dot-separated hierarchy, e.g., 'Character.State.Stunned', 'Ability.Cooldown') |
| `comment` | string |  | Optional comment describing these tags |

### `list_gameplay_tags`

List all registered Gameplay Tags in the project. Filter by substring or parent tag to narrow results. Returns tag names in hierarchical dot-notation.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `filter` | string |  | Filter tags by substring (e.g., 'Character' shows all Character.* tags) |
| `parent_tag` | string |  | Only show children of this parent tag |
| `limit` | integer |  | Maximum tags to return (default: 100) |

### `set_actor_gameplay_tags`

Assign Gameplay Tags to an actor via its Tags property. Tags must already be registered (use add_gameplay_tags first). Modes: 'add' appends, 'remove' removes specific tags, 'replace' overwrites all tags.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor |
| `tags` | array<string> | yes | Array of gameplay tag strings to assign |
| `mode` | enum(add\|remove\|replace) |  | How to apply tags (default: add) |

## Gizmo (3 tools)

### `gizmo_get_state`

Report the current transform gizmo mode and coordinate space.

*read-only · idempotent*

_No arguments._

### `gizmo_set_coordinate_system`

Set the transform gizmo coordinate space (world / local / parent).

| Argument | Type | Required | Description |
|---|---|---|---|
| `space` | enum(world\|local\|parent) | yes | Coordinate space. |

### `gizmo_set_mode`

Set the level-editor transform gizmo mode (translate / rotate / scale). Affects the active viewport widget used for manual-feel operations and screenshots.

| Argument | Type | Required | Description |
|---|---|---|---|
| `mode` | enum(translate\|rotate\|scale\|translate_rotate_z\|2d) | yes | Gizmo mode. |

## Iris (1 tools)

### `iris_get_status`

Report the Iris replication system configuration: whether the IrisCore module is loaded and the values of key net.Iris.* console variables. Note: whether a net driver actually uses Iris is decided at connection time from project settings / command line (-UseIrisReplication).

*read-only · idempotent*

_No arguments._

## Landscape (3 tools)

### `create_landscape`

Create a new flat landscape actor in the current level using UE's standard import pipeline. The landscape is initialised with a flat (mid-grey) heightmap. Parameters control the component grid, subsection size, and world-space scale. Typical presets:   Small  (1 km^2):  8x8  components, 1 section, 63 quads, scale 100   Medium (4 km^2): 16x16 components, 1 section, 63 quads, scale 100   Large  (8 km^2): 16x16 components, 2 sections, 127 quads, scale 100 After creation use get_landscape_info to confirm the result.

| Argument | Type | Required | Description |
|---|---|---|---|
| `x` | number |  | World X position of the landscape origin (default: 0) |
| `y` | number |  | World Y position of the landscape origin (default: 0) |
| `z` | number |  | World Z position of the landscape origin (default: 0) |
| `num_components_x` | integer |  | Number of landscape components along X axis (default: 8). Total quad width = num_components_x * sections_per_component * quads_per_section. |
| `num_components_y` | integer |  | Number of landscape components along Y axis (default: 8). Total quad height = num_components_y * sections_per_component * quads_per_section. |
| `quads_per_section` | enum(7\|15\|31\|63\|127\|255) |  | Quads per subsection. Must be one of the standard UE values: 7, 15, 31, 63, 127, 255. Higher values = higher per-component resolution. Default: 63. |
| `sections_per_component` | enum(1\|2) |  | Number of subsections per component (1 or 2). Default: 1. Using 2 doubles the component's quad count and LOD flexibility. |
| `scale_x` | number |  | Landscape scale on X axis in cm per quad (default: 100). At scale 100 each quad = 1m in world space. |
| `scale_y` | number |  | Landscape scale on Y axis in cm per quad (default: 100). |
| `scale_z` | number |  | Landscape Z scale controlling height range (default: 100). Actual height range in cm = +/- 256 * scale_z. |
| `label` | string |  | Actor label shown in the World Outliner (default: 'Landscape'). |

### `get_landscape_info`

Get detailed information about landscape actors in the current level. Reports name, class, component counts, quad/vertex resolution, world bounds, scale, material, paint layers, and edit layers for each ALandscape and ALandscapeStreamingProxy. Useful for understanding existing terrain before making modifications.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string |  | Optional: filter to a specific landscape actor by label. If empty, all landscapes are returned. |

### `set_landscape_material`

Assign a material to a landscape actor's LandscapeMaterial slot. The material should be a landscape-compatible material with layer blend nodes for paint layers.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the landscape actor |
| `material_path` | string | yes | Content path of the material to assign (e.g., '/Game/Materials/M_Landscape') |

## Level (6 tools)

### `get_level_info`

Get information about the currently loaded level: name, path, actor count, world settings, and streaming levels.

*read-only · idempotent*

_No arguments._

### `get_world_settings`

Read current world settings: gravity, kill-Z height, default game mode, navigation system class, and world bounds.

*read-only · idempotent*

_No arguments._

### `new_level`

Create and open a new empty level at the specified content path.

| Argument | Type | Required | Description |
|---|---|---|---|
| `level_path` | string | yes | Content path for the new level (e.g., '/Game/Maps/NewLevel') |

### `open_level`

Open an existing level in the editor.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `level_path` | string | yes | Content path of the level to open (e.g., '/Game/Maps/MyLevel') |
| `save_current` | boolean |  | Save the current level before opening (default: true) |

### `save_level`

Save the current level and optionally all dirty packages.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `save_all` | boolean |  | Save all dirty packages, not just the level (default: false) |

### `set_world_settings`

Modify world settings for the current level. Set gravity, kill-Z height, default game mode, and world bounds checks. Only provided fields are changed.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `gravity_z` | number |  | World gravity on Z axis (default: -980). Set to -490 for moon gravity, 0 for zero-G. |
| `kill_z` | number |  | Kill Z height - actors below this Z are destroyed (default: -10000) |
| `game_mode_class` | string |  | Content path to Game Mode Blueprint to set as default (e.g., '/Game/BP_GameMode') |
| `enable_world_bounds_checks` | boolean |  | Enable world bounds checking |

## Lighting (3 tools)

### `lighting_get_settings`

Read the current values of the key global-illumination / lighting console variables (MegaLights, Lumen, shadows) so an agent can verify the active lighting configuration.

*read-only · idempotent*

_No arguments._

### `lighting_set_lumen`

Enable/disable Lumen dynamic global illumination (diffuse indirect) at runtime via r.Lumen.DiffuseIndirect.Allow, and optionally tune the screen-probe gather quality. Use for quick lighting iteration; the project's Dynamic GI Method is the persistent setting.

| Argument | Type | Required | Description |
|---|---|---|---|
| `diffuse_indirect` | boolean | yes | Allow Lumen diffuse GI (true) or disable (false). |
| `screen_probe_gather` | integer |  | Optional: r.Lumen.ScreenProbeGather (0/1) quality toggle. |

### `lighting_set_megalights`

Toggle MegaLights (production-ready in UE 5.8): GPU-efficient rendering of many dynamic, shadow-casting lights. Sets the r.MegaLights.EnableForProject console variable. Note: MegaLights also requires the project/scene to allow it (r.MegaLights.Allowed) and a compatible shadowing setup.

| Argument | Type | Required | Description |
|---|---|---|---|
| `enabled` | boolean | yes | True to enable MegaLights, false to disable. |

## Macro (6 tools)

### `create_basic_level`

Creates a complete basic level with all essentials: floor plane, DirectionalLight, SkyLight, SkyAtmosphere, PostProcessVolume (infinite extent), ExponentialHeightFog, PlayerStart, and NavMeshBoundsVolume. Configures lighting based on preset (Day/Night/Sunset/Indoor).

| Argument | Type | Required | Description |
|---|---|---|---|
| `floor_size` | number |  | Size of the floor plane in cm (default: 5000). The floor is scaled uniformly from the 100x100 engine Plane mesh. |
| `add_player_start` | boolean |  | Add a PlayerStart actor (default: true) |
| `add_nav_mesh` | boolean |  | Add a NavMeshBoundsVolume covering the floor (default: true) |
| `lighting_preset` | enum(Day\|Night\|Sunset\|Indoor) |  | Lighting preset |

### `create_grid_layout`

Creates a grid of StaticMeshActors arranged in rows and columns. All actors are placed in a 'Layout/Grid' folder.

| Argument | Type | Required | Description |
|---|---|---|---|
| `mesh_path` | string | yes | Content path of the static mesh asset to use for each grid cell |
| `rows` | integer | yes | Number of rows in the grid |
| `columns` | integer | yes | Number of columns in the grid |
| `spacing_x` | number |  | Spacing between columns along X axis in cm (default: 200) |
| `spacing_y` | number |  | Spacing between rows along Y axis in cm (default: 200) |
| `start_x` | number |  | Grid origin X position (default: 0) |
| `start_y` | number |  | Grid origin Y position (default: 0) |
| `start_z` | number |  | Grid origin Z position (default: 0) |
| `material_path` | string |  | Optional material to assign to all mesh actors |

### `create_light_rig`

Creates a 3-point lighting setup (key, fill, rim) around a center position. Key light is front-left, fill light is front-right (softer), rim/back light is behind the subject. All lights placed in 'Lighting/LightRig' folder.

| Argument | Type | Required | Description |
|---|---|---|---|
| `center_x` | number |  | Center X position of the rig (default: 0) |
| `center_y` | number |  | Center Y position of the rig (default: 0) |
| `center_z` | number |  | Center Z position of the rig (default: 0) |
| `radius` | number |  | Distance of lights from center (default: 500) |
| `key_intensity` | number |  | Key light intensity in candelas (default: 10) |
| `fill_intensity` | number |  | Fill light intensity in candelas (default: 3) |
| `rim_intensity` | number |  | Rim/back light intensity in candelas (default: 5) |

### `create_ring_layout`

Creates a circular arrangement of StaticMeshActors evenly distributed around a ring. Optionally rotates each actor to face the center. All actors placed in 'Layout/Ring' folder.

| Argument | Type | Required | Description |
|---|---|---|---|
| `mesh_path` | string | yes | Content path of the static mesh asset |
| `count` | integer | yes | Number of actors to distribute around the ring |
| `radius` | number | yes | Radius of the ring in cm |
| `center_x` | number |  | Center X position (default: 0) |
| `center_y` | number |  | Center Y position (default: 0) |
| `center_z` | number |  | Center Z position (default: 0) |
| `face_center` | boolean |  | Rotate actors to face the center of the ring (default: true) |
| `material_path` | string |  | Optional material to assign to all mesh actors |

### `create_staircase`

Creates a staircase from N copies of a mesh arranged in ascending steps. Each step is offset by step_height vertically and step_depth horizontally along the yaw direction. All actors placed in 'Layout/Staircase' folder.

| Argument | Type | Required | Description |
|---|---|---|---|
| `mesh_path` | string | yes | Content path of the static mesh asset to use for each step |
| `step_count` | integer | yes | Number of steps in the staircase |
| `step_height` | number |  | Vertical height offset per step in cm (default: 20) |
| `step_depth` | number |  | Horizontal depth offset per step in cm (default: 30) |
| `step_width` | number |  | Width of each step in cm; used only for labeling, mesh defines actual width (default: 100) |
| `start_x` | number |  | X position of the first step (default: 0) |
| `start_y` | number |  | Y position of the first step (default: 0) |
| `start_z` | number |  | Z position of the first step (default: 0) |
| `yaw` | number |  | Yaw rotation of the staircase in degrees (default: 0). Steps progress forward along this direction. |

### `create_trigger_volume`

Creates a TriggerBox actor with configurable position, extents, and optional tag. Useful for creating gameplay trigger zones.

| Argument | Type | Required | Description |
|---|---|---|---|
| `label` | string | yes | Label for the trigger box actor |
| `x` | number |  | X position (default: 0) |
| `y` | number |  | Y position (default: 0) |
| `z` | number |  | Z position (default: 0) |
| `extent_x` | number |  | Box half-extent along X in cm (default: 100) |
| `extent_y` | number |  | Box half-extent along Y in cm (default: 100) |
| `extent_z` | number |  | Box half-extent along Z in cm (default: 100) |
| `tag` | string |  | Optional tag to apply to the actor |

## Material (5 tools)

### `assign_material`

Assign a material to a static mesh actor's material slot. Works on any actor with a mesh component.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the target actor |
| `material_path` | string | yes | Content path of the material to assign |
| `slot_index` | integer |  | Material slot index (default: 0) |

### `create_material`

Create a new Material asset with specified shading model and blend mode. The material is saved and ready for parameter editing.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new material (e.g., '/Game/Materials/M_MyMaterial') |
| `shading_model` | enum(DefaultLit\|Unlit\|Subsurface\|ClearCoat\|TwoSidedFoliage) |  | Shading model |
| `blend_mode` | enum(Opaque\|Masked\|Translucent\|Additive) |  | Blend mode |
| `two_sided` | boolean |  | Enable two-sided rendering (default: false) |

### `create_material_instance`

Create a Material Instance Constant from a parent material. Parameters can then be set using set_material_scalar/set_material_vector.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new MI |
| `parent_path` | string | yes | Content path of the parent material |

### `set_material_scalar`

Set a scalar parameter value on a Material Instance Constant.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Material Instance path |
| `parameter_name` | string | yes | Scalar parameter name |
| `value` | number | yes | Scalar value |

### `set_material_vector`

Set a vector (color) parameter value on a Material Instance Constant.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Material Instance path |
| `parameter_name` | string | yes | Vector parameter name |
| `r` | number | yes | Red channel (0-1) |
| `g` | number | yes | Green channel (0-1) |
| `b` | number | yes | Blue channel (0-1) |
| `a` | number |  | Alpha channel (0-1, default: 1) |

## MaterialGraph (8 tools)

### `add_material_expression`

Add a material expression node to a UMaterial asset's expression graph. Specify the class by its short Unreal name (e.g. 'MaterialExpressionAdd'). Returns the zero-based index of the newly created expression for use in subsequent connect/set calls.

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_path` | string | yes | Content path of the target UMaterial asset (e.g., '/Game/Materials/M_MyMat') |
| `expression_class` | string | yes | Unreal expression class name without the leading 'U' prefix, e.g. 'MaterialExpressionAdd', 'MaterialExpressionMultiply', 'MaterialExpressionConstant', 'MaterialExpressionVectorParameter', 'MaterialExpressionTextureSample' |
| `node_x` | integer |  | Editor graph X position for the new node (default: 0) |
| `node_y` | integer |  | Editor graph Y position for the new node (default: 0) |

### `add_material_parameter_expression`

Add a scalar, vector, or texture parameter expression to a material. Parameter expressions are exposed when creating material instances, allowing per-instance value overrides without recompiling. Returns the new expression's index.

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_path` | string | yes | Content path of the UMaterial asset |
| `parameter_type` | enum(Scalar\|Vector\|Texture) | yes | Type of parameter expression to add |
| `parameter_name` | string | yes | Name for the parameter as it appears in material instances (e.g., 'Roughness', 'BaseColor') |
| `default_value` | string |  | Default value for the parameter. Scalar: '0.5'. Vector: '1.0,0.5,0.0,1.0' (R,G,B,A). Texture: omit or unused. |
| `node_x` | integer |  | Editor graph X position (default: -400) |
| `node_y` | integer |  | Editor graph Y position (default: 0) |

### `add_texture_sample_expression`

Convenience tool: add a UMaterialExpressionTextureSample node to a material with a specific texture asset already assigned. Returns the new expression's index for use in connect_material_expression.

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_path` | string | yes | Content path of the UMaterial asset |
| `texture_path` | string | yes | Content path of the UTexture asset to assign to the sample node (e.g., '/Game/Textures/T_Rock_D') |
| `node_x` | integer |  | Editor graph X position (default: -300) |
| `node_y` | integer |  | Editor graph Y position (default: 0) |

### `compile_material`

Trigger a full recompile of a UMaterial by calling PreEditChange + PostEditChange. Call this after making expression graph changes (add/connect/set/remove) to apply them. The material will be saved with pending changes marked dirty.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_path` | string | yes | Content path of the UMaterial asset to recompile |

### `connect_material_expression`

Connect a material expression's output pin to another expression's input pin or to a material output slot (BaseColor, Metallic, etc.). Set target_expression_index to -1 to connect to a material output; target_input_index then selects the slot: 0=BaseColor 1=Metallic 2=Specular 3=Roughness 4=EmissiveColor 5=Opacity 6=Normal 7=WorldPositionOffset.

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_path` | string | yes | Content path of the UMaterial asset |
| `source_expression_index` | integer | yes | Zero-based index of the source expression in the material's Expressions array |
| `source_output_index` | integer |  | Index of the output pin on the source expression (default: 0) |
| `target_expression_index` | integer | yes | Zero-based index of the target expression. Use -1 to target a material output slot (BaseColor=0, Metallic=1, Specular=2, Roughness=3, EmissiveColor=4, Opacity=5, Normal=6, WorldPositionOffset=7) |
| `target_input_index` | integer |  | Index of the input pin on the target expression, or material output slot index when target_expression_index is -1 (default: 0) |

### `get_material_expressions`

List every expression node in a UMaterial's graph with its index, class name, editor graph position, and connected input information. Use the returned indices with add/connect/set/remove tools.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_path` | string | yes | Content path of the UMaterial asset to inspect |

### `remove_material_expression`

Remove a material expression node at the given index. Any connections referencing this expression will be cleared automatically. WARNING: Removing an expression will shift the indices of all subsequent expressions. Use get_material_expressions to verify current indices before calling.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_path` | string | yes | Content path of the UMaterial asset |
| `expression_index` | integer | yes | Zero-based index of the expression to remove from the material's Expressions array |

### `set_material_expression_value`

Set a default value or property on a material expression node by index. Supports setting scalar constants (Const), per-channel vector values (ConstR/ConstG/ConstB/ConstA), vector parameter defaults (DefaultValue as 'R,G,B,A'), and parameter names (ParameterName).

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_path` | string | yes | Content path of the UMaterial asset |
| `expression_index` | integer | yes | Zero-based index of the expression in the Expressions array |
| `property_name` | string | yes | Property to set. Common values: 'Const' (UMaterialExpressionConstant scalar), 'ConstR'/'ConstG'/'ConstB'/'ConstA' (Constant3/4Vector channels), 'DefaultValue' (vector parameter FLinearColor as 'R,G,B,A'), 'ParameterName' (FName for parameter expressions) |
| `value` | string | yes | String representation of the value. Floats: '0.5'. FLinearColor: '1.0,0.5,0.0,1.0'. FName: 'MyParamName'. |

## MaterialLayer (4 tools)

### `mat_layer_add`

Append a layer (and optional matching blend) onto a material instance's layer stack. layer_path is a UMaterialFunction asset; blend_path is required for non-base layers.

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_instance` | string | yes | Material Instance asset path. |
| `layer_path` | string | yes | UMaterialFunctionInterface asset path for the new layer. |
| `blend_path` | string |  | Optional matching blend function path. |

### `mat_layer_get_stack`

Return the material layer stack of a material instance: arrays of layer / blend function asset paths. Index 0 of layers is the base layer (no matching blend).

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_instance` | string | yes | Material Instance asset path. |

### `mat_layer_remove`

Remove a layer (and its matching blend) from a material instance's layer stack at an index. Index 0 is the base layer; removing it shifts the next layer down.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_instance` | string | yes | Material Instance asset path. |
| `index` | integer | yes | Layer index to remove (0-based). |

### `mat_layer_set_blend`

Replace the blend function at a given blend-index. Pass blend_path='' to clear the slot.

| Argument | Type | Required | Description |
|---|---|---|---|
| `material_instance` | string | yes | Material Instance asset path. |
| `blend_index` | integer | yes | Blend index (0-based). |
| `blend_path` | string | yes | UMaterialFunctionInterface asset path or '' to clear. |

## Meta (8 tools)

### `cancel_task`

Request cooperative cancellation of a working background task. The tool stops at its next cancellation checkpoint; poll get_task_status to confirm.

| Argument | Type | Required | Description |
|---|---|---|---|
| `task_id` | string | yes | Task id to cancel |

### `export_tool_docs`

Generate the complete tool-reference markdown from the LIVE registry (names, categories, descriptions, argument tables, annotations) and write it to Saved/MCPDocs/ToolReference.md. Kills documentation drift: counts and signatures always match the build.

*idempotent*

_No arguments._

### `get_task_status`

Poll a background task started by a long-running tool (the tool returned {task_id, status:'working'}). Returns status (working/completed/failed/cancelled), progress when available, and the final tool result once finished.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `task_id` | string | yes | Task id from the original tool call |

### `get_tool_schemas`

Fetch the complete definitions (description, full input schema with per-parameter docs, annotations) for one or more tools by name. Use after search_tools / list_tool_categories.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `names` | array<string> | yes | Tool names to fetch (max 25 per call) |

### `list_tasks`

List tracked background tasks (newest first): working ones plus recently finished (kept ~1 hour).

*read-only · idempotent*

_No arguments._

### `list_tool_categories`

List all tool categories with their tool counts and names. The catalog's table of contents — combine with search_tools and get_tool_schemas to find what you need.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `include_tool_names` | boolean |  | Include the tool-name list per category (default true) |

### `run_tool_script`

Run a bounded tool script after structural, permission and known-argument preflight. Steps support tool, args, save_as, foreach and as; prior results use $name.field references. Result-dependent arguments and editor state are checked during execution. Failures may leave edits in place: no automatic rollback is claimed, and recorded changes remain available to editor undo. dry_run performs preflight only without executing children. Limits: 100 steps, 1000 invocations; nested scripts are prohibited.

*preview*

| Argument | Type | Required | Description |
|---|---|---|---|
| `dry_run` | boolean |  | Preflight only; do not execute child tools |
| `script` | object | yes | The step program: {steps: [...]} |

### `search_tools`

Search the tool catalog by keyword. Returns matching tool names with one-line summaries and categories — call get_tool_schemas next for the full definitions of the tools you want to use. This server exposes 350+ tools; search instead of guessing names.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `query` | string | yes | Keywords describing what you want to do (e.g. 'spawn niagara particles', 'blueprint variable') |
| `category` | string |  | Optional: restrict to one category (see list_tool_categories) |
| `limit` | integer |  | Max results (default 10, max 50) |

## MetaHuman (4 tools)

### `metahuman_attach_to_skeletal_mesh`

Assign a USkeletalMesh asset (typically a MetaHuman body or face mesh) onto an actor's skeletal mesh component. The component must already exist on the actor.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_label` | string | yes | Target actor. |
| `skeletal_mesh_path` | string | yes | USkeletalMesh asset path. |

### `metahuman_import`

Import a MetaHuman from Quixel Bridge into the project. Requires the MetaHuman + Quixel Bridge plugins. Currently registered surface only — implementation depends on the plugin's Python bridge entry points.

| Argument | Type | Required | Description |
|---|---|---|---|
| `metahuman_id` | string | yes | MetaHuman identifier as exposed by Quixel Bridge. |

### `metahuman_list_assets`

List MetaHuman skeletal mesh assets in the project. Uses the AssetRegistry, filtering USkeletalMesh assets whose package path contains '/MetaHumans/'.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `limit` | integer |  | Max results (default 100). |

### `metahuman_set_lod`

Force a forced LOD index on an actor's skeletal mesh component. -1 disables forcing and lets streaming pick. Works for any skeletal mesh actor, not just MetaHumans.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_label` | string | yes | Editor label of the actor. |
| `lod` | integer | yes | Forced LOD index (>=0) or -1 to clear. |

## MetaSound (6 tools)

### `create_metasound_source`

Create a new MetaSound Source asset at the specified content path. The MetaSound plugin must be enabled in Edit > Plugins. The asset is saved to disk immediately after creation.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new MetaSound Source asset (e.g., '/Game/Audio/MS_MySound'). |

### `duplicate_metasound`

Duplicate an existing MetaSound Source or Patch asset to create a variant. Uses IAssetTools::DuplicateAsset so that all internal MetaSound graph data is properly deep-copied. The duplicate is saved to disk immediately.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `source_path` | string | yes | Content path of the MetaSound asset to duplicate (e.g., '/Game/Audio/MS_Base'). |
| `dest_path` | string | yes | Destination content folder for the duplicate (e.g., '/Game/Audio/Variants/'). |
| `new_name` | string | yes | Name for the new duplicate asset (e.g., 'MS_BaseVariant'). |

### `get_metasound_info`

Return information about a MetaSound asset: asset name, class, output format (channels), duration, and looping flag. Properties are read via UObject reflection.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the MetaSound Source or Patch asset (e.g., '/Game/Audio/MS_MySound'). |

### `list_metasound_assets`

List all MetaSound Source and MetaSound Patch assets in the project using the Asset Registry. Returns name, content path, and class for each asset found.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search recursively (default: '/Game/'). |
| `name_filter` | string |  | Optional substring filter applied to the asset name. |
| `limit` | integer |  | Maximum number of results to return (default: 100). |

### `set_metasound_parameter`

Set a default parameter value on a MetaSound asset using UObject property reflection. The parameter_name must match a UPROPERTY on the MetaSound class. Use get_metasound_info to discover the asset's class and then consult the MetaSound documentation for available properties (e.g., 'OutputFormat', 'bLooping').

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the MetaSound asset (e.g., '/Game/Audio/MS_MySound'). |
| `parameter_name` | string | yes | Name of the property to set on the MetaSound asset as exposed via UObject reflection. |
| `value` | string | yes | New value expressed as a string. Booleans: 'true'/'false'. Numbers: '1.5'. Enums: display name string. |
| `type` | enum(Float\|Int\|Bool\|String) |  | Hint for value interpretation (Float, Int, Bool, String). Default: Float. |

### `set_metasound_quality`

Set quality and output settings on a MetaSound Source asset using UObject reflection. Supports looping, duration, and output channel count (mapped to the OutputFormat enum). Only parameters that are explicitly provided are modified.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the MetaSound Source asset (e.g., '/Game/Audio/MS_MySound'). |
| `is_looping` | boolean |  | Set whether the MetaSound plays in a continuous loop. |
| `duration` | number |  | Override the duration in seconds (if the MetaSound asset exposes a duration property). |
| `output_channels` | integer |  | Set the output channel count: 1 = Mono, 2 = Stereo, 4 = Quad, 6 = 5.1, 8 = 7.1. Maps to the OutputFormat enum on UMetaSoundSource. |

## MetaSoundGraph (8 tools)

### `metasound_add_node`

Insert a node into a MetaSound graph. node_class_path is a MetaSound Frontend class name "Namespace.Name" or "Namespace.Name.Variant" (e.g. "UE.Sine.Audio"). Returns the new node's GUID (node_id) for use with connect_pins / set_node_property. Marks the asset dirty; call metasound_compile to persist.

| Argument | Type | Required | Description |
|---|---|---|---|
| `metasound_path` | string | yes | Asset path to the MetaSound. |
| `node_class_path` | string | yes | Frontend class name 'Namespace.Name[.Variant]'. |
| `major_version` | integer |  | Node class major version (default 1). |

### `metasound_compile`

Mark the MetaSound asset dirty and save it to disk. The MetaSound builder rebuilds the Frontend document on next access, which is the engine's compile path. Per-node error surfacing requires the MetasoundFrontend module.

| Argument | Type | Required | Description |
|---|---|---|---|
| `metasound_path` | string | yes | Asset path to compile. |

### `metasound_connect_pins`

Wire an output pin to an input pin inside a MetaSound graph. Pins are addressed by vertex name (the label shown in the MetaSound editor). Types must be compatible. Marks the asset dirty.

| Argument | Type | Required | Description |
|---|---|---|---|
| `metasound_path` | string | yes | Asset path. |
| `from_node` | string | yes | Source node GUID. |
| `from_pin` | string | yes | Source output vertex name. |
| `to_node` | string | yes | Destination node GUID. |
| `to_pin` | string | yes | Destination input vertex name. |

### `metasound_disconnect_pin`

Remove the connection feeding a destination input pin inside a MetaSound graph. Marks dirty.

| Argument | Type | Required | Description |
|---|---|---|---|
| `metasound_path` | string | yes | Asset path. |
| `to_node` | string | yes | Destination node GUID. |
| `to_pin` | string | yes | Destination input vertex name to clear. |

### `metasound_get_graph`

Return a structural summary of a MetaSound asset: class, package, exposed inputs/outputs, and any graph metadata reachable via UObject reflection. For full node-and-connection introspection the MetasoundFrontend module is required (see hint on mutation tools).

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `metasound_path` | string | yes | Asset path to a MetaSound Source or Patch. |

### `metasound_list_node_classes`

Enumerate MetaSound node classes registered with the engine. Returns a list of {class_name, full_name, namespace} entries discovered via the UObject reflection system. Useful for discovering valid node_class_path values for metasound_add_node.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `name_filter` | string |  | Optional substring filter. |

### `metasound_remove_node`

Remove a node (by GUID) from a MetaSound graph. Also removes its edges. Marks the asset dirty.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `metasound_path` | string | yes | Asset path. |
| `node_id` | string | yes | Node GUID to remove (from metasound_add_node / metasound_get_graph). |

### `metasound_set_node_property`

Set the literal default value on a node input pin. The value string is coerced to the input's data type (float / int / bool / string heuristic). Marks the asset dirty.

| Argument | Type | Required | Description |
|---|---|---|---|
| `metasound_path` | string | yes | Asset path. |
| `node_id` | string | yes | Node GUID. |
| `property_name` | string | yes | Input vertex name to set the default on. |
| `value` | string | yes | Value as a string; coerced by the MetaSound type system. |

## Modeling (5 tools)

### `modeling_boolean`

Apply a CSG boolean (union / intersect / subtract) between two static-mesh actors' meshes, in their current world arrangement. The result replaces actor_a's MESH ASSET (every instance updates); actor_b is left untouched — delete or hide it afterwards if it was only a cutting tool.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_a` | string | yes | Primary actor (its mesh asset receives the result). |
| `actor_b` | string | yes | Secondary actor (operand). |
| `operation` | enum(union\|intersect\|subtract) | yes | Boolean operation. |

### `modeling_polycut`

Cut a static-mesh actor's mesh with a world-space plane (origin + normal). The side the normal points toward is removed; holes are filled. Modifies the MESH ASSET (every instance updates).

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_label` | string | yes | Actor whose mesh to cut. |
| `origin_x` | number | yes | Plane origin X (world). |
| `origin_y` | number | yes | Plane origin Y (world). |
| `origin_z` | number | yes | Plane origin Z (world). |
| `normal_x` | number | yes | Plane normal X. |
| `normal_y` | number | yes | Plane normal Y. |
| `normal_z` | number | yes | Plane normal Z. |

### `modeling_polyextrude`

Extrude faces of a static-mesh actor's mesh by a distance along the average face normal. Provide face_indices (triangle indices) to extrude a subset, or omit to extrude ALL faces. Modifies the MESH ASSET.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_label` | string | yes | Actor whose mesh to extrude. |
| `distance` | number | yes | Extrusion distance in world units (negative = inset). |
| `face_indices` | array<string> |  | Triangle indices to extrude (numeric). Omit to extrude all faces. |

### `modeling_remesh`

Uniformly remesh a static mesh asset toward a target triangle count (use for topology cleanup or densification; for pure reduction prefer the simplify pipeline). Can be slow on dense meshes.

*long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Static mesh asset path. |
| `target_triangles` | integer | yes | Approximate target triangle count (100 - 2,000,000). |

### `modeling_uv_unwrap`

Generate UVs for a static mesh asset's UV channel. Methods: planar / box / cylindrical projections (sized to the mesh bounds), or 'auto' (XAtlas auto-unwrap, best general choice).

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Static mesh asset path. |
| `uv_channel` | integer |  | UV channel index (default 0). |
| `method` | enum(planar\|cylindrical\|box\|auto) |  | Unwrap method (default: auto). |

## Montage (14 tools)

### `montage_add_section`

Add a named composite section to a montage at a given time. Sections are the addressable entry points for Montage_JumpToSection() at runtime — a combo is built as several sections on one montage. Adding a section splits the timeline: the previous section now ends where this one begins. Use montage_link_sections afterwards to control what plays next.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `section_name` | string | yes | Name for the new section (e.g. 'Combo2'). Must be unique within the montage. |
| `start_time` | number | yes | Section start time in seconds, within [0, montage duration] |
| `next_section` | string |  | Optional: name of the section to chain to when this one ends. Pass this section's own name to loop it. |

### `montage_add_segment`

Append an animation segment to one of a montage's slot tracks. Multi-segment slot tracks are how a montage stitches several sequences into one timeline (windup / strike / recovery as three clips). The source animation must share the montage's skeleton. Segments are appended at the end of the track unless start_time is given, and the montage length grows to fit.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `animation_path` | string | yes | Content path to the UAnimSequence to append |
| `slot_name` | string |  | Slot track to append to (default: the montage's first slot) |
| `start_time` | number |  | Position of the segment on the montage timeline in seconds (default: end of the track) |
| `anim_start_time` | number |  | Trim: where in the source animation the segment starts (default: 0) |
| `anim_end_time` | number |  | Trim: where in the source animation the segment ends (default: source play length) |
| `play_rate` | number |  | Segment play rate multiplier (default: 1.0) |
| `loop_count` | integer |  | How many times the segment repeats (default: 1) |

### `montage_add_slot`

Add a slot track to a montage and register the slot name on the target skeleton. Registration is the part that is easy to miss: an unregistered slot never appears in the AnimGraph's Slot node dropdown, so the montage plays into nothing. Slots let one montage drive different parts of the AnimGraph (e.g. 'DefaultSlot' for full body, 'UpperBody' for an additive layer).

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `slot_name` | string | yes | Slot name (e.g. 'UpperBody'). Registered on the skeleton if not already present. |
| `slot_group` | string |  | Skeleton slot group to file the slot under (default: 'DefaultGroup') |

### `montage_create_from_sections`

Build a complete multi-section montage in one call: creates the asset, appends each animation as a segment on one slot track, names a section at each segment boundary, and chains the sections in order. This is the fast path for combos — three attack sequences become a three-section montage the gameplay code drives with Montage_JumpToSection. All animations must share one skeleton. Set loop_last=true to make the final section loop on itself (charge-and-hold).

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new montage (e.g. '/Game/Characters/AM_HeroCombo') |
| `animation_paths` | array<string> | yes | Ordered list of UAnimSequence paths, one per section |
| `section_names` | array<string> |  | Optional section names, one per animation. Defaults to 'Section1', 'Section2', ... |
| `slot_name` | string |  | Slot track name (default: 'DefaultSlot') |
| `blend_in_time` | number |  | Blend in duration in seconds (default: 0.25) |
| `blend_out_time` | number |  | Blend out duration in seconds (default: 0.25) |
| `chain_sections` | boolean |  | Chain each section to the next so the montage plays straight through (default: true). Set false so each section must be jumped to explicitly — the usual choice for input-driven combos. |
| `loop_last` | boolean |  | Point the final section at itself so it loops (default: false) |

### `montage_get_sections`

Read a montage's full structure: sections (start/end time, length, and the next section each one chains to), slot tracks with their animation segments, blend in/out settings, and sync group. This is the read-back tool for montage authoring — call it after any montage_* edit to verify the result. A section's end time is implicit: it is the start of the next section by time, or the montage length.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage (e.g. '/Game/Characters/AM_HeroAttack') |

### `montage_link_sections`

Set which section plays after a given section finishes (FCompositeSection::NextSectionName). This is how montage flow is authored: point a section at itself to loop it (an idle-hold or a charge-up), point it at the next combo step to chain, or clear it so the montage blends out. Clear by passing an empty next_section.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `section_name` | string | yes | Section whose successor is being set |
| `next_section` | string | yes | Section to play next. Pass the same name as section_name to loop. Pass an empty string to clear the link (montage blends out). |

### `montage_remove_section`

Remove a named composite section from a montage. Refuses to remove the last remaining section — a montage with no section starting at 0 has no playback entry point. Any other section that chained to the removed one has its next_section cleared, and those are reported.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `section_name` | string | yes | Name of the section to remove |

### `montage_remove_segment`

Remove an animation segment from a montage slot track by index (see montage_get_sections for segment indices). The montage length is recomputed and sections are relinked, so removing a segment can shorten the montage and pull later sections past its end.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `segment_index` | integer | yes | Index of the segment within the slot track |
| `slot_name` | string |  | Slot track to remove from (default: the montage's first slot) |

### `montage_set_blend_profile`

Assign per-bone blend profiles to a montage's blend in and/or blend out. A blend profile scales blend time per bone, so an upper-body attack can snap on at the hands while the spine eases in. Profiles are authored in the Skeleton editor and cannot be created from script; this tool selects an existing one by name and lists the available profiles if the name is wrong. Pass an empty string to clear.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `blend_in_profile` | string |  | Blend profile name for the blend in, or empty to clear |
| `blend_out_profile` | string |  | Blend profile name for the blend out, or empty to clear |

### `montage_set_blend_settings`

Set a montage's blend in/out timing and curve. blend_out_trigger_time is the subtle one: a negative value (the default) means the blend out finishes exactly as the montage ends, so the blend eats the tail of the animation; a value >= 0 starts the blend that many seconds before the end instead. Set enable_auto_blend_out=false for montages you intend to end explicitly with Montage_Stop.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `blend_in_time` | number |  | Blend in duration in seconds (e.g. 0.25) |
| `blend_out_time` | number |  | Blend out duration in seconds (e.g. 0.25) |
| `blend_out_trigger_time` | number |  | When to start blending out. Negative (default) = blend finishes as the montage ends. >= 0 = start blending that many seconds before the end. |
| `blend_in_option` | enum(Linear\|Cubic\|HermiteCubic\|Sinusoidal\|QuadraticInOut\|CubicInOut\|QuarticInOut\|QuinticInOut\|CircularIn\|CircularOut\|CircularInOut\|ExpIn\|ExpOut\|ExpInOut\|Custom) |  | Interpolation curve for the blend in |
| `blend_out_option` | enum(Linear\|Cubic\|HermiteCubic\|Sinusoidal\|QuadraticInOut\|CubicInOut\|QuarticInOut\|QuinticInOut\|CircularIn\|CircularOut\|CircularInOut\|ExpIn\|ExpOut\|ExpInOut\|Custom) |  | Interpolation curve for the blend out |
| `enable_auto_blend_out` | boolean |  | Whether the montage blends out automatically when it reaches the end (default: true) |

### `montage_set_rate_scale`

Set a montage's global RateScale — a multiplier applied on top of the play rate passed to Montage_Play at runtime. Use it to retime an entire montage (and everything anchored to it: sections, notifies) without touching the source animations.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `rate_scale` | number | yes | Playback rate multiplier; 1.0 = authored speed. Must be non-zero. |

### `montage_set_section_time`

Move a montage section to a new start time. Because a section's end is implicit (the next section's start), moving one section resizes its neighbours; the result reports every section's new start/end so the effect is visible. The section starting at 0.0 cannot be moved off 0.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `section_name` | string | yes | Name of the section to move |
| `start_time` | number | yes | New start time in seconds, within [0, montage duration] |

### `montage_set_sync_group`

Put a montage into a sync group so its playback position follows the group leader — the mechanism that keeps an upper-body montage in step with the locomotion underneath it (e.g. a reload that must stay on the walk cycle's footfalls). sync_slot_index selects which of the montage's slot tracks provides the sync position. Pass an empty sync_group to clear.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |
| `sync_group` | string | yes | Sync group name, or empty to clear |
| `sync_slot_index` | integer |  | Index of the slot track that drives sync (default: 0) |

### `montage_validate`

Check a montage for the structural problems that make it fail silently at runtime: sections chaining to names that do not exist, no section at time 0, slot names not registered on the skeleton (so no AnimGraph Slot node can play them), empty slot tracks, segments whose source animation is missing or targets a different skeleton, notifies past the montage end, and unreachable sections. Read-only — reports, never repairs.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the UAnimMontage |

## MorphTarget (4 tools)

### `morph_clear`

Clear all morph target weights set via SetMorphTarget on an actor's component.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_label` | string | yes | Target actor. |

### `morph_get_weights`

Read the currently-applied morph target weights (set via SetMorphTarget) on an actor's skeletal mesh component.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_label` | string | yes | Target actor. |

### `morph_list_targets`

List the morph targets (blendshapes) on a skeletal mesh. Provide either skeletal_mesh_path (asset) or actor_label (uses the actor's skeletal mesh component). Returns morph target names.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `skeletal_mesh_path` | string |  | USkeletalMesh asset path (optional). |
| `actor_label` | string |  | Actor whose skeletal mesh to inspect (optional). |

### `morph_set_weight`

Set a morph target (blendshape) weight on an actor's skeletal mesh component. Weight is typically 0..1 (values outside are allowed for over/under-driving). Use morph_list_targets to find valid names.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_label` | string | yes | Target actor. |
| `morph_target` | string | yes | Morph target name. |
| `weight` | number | yes | Weight (0..1 nominal). |

## Navigation (3 tools)

### `build_navigation`

Trigger a navigation mesh build for the current level. Requires at least one NavMeshBoundsVolume in the level.

*idempotent · long-running*

_No arguments._

### `get_navigation_info`

Get navigation system information: navmesh bounds, agent settings, and build status.

*read-only · idempotent*

_No arguments._

### `query_navigation_path`

Find a navigation path between two world positions. Returns path points, total distance, and whether the path is complete.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `start_x` | number | yes | Start X position |
| `start_y` | number | yes | Start Y position |
| `start_z` | number | yes | Start Z position |
| `end_x` | number | yes | End X position |
| `end_y` | number | yes | End Y position |
| `end_z` | number | yes | End Z position |

## Networking (5 tools)

### `get_component_replication`

Get a list of all components on an actor with their replication state. Reports: component name, class, bIsReplicated, and bReplicateUsingRegisteredSubObjectList for each component.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor whose components to inspect |

### `get_replication_info`

Get a full report of the network replication settings on an actor: bReplicates, bReplicateMovement, bAlwaysRelevant, bOnlyRelevantToOwner, NetUpdateFrequency, MinNetUpdateFrequency, NetPriority, NetDormancy, and the current net role.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to inspect for replication settings |

### `set_component_replication`

Enable or disable network replication on a specific component of an actor. The actor must have replication enabled (bReplicates) for component replication to have any effect at runtime. Use get_component_replication to list component names.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor that owns the component |
| `component_name` | string | yes | Name of the component to configure (use get_component_replication to list component names) |
| `replicate` | boolean | yes | True to enable replication on the component, false to disable it |

### `set_net_dormancy`

Set the network dormancy mode on an actor to control how aggressively replication bandwidth is conserved. Dormant actors stop sending replication updates until manually woken with FlushNetDormancy. Use DORM_DormantAll for static or infrequently-updated actors to save bandwidth.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to configure network dormancy on |
| `dormancy` | enum(DORM_Never\|DORM_Awake\|DORM_DormantAll\|DORM_DormantPartial\|DORM_Initial) | yes | Network dormancy mode. DORM_Never: always replicate. DORM_Awake: replicate while awake. DORM_DormantAll: dormant for all connections (most bandwidth-efficient). DORM_DormantPartial: dormant for some connections. DORM_Initial: starts dormant until explicitly woken. |

### `set_replication_settings`

Configure network replication settings on an actor. Only the parameters you provide are applied. Changes are wrapped in an undo transaction. Use get_replication_info to inspect the current state before modifying.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to configure replication settings on |
| `replicate` | boolean |  | Enable or disable replication on the actor (bReplicates) |
| `replicate_movement` | boolean |  | Enable or disable movement replication (bReplicateMovement) |
| `always_relevant` | boolean |  | If true, this actor is always relevant to all clients (bAlwaysRelevant) |
| `only_relevant_to_owner` | boolean |  | If true, only the owning client receives this actor's replication updates (bOnlyRelevantToOwner) |
| `net_update_frequency` | number |  | How many times per second the actor checks for replication updates (NetUpdateFrequency). Typical range: 1-100. |
| `min_net_update_frequency` | number |  | Minimum update frequency when the actor is not moving or changing (MinNetUpdateFrequency). Must be <= net_update_frequency. |
| `net_priority` | number |  | Priority given to this actor when bandwidth is constrained. Higher value = sent first (NetPriority). Typical range: 1.0-5.0. |

## Niagara (3 tools)

### `get_niagara_parameters`

List all user-exposed parameters on a Niagara system, including their names, types, and any current override values set on the component.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor containing the NiagaraComponent |

### `set_niagara_parameter`

Set a user-exposed parameter on a NiagaraComponent attached to an actor. Use get_niagara_parameters first to discover available parameter names and types.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor containing the NiagaraComponent |
| `parameter_name` | string | yes | Name of the user-exposed Niagara parameter (with or without 'User.' prefix) |
| `value` | string | yes | New value as a string. Float: '1.5', Int: '3', Bool: 'true'/'false', Vector: 'X=1 Y=2 Z=3', Color: '(R=1,G=0,B=0,A=1)' |
| `type` | enum(Float\|Int\|Bool\|Vector\|Color) |  | Parameter type to parse the value as (default: Float) |

### `spawn_niagara_system`

Spawn a Niagara particle system actor in the current level at a given world position. The system_path must point to a valid UNiagaraSystem asset in the content browser.

| Argument | Type | Required | Description |
|---|---|---|---|
| `system_path` | string | yes | Content path to the NiagaraSystem asset (e.g., '/Game/FX/NS_Fire') |
| `x` | number |  | X position in the world (default: 0) |
| `y` | number |  | Y position in the world (default: 0) |
| `z` | number |  | Z position in the world (default: 0) |
| `label` | string |  | Optional actor label shown in the scene outliner |

## Operations (6 tools)

### `cancel_editor_operation`

Request cooperative cancellation of an owned operation. Queued work never starts; running work stops at its next step boundary and scenario-owned PIE is stopped. Already finished operations keep their terminal state (a committed success never becomes a rollback).

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `operation_id` | string | yes | Operation identifier |

### `get_editor_operation`

Poll an owned operation: state (queued, running, cancel_requested, succeeded, failed, cancelled), progress, per-step states, frames captured, timestamps and optionally the event log. With include_frame=true the newest captured frame is returned inline. Finished operations remain readable for 10 minutes.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `operation_id` | string | yes | Operation identifier |
| `include_events` | boolean |  | Include the bounded event log (default false) |
| `include_frame` | boolean |  | Return the newest captured frame inline (default false) |

### `get_result_page`

Read a page of a stored oversized tool result. When a tools/call result exceeds the response cap (MaxToolResultKB), the server keeps the full serialized result for 10 minutes and answers with result_truncated=true and a result_id; call this with increasing offset until complete=true (or read unreal://results/{result_id} in one piece). Owner only; at most 16 stored results per session and 64 MiB overall.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `result_id` | string | yes | Result identifier from the truncated response |
| `offset` | integer |  | Character offset to start from (default 0) |
| `max_bytes` | integer |  | Page size in characters, 1024..1048576 (default 262144) |

### `list_editor_operations`

List this session's operations (running and recently finished), newest first, including legacy long-running tool tasks presented as kind=legacy_task.

*read-only · idempotent*

_No arguments._

### `run_editor_scenario`

Start an owned, tick-driven editor scenario and return an operation handle immediately. Steps (1..32, in order): start_pie, stop_pie, wait_seconds{seconds}, wait_for_actor{class?,label?,timeout}, assert_actor_count{class?,label?,min?,max?}, assert_property{class?,label?,property,expected,timeout}, add_widget{class = widget asset path}, send_input{key}, capture{surface_title?} (optional by default: headless editors have no pixels). Waits use per-step timeouts and a scenario deadline, never fixed sleeps on the game thread. A pre-existing play session is never taken over; PIE started by the scenario is stopped when it ends for any reason. Poll with get_editor_operation; cancel with cancel_editor_operation. Requires Scene scope (steps may add widgets and send input).

*PIE-off · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `name` | string |  | Short scenario name for reports |
| `deadline_seconds` | number |  | Whole-scenario deadline 1..600 (default 60) |
| `steps` | array<object> | yes | Ordered typed steps |

### `submit_generation_job`

Submit an external generation job without blocking the editor. Returns an owned operation (poll get_editor_operation, cancel with cancel_editor_operation). Providers: fal (queue API; requires the fal.ai key in Project Settings, which is sent only as a request header and never returned) and mock (no network, produces a small PNG or OBJ after a simulated delay; use it for tests and soak runs). Kinds: text_to_image (prompt), text_to_3d (prompt), image_to_3d (image_url), remove_background (image_url). model is a short key (list_3d_models / generate_ui_image docs); params are merged into the provider payload. The file is downloaded to Saved/MCPV5/Generated/<operation>/ (allowed: png jpg jpeg webp glb gltf fbx obj; max_bytes cap) and, with auto_import (default true), imported into destination_path on the game thread. Cancellation stops local work immediately and reports provider_cancellation honestly (accepted, rejected, not_applicable). The operation result carries provider_job_id, file_path, bytes, imported_asset and notes.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `provider` | enum(fal\|mock) | yes | fal or mock |
| `kind` | enum(text_to_image\|text_to_3d\|image_to_3d\|remove_background) | yes | Generation kind |
| `model` | string |  | Model key (default per kind: flux-2-flash, meshy-v6, trellis-2, birefnet-v2) |
| `prompt` | string |  | Prompt for text_* kinds |
| `image_url` | string |  | Public image URL for image_to_3d / remove_background |
| `params` | object |  | Extra provider payload fields (merged; for mock: delay_seconds) |
| `destination_path` | string |  | Content folder for the imported asset (default /Game/Generated) |
| `asset_name` | string |  | Asset and file base name (default generated) |
| `auto_import` | boolean |  | Import the downloaded file (default true); false leaves the file on disk |
| `max_bytes` | integer |  | Download size cap in bytes (default 104857600) |
| `deadline_seconds` | integer |  | Operation deadline 1..600 (default 600) |

## PCG (16 tools)

### `add_pcg_node`

Add a new node to a PCG Graph by specifying the UPCGSettings subclass to use. The node is appended to the graph and the package is marked dirty. Use get_pcg_graph_nodes to retrieve node indices for subsequent connect_pcg_nodes calls.

| Argument | Type | Required | Description |
|---|---|---|---|
| `graph_path` | string | yes | Content path of the PCG Graph asset (e.g., '/Game/PCG/MyGraph.MyGraph') |
| `settings_class` | string | yes | Name of the UPCGSettings subclass to instantiate. Common values: 'PCGSurfaceSamplerSettings', 'PCGStaticMeshSpawnerSettings', 'PCGDensityFilterSettings', 'PCGPointFilterSettings', 'PCGSelfPruningSettings'. The 'U' prefix is optional. |
| `node_x` | integer |  | Horizontal position of the node in the graph editor (default: 0) |
| `node_y` | integer |  | Vertical position of the node in the graph editor (default: 0) |

### `connect_pcg_nodes`

Connect an output pin of one PCG node to an input pin of another PCG node in the same graph. Node indices correspond to the array order returned by get_pcg_graph_nodes. Default pin names 'Out' and 'In' are used when source_pin_label / target_pin_label are omitted. The graph package is marked dirty after a successful connection.

| Argument | Type | Required | Description |
|---|---|---|---|
| `graph_path` | string | yes | Content path of the PCG Graph asset (e.g., '/Game/PCG/MyGraph.MyGraph') |
| `source_node_index` | integer | yes | Zero-based index of the source (output) node as returned by get_pcg_graph_nodes |
| `source_pin_label` | string |  | Name of the output pin on the source node (default: 'Out') |
| `target_node_index` | integer | yes | Zero-based index of the target (input) node as returned by get_pcg_graph_nodes |
| `target_pin_label` | string |  | Name of the input pin on the target node (default: 'In') |

### `create_pcg_graph`

Create a new UPCGGraph asset in the content browser at the specified path. The graph is saved immediately and registered with the asset registry. Use add_pcg_node to populate the graph with nodes after creation.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new PCG Graph asset (e.g., '/Game/PCG/MyGraph'). Do not include a file extension. |

### `execute_pcg`

Trigger PCG graph generation on the UPCGComponent attached to the named actor. The component's assigned PCG Graph will be executed immediately with a forced regeneration. Use spawn_pcg_actor to create a PCG actor first, then call this tool to run the graph.

*idempotent · long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor containing the UPCGComponent to execute |

### `get_asset_instance_impact`

Report every place a shared asset is used in a world before changing the asset itself. Supports StaticMesh (StaticMeshComponents), SkeletalMesh (SkeletalMeshComponents) and MaterialInterface (any mesh component slot; instances derived from a material are counted when include_derived is true). Returns per-instance actor label, path, component and slot, counts, and the asset registry's referencing packages. Use it to make the all-instance impact of an asset-level edit explicit.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | StaticMesh, SkeletalMesh or Material/Material Instance path |
| `world_id` | string |  | World path from list_worlds (default: editor world) |
| `include_derived` | boolean |  | For materials, also count instances whose parent chain contains the asset (default true) |
| `limit` | integer |  | Maximum instances listed (default 200, max 1000) |

### `get_pcg_graph_nodes`

List all nodes contained in a PCG Graph asset. Returns the node index, display title, settings class name, and editor position for each node. The node index can be used with add_pcg_node and connect_pcg_nodes.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `graph_path` | string | yes | Content path of the PCG Graph asset (e.g., '/Game/PCG/MyGraph.MyGraph') |

### `get_pcg_info`

Retrieve information about the UPCGComponent attached to the named actor. Reports the assigned graph name and path, seed, generation trigger type, actor location and scale (which defines the effective generation bounds).

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to inspect for a UPCGComponent |

### `list_pcg_graphs`

List all PCG Graph assets found in the project content browser. Optionally filter by path and/or name substring. Returns asset name and full content path for each graph.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search under (e.g., '/Game/', '/Game/PCG/'). Default: '/Game/' |
| `name_filter` | string |  | Optional substring filter applied to asset names (case-insensitive) |
| `limit` | integer |  | Maximum number of results to return (default: 100) |

### `preview_asset_import`

Preflight an asset import without importing anything. Resolves the source file (absolute path required, existence, size), detects the asset type from the extension and lists the editor factories that accept the file, validates the destination package path (mounted root, no traversal, protected roots such as /Engine are refused), computes the expected asset name and object path, checks for a destination collision and evaluates collision_policy (error, skip, replace; replace reports existing referencers). Rules: source_relative_path, source_missing, source_empty, unsupported_extension, no_factory, invalid_destination, protected_destination, invalid_asset_name, destination_collision (severity depends on policy). importable=true when no error remains. Nothing is read beyond file metadata; nothing is created.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `source_path` | string | yes | Absolute filesystem path of the file to import |
| `destination_path` | string | yes | Destination content folder, e.g. /Game/Imports |
| `asset_name` | string |  | Asset name override (default: sanitised file base name) |
| `collision_policy` | enum(error\|skip\|replace) |  | What the real import should do if the destination exists: error (default), skip, replace |

### `run_pcg_generation`

Generate a PCG component as an owned operation instead of a blocking call. Finds the actor's PCGComponent, starts GenerateLocal(force) and polls IsGenerating on the editor ticker; poll with get_editor_operation, cancel with cancel_editor_operation (the component's CancelGeneration is called and pcg_cancellation reports requested). The result carries generated (component flag), last_generated_bounds, generated_output_count (data collection entries) and dirty (package dirty flag). Requires Scene scope; PIE must be stopped.

*PIE-off*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_path` | string | yes | Exact loaded actor path with a PCGComponent |
| `force` | boolean |  | Force regeneration even when already generated (default true) |
| `deadline_seconds` | integer |  | Operation deadline 1..600 (default 300) |

### `set_pcg_static_mesh_spawner_meshes`

Assign one or more static meshes to a PCG Static Mesh Spawner node using the weighted mesh selector. Each mesh is added with equal weight. This is useful for completing PCG graphs created via MCP.

| Argument | Type | Required | Description |
|---|---|---|---|
| `graph_path` | string | yes | Content path of the PCG Graph asset (e.g., '/Game/PCG/MyGraph.MyGraph') |
| `node_index` | integer | yes | Zero-based node index of the Static Mesh Spawner node in the graph. |
| `mesh_paths` | array<string> | yes | Array of static mesh asset paths to assign to the spawner. |

### `spawn_pcg_actor`

Spawn a new Actor with a UPCGComponent in the current level. Optionally assign a PCG Graph asset to the component. The actor's scale is set to the provided scale values, which determines the effective PCG volume bounds. Use execute_pcg after spawning to trigger graph generation.

| Argument | Type | Required | Description |
|---|---|---|---|
| `graph_path` | string |  | Optional content path to a PCG Graph asset to assign (e.g., '/Game/PCG/MyGraph.MyGraph') |
| `x` | number |  | World X position to spawn the actor (default: 0) |
| `y` | number |  | World Y position to spawn the actor (default: 0) |
| `z` | number |  | World Z position to spawn the actor (default: 0) |
| `scale_x` | number |  | Scale X applied to the actor, effectively controlling PCG volume extent (default: 1.0) |
| `scale_y` | number |  | Scale Y applied to the actor, effectively controlling PCG volume extent (default: 1.0) |
| `scale_z` | number |  | Scale Z applied to the actor, effectively controlling PCG volume extent (default: 1.0) |
| `label` | string |  | Optional actor label shown in the scene outliner |
| `seed` | integer |  | Optional integer seed to set on the PCG component for deterministic generation |

### `validate_material_setup`

Validate a Material or Material Instance without compiling or saving. For a Material: domain, blend mode, shading models, substrate flag, expression count, connected material outputs, compile state for the current shader platform (compilation_finished, compile_errors[]), and rules texture_sample_without_texture, unused_expression (no output used and not wired to a material output), no_material_output_connected, compile_error. For a Material Instance: parent chain (missing_parent, parent_chain), override counts and the root material's compile errors. Issues carry rule, severity, where, message; ok=false on any error.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Material or Material Instance asset path |
| `limit` | integer |  | Maximum issues returned (default 200) |

### `validate_niagara_system`

Validate a Niagara System asset without compiling, spawning or saving. Reports system validity, emitters (name, enabled, sim_target, spawn/update script compile status), system script compile status, exposed user parameters and rules: system_invalid, no_emitters, emitter_disabled (info), missing_emitter (handle without emitter asset), script_not_compiled (status other than UpToDate). Unavailable Niagara plugin returns an unsupported error rather than fabricated output.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Niagara System asset path |
| `limit` | integer |  | Maximum issues returned (default 200) |

### `validate_pcg_graph`

Validate a PCG Graph asset's structure without executing it. Reports per node: title, settings class, enabled state, input pins (label, required, connected) and output pins. Rules: missing_settings, disabled_node, required_input_unconnected, dead_end_node (a non-output node whose outputs are all unconnected), output_node_unconnected (graph output receives nothing), dangling_edge (edge whose pins or nodes are invalid), no_input_node/no_output_node. Issues carry rule, severity, where and message; counts per rule; ok=false when any error. Nothing is generated, compiled or saved. Use execute_pcg to generate afterwards.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `graph_path` | string | yes | PCG Graph asset path |
| `include_nodes` | boolean |  | Include the per-node structure (default true) |
| `limit` | integer |  | Maximum issues returned (default 200, max 1000) |

### `validate_sound_setup`

Validate a sound asset (SoundWave, SoundCue or MetaSound source) without playing or saving. SoundWave: duration, channels, sample rate, looping, streaming; rules zero_duration, no_channels. SoundCue: node count, first node, wave players; rules cue_without_first_node, wave_player_without_wave, cue_zero_duration. MetaSound: reported as metasound with class name (use metasound_get_graph for graph checks). Common: sound_class, attenuation, volume and pitch.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Sound asset path |
| `limit` | integer |  | Maximum issues returned (default 200) |

## PIE (10 tools)

### `pie_attach_player_controller`

Possess an actor (must be a Pawn) at runtime with the given player controller. Useful for debugging gameplay flows.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_label` | string | yes | Editor label of the Pawn actor to possess. |
| `controller_index` | integer |  | Player controller index (default 0). |

### `pie_get_actor_property`

Read live property values from an actor (or one of its components) in the RUNNING PIE world. Defaults to player 0's pawn. Use 'component' to read a component (e.g. 'Health') instead of the actor. Omit property_names for all editable/blueprint-visible properties.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string |  | Actor label to read; if omitted, uses the player pawn |
| `player_index` | integer |  | Player index when reading the player pawn (default 0) |
| `component` | string |  | Read this component (name substring match) instead of the actor, e.g. 'Health' |
| `property_names` | array<string> |  | Specific properties to read; omit for all editable ones |

### `pie_get_state`

Query PIE state: is_running, is_paused, num_players, world_time_seconds, world_path, fps_estimate.

*read-only · idempotent*

_No arguments._

### `pie_pause`

Pause the active PIE session. Returns was_running=false if PIE was not active.

_No arguments._

### `pie_resume`

Resume a paused PIE session.

_No arguments._

### `pie_screenshot`

Capture the active PIE viewport as a PNG. Returns base64-encoded image data plus the captured dimensions.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `width` | integer |  | Target width in pixels (capped at viewport width, default = viewport). |
| `height` | integer |  | Target height in pixels (capped at viewport height, default = viewport). |

### `pie_send_input`

Synthesize a keyboard/mouse/gamepad event into the active PIE session. Key is an FKey name (e.g. 'SpaceBar', 'LeftMouseButton', 'Gamepad_FaceButton_Bottom').

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `key` | string | yes | FKey name (e.g. 'SpaceBar', 'LeftMouseButton'). |
| `event` | enum(Pressed\|Released\|Repeat) |  | Input event type (default 'Pressed'). |
| `controller_index` | integer |  | Player controller index (default 0). |

### `pie_start`

Start a Play-In-Editor session. Mode determines viewport (Selected/Standalone/MobilePreview/VRPreview). Returns an error if PIE is already running.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `mode` | enum(Selected\|Standalone\|MobilePreview\|VRPreview) |  | PIE preview mode (default 'Selected'). |
| `num_players` | integer |  | Number of players (1-4, default 1). |
| `window_width` | integer |  | Window width (only used for Standalone/MobilePreview). |
| `window_height` | integer |  | Window height. |

### `pie_step_frame`

Advance N frames while paused via UEditorEngine::PlaySessionSingleStepped(). PIE must be paused. Default 1 frame, range 1-60.

| Argument | Type | Required | Description |
|---|---|---|---|
| `frames` | integer |  | Number of frames to advance (1-60, default 1). |

### `pie_stop`

Terminate the active Play-In-Editor session. Idempotent: succeeds even if PIE is not running.

*destructive*

_No arguments._

## Performance (5 tools)

### `create_scene_from_template`

Create a pre-configured scene from a template. Combines floor, lighting, sky, post-processing, and template-specific elements. Templates: fps_arena (closed arena with cover), tps_playground (open area with obstacles), rpg_outdoor (landscape with trees), horror_interior (dark room with fog), empty_studio (clean lighting setup for showcasing).

| Argument | Type | Required | Description |
|---|---|---|---|
| `template` | enum(fps_arena\|tps_playground\|rpg_outdoor\|horror_interior\|empty_studio) | yes | Scene template to create |
| `lighting` | enum(Day\|Night\|Sunset\|Indoor) |  | Lighting mood (default: Day) |
| `floor_size` | number |  | Floor plane size in cm (default: 5000) |

### `get_memory_report`

Get memory usage report: system memory stats, and disk size of assets by category (Textures, StaticMeshes, Blueprints, Materials, Animations, Audio). Shows total size and top N largest assets per category.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to analyze (default: '/Game/') |
| `limit` | integer |  | Top N largest assets per category (default: 10) |

### `get_render_stats`

Get rendering performance statistics for the current viewport: estimated draw calls, triangle count from visible static meshes, light count by type, shadow caster count, and actor distribution by class.

*read-only · idempotent*

_No arguments._

### `measure_frame_times`

Measure editor frame times over N ticks as an owned operation (poll get_editor_operation). Samples per tick: game thread delta (FApp::GetDeltaTime), render thread time (GRenderThreadTime) and GPU frame time (RHIGetGPUFrameCycles). The result carries min/avg/p95/max in milliseconds per series, the sample count, provenance kind=measurement and run_metadata (engine, RHI, GPU adapter, viewport, world). Under NullRHI the GPU series is reported as unavailable rather than zero. This measures the editor as it is, including this plugin; compare runs only with matching run_metadata.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `frames` | integer |  | Ticks to sample, 1..600 (default 60) |

### `profile_actors_in_view`

Profile per-actor rendering cost for actors in the current viewport frustum. Returns actors sorted by estimated render cost: triangle count, material count, shadow casting, Nanite state, and component count. Use to identify performance bottlenecks.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `limit` | integer |  | Maximum actors to return, sorted by cost (default: 20) |
| `include_lights` | boolean |  | Include light actors (default: true) |

## Physics (10 tools)

### `add_physics_constraint`

Spawn an APhysicsConstraintActor that links two actors with a named constraint type. Fixed locks all motion, Hinge allows rotation on one axis, Prismatic allows sliding on one axis, BallSocket allows free rotation, Free allows all motion.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name_1` | string | yes | Label of the first actor to constrain |
| `actor_name_2` | string | yes | Label of the second actor to constrain |
| `constraint_type` | enum(Fixed\|Hinge\|Prismatic\|BallSocket\|Free) |  | Type of physics constraint to create |
| `label` | string |  | Label for the spawned PhysicsConstraintActor in the scene outliner |
| `x` | number |  | World X position of the constraint (default: midpoint between actors) |
| `y` | number |  | World Y position of the constraint (default: midpoint between actors) |
| `z` | number |  | World Z position of the constraint (default: midpoint between actors) |

### `assign_physics_material`

Assign a PhysicalMaterial to an actor's root PrimitiveComponent. Controls friction, bounciness, and density for physics interactions.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor |
| `material_path` | string | yes | Content path to the PhysicalMaterial asset |

### `create_physics_material`

Create a PhysicalMaterial asset with configurable friction, restitution (bounciness), and density. Use assign_physics_material to apply it to actors.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new PhysicalMaterial (e.g., '/Game/Physics/PM_Ice') |
| `friction` | number |  | Friction coefficient (default: 0.7) |
| `static_friction` | number |  | Static friction override (default: same as friction) |
| `restitution` | number |  | Bounciness 0-1 (default: 0.3) |
| `density` | number |  | Density in kg/cm^3 (default: 1.0) |

### `get_physics_info`

Get a comprehensive physics report for an actor: simulation state, mass, damping, gravity, collision profile, bounds, velocity, center of mass, and inertia tensor.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to inspect |

### `get_physics_material_info`

Read properties of a PhysicalMaterial asset: friction, static friction, restitution (bounciness), density, and surface type. Use to inspect existing physics materials before assigning them.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the PhysicalMaterial asset |

### `list_collision_channels`

List all collision channels (default engine channels + custom project channels) with their default responses. Use with set_collision_response to configure per-actor collision.

*read-only · idempotent*

_No arguments._

### `set_collision_profile`

Set the collision profile (preset) and/or collision enabled type on an actor's root PrimitiveComponent. Use profile_name for named presets like 'BlockAll' or 'PhysicsActor', or use collision_enabled for explicit control.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to configure collision on |
| `profile_name` | string |  | Named collision preset to apply (e.g. 'BlockAll', 'OverlapAll', 'NoCollision', 'Pawn', 'PhysicsActor', 'Trigger'). Takes priority over other settings when provided. |
| `collision_enabled` | enum(NoCollision\|QueryOnly\|PhysicsOnly\|QueryAndPhysics) |  | Type of collision to enable |
| `generate_overlap_events` | boolean |  | Whether the component generates overlap events when it overlaps other components |

### `set_collision_response`

Set the collision response for a specific channel on an actor's root PrimitiveComponent. Use list_collision_channels to see available channels. Common channels: WorldStatic, WorldDynamic, Pawn, PhysicsBody, Vehicle, Destructible.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor |
| `channel` | string | yes | Collision channel name (e.g., 'WorldStatic', 'Pawn', 'PhysicsBody', 'Visibility') |
| `response` | enum(Block\|Overlap\|Ignore) | yes | Collision response for this channel |

### `set_component_collision`

Set collision profile / enabled / overlap-events on a NAMED Blueprint component (works on sub-components, unlike set_collision_profile which only targets the actor root). Recompiles.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Blueprint content path |
| `component_name` | string | yes | Primitive component name (e.g. 'Trigger') |
| `profile_name` | string |  | Collision profile (e.g. 'OverlapAllDynamic', 'BlockAll', 'NoCollision') |
| `collision_enabled` | enum(NoCollision\|QueryOnly\|PhysicsOnly\|QueryAndPhysics) |  | Collision enabled mode |
| `generate_overlap_events` | boolean |  | Whether the component generates overlap events |

### `set_physics_simulation`

Enable or disable physics simulation on an actor's root PrimitiveComponent and configure physical properties: mass, damping, gravity, and per-axis translation/rotation locks.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to configure physics on |
| `simulate_physics` | boolean |  | Enable or disable physics simulation on the root primitive component |
| `enable_gravity` | boolean |  | Enable or disable gravity on the component |
| `mass_kg` | number |  | Override the mass in kilograms. Use 0 or omit to clear the override. |
| `linear_damping` | number |  | Linear damping coefficient (drag). Higher values slow linear movement faster. |
| `angular_damping` | number |  | Angular damping coefficient (rotational drag). Higher values slow rotation faster. |
| `lock_x_translation` | boolean |  | Lock movement along the world X axis |
| `lock_y_translation` | boolean |  | Lock movement along the world Y axis |
| `lock_z_translation` | boolean |  | Lock movement along the world Z axis |
| `lock_x_rotation` | boolean |  | Lock rotation around the world X axis |
| `lock_y_rotation` | boolean |  | Lock rotation around the world Y axis |
| `lock_z_rotation` | boolean |  | Lock rotation around the world Z axis |

## Python (1 tools)

### `execute_python`

Execute Python code in Unreal Engine's embedded Python environment. The 'unreal' module is available for accessing the engine API. Output is captured from the log. This is a powerful escape hatch for operations not covered by other tools. v5: runs as a script file (no literal splicing), returns ok, bounded output/errors lines and output_truncated; print MCP_RESULT:<json> to return a value; optional data object arrives as MCP_DATA.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `data` | object |  | Optional JSON object delivered to the script as MCP_DATA without any quoting |
| `code` | string | yes | Python code to execute. Has access to the full UE Python API (unreal module). |

## Search (2 tools)

### `rebuild_search_index`

Force rebuild the project search index. Call this after significant project changes (importing assets, creating Blueprints, adding actors) to ensure search results are up to date. The index auto-builds on first search if empty.

*idempotent*

_No arguments._

### `search_project`

Fuzzy search across the entire project: assets, Blueprint functions, variables, and level actors. Supports partial names, CamelCase fragments, and approximate matching. Use this when you don't know the exact name or path of something. Results are ranked by relevance score (lower = better match).

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `query` | string | yes | Search query - supports partial names, CamelCase fragments, fuzzy matching (e.g., 'player character', 'health bar', 'BP_Enemy', 'print string') |
| `category` | enum(all\|Asset\|Function\|Variable\|Actor) |  | Filter results by category (default: all) |
| `limit` | integer |  | Maximum results to return (default: 20, max: 100) |

## Sequencer (12 tools)

### `add_actor_to_sequence`

Bind an actor from the current level as a Possessable in a LevelSequence. The actor can then have tracks and keyframes added to it.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the LevelSequence asset (e.g., '/Game/Sequences/MySequence.MySequence') |
| `actor_name` | string | yes | Label of the actor in the current level to bind as a possessable |

### `add_audio_track`

Add an audio track with a sound asset to an actor binding in a LevelSequence. The actor is automatically bound if not already present. The sound is placed at the start of the playback range.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the LevelSequence asset |
| `actor_name` | string | yes | Label of the actor to bind (will be auto-bound if not already) |
| `sound_path` | string | yes | Content path to a USoundBase asset (e.g., '/Game/Audio/MySound') |

### `add_camera_cut_track`

Add a camera cut track to a LevelSequence pointing to a bound camera actor. Creates a CameraCut section spanning the specified time range. The camera actor must already be bound via add_actor_to_sequence.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the LevelSequence asset |
| `camera_actor_name` | string | yes | Label of the camera actor in the current level (must already be bound in the sequence) |
| `start_seconds` | number |  | Start time of the camera cut in seconds (default: 0) |
| `end_seconds` | number |  | End time of the camera cut in seconds (default: end of playback range) |

### `add_fade_track`

Add a cinematic fade track to a LevelSequence. Creates a Fade track (master track) with keyframes for start and end fade values. 0 = no fade (clear), 1 = fully black. Useful for fade-in/fade-out transitions in cinematics.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the LevelSequence asset |
| `start_seconds` | number |  | Fade start time in seconds (default: 0) |
| `end_seconds` | number |  | Fade end time in seconds (default: 2) |
| `start_value` | number |  | Fade value at start (0 = no fade/clear, 1 = fully black). Default: 0 |
| `end_value` | number |  | Fade value at end (0 = no fade/clear, 1 = fully black). Default: 1 |

### `add_keyframe`

Add a keyframe at a specified time on a track belonging to a bound actor. For Transform tracks, the value is parsed as 'X Y Z' or 'X Y Z Pitch Yaw Roll' in world space. For Visibility tracks, the value is 'true' (visible) or 'false' (hidden).

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the LevelSequence asset |
| `actor_name` | string | yes | Label of the bound actor |
| `track_type` | enum(Transform\|Visibility) | yes | Track type to key |
| `time_seconds` | number | yes | Time in seconds at which to place the keyframe |
| `value` | string | yes | Value to key. Transform: space-separated floats: 'X Y Z' (location only) or 'X Y Z Pitch Yaw Roll' (location + rotation). Visibility: 'true' or 'false' (visible = true means actor is shown). |

### `add_sequence_track`

Add a track to an actor binding in a LevelSequence. Transform adds a 3D transform track; Visibility adds a bool visibility track. The actor must already be bound via add_actor_to_sequence.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the LevelSequence asset |
| `actor_name` | string | yes | Label of the already-bound actor to add the track to |
| `track_type` | enum(Transform\|Visibility) | yes | Type of track to add |

### `add_sub_sequence`

Embed a child LevelSequence inside a master LevelSequence using a Sub Track. The sub-sequence is placed at the specified time range on a new or existing Sub Track.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the master LevelSequence |
| `sub_sequence_path` | string | yes | Content path of the child LevelSequence to embed |
| `start_seconds` | number |  | Start time in seconds for the sub-sequence (default: 0) |
| `end_seconds` | number |  | End time in seconds for the sub-sequence (default: end of sub-sequence playback range) |

### `create_level_sequence`

Create a new LevelSequence asset at the specified content path. Sets the display frame rate and saves the asset immediately.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new Level Sequence (e.g., '/Game/Sequences/MySequence') |
| `frame_rate` | integer |  | Display frame rate (frames per second, default: 30) |

### `get_sequence_info`

Get detailed information about a LevelSequence: display rate, playback range, bound actors, track types, and section counts. Useful for inspecting existing sequences before modifying them.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the LevelSequence asset |

### `open_sequence`

Open a LevelSequence asset in the Sequencer editor. The sequence will be focused and ready for editing.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the LevelSequence to open (e.g., '/Game/Sequences/MySequence.MySequence') |

### `play_sequence`

Open a LevelSequence in the Sequencer editor (if not already open) so it can be previewed. Playback must be triggered manually in the Sequencer UI after opening. This tool ensures the sequence is loaded and focused in the editor.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the LevelSequence asset to open and play |

### `set_sequence_range`

Set the playback range (start and end times in seconds) of a LevelSequence. Times are converted to frame numbers using the sequence's tick resolution.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the LevelSequence asset |
| `start_seconds` | number |  | Playback start time in seconds (default: 0) |
| `end_seconds` | number |  | Playback end time in seconds (default: 5) |

## SequencerAnimation (7 tools)

### `add_animation_section`

Place an animation clip on a binding's skeletal animation track, creating the track if needed. The section starts at start_seconds and, by default, runs for the animation's full length. Sections on the same row cannot overlap — pass row_index to stack a second clip on its own row so Sequencer blends between them. The animation must target the bound actor's skeleton.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the ULevelSequence |
| `animation_path` | string | yes | Content path to the UAnimSequence to place |
| `actor_name` | string |  | Label of the skeletal-mesh actor (auto-bound if needed) |
| `binding_id` | string |  | Existing binding GUID, as an alternative to actor_name |
| `start_seconds` | number |  | Where the clip starts on the sequence timeline (default: 0) |
| `row_index` | integer |  | Track row to place the section on. Use a different row to overlap and blend two clips (default: the first free row). |

### `add_animation_track`

Add a skeletal animation track to an actor in a level sequence — the track that makes a character actually animate in a cinematic, as opposed to the transform track that only moves it. The actor is bound automatically if it is not already, and must have a SkeletalMeshComponent. Idempotent: returns the existing track if the binding already has one. Add clips to it with add_animation_section.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the ULevelSequence |
| `actor_name` | string |  | Label of the skeletal-mesh actor to animate (auto-bound if needed) |
| `binding_id` | string |  | Existing binding GUID, as an alternative to actor_name |

### `bake_sequence_to_anim_sequence`

Bake one binding's animation in a level sequence down into a reusable UAnimSequence asset. This is how cinematic or Control Rig work becomes a gameplay-usable clip: everything driving that skeletal mesh in the sequence — animation sections, Control Rig, transform tracks — is evaluated per frame and written into a new animation on the actor's skeleton. Baking evaluates the whole sequence and can take a while on long shots. With create_link=true the two assets stay associated so the bake can be re-run after the sequence changes.

*long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the ULevelSequence to bake |
| `output_path` | string | yes | Content path for the new UAnimSequence (e.g. '/Game/Anims/AS_BakedIntro') |
| `actor_name` | string |  | Label of the bound actor to bake |
| `binding_id` | string |  | Binding GUID, as an alternative to actor_name |
| `export_morph_targets` | boolean |  | Bake morph target curves (default: true) |
| `export_material_curves` | boolean |  | Bake material parameter curves (default: true) |
| `record_in_world_space` | boolean |  | Bake in world space rather than component space (default: false) |
| `create_link` | boolean |  | Link the level sequence and the baked animation so the bake can be repeated (default: true) |

### `link_anim_sequence_to_sequence`

Link an existing UAnimSequence to a level sequence binding without baking now. The link records which binding and export settings produced the animation, so the bake can be re-run later (bake_sequence_to_anim_sequence) after the sequence is edited — the round trip that keeps a hand-animated Sequencer shot and its gameplay clip in step. The animation must target the bound actor's skeleton.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the ULevelSequence |
| `anim_sequence_path` | string | yes | Content path of the UAnimSequence to link |
| `actor_name` | string |  | Label of the bound actor |
| `binding_id` | string |  | Binding GUID, as an alternative to actor_name |

### `list_animation_sections`

List the animation clips on a binding's skeletal animation track: each section's animation, row, start/end in both frames and seconds, trim offsets, play rate, slot and mirror table. The section indices it returns are what set_animation_section_params and remove_animation_section take.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the ULevelSequence |
| `actor_name` | string |  | Label of the bound actor |
| `binding_id` | string |  | Binding GUID, as an alternative to actor_name |

### `remove_animation_section`

Remove an animation clip from a binding's skeletal animation track. Section indices come from list_animation_sections and shift after a removal, so re-list between removals rather than removing several by index in one pass. The track itself is kept even when its last section goes.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the ULevelSequence |
| `section_index` | integer | yes | Index of the section to remove, from list_animation_sections |
| `actor_name` | string |  | Label of the bound actor |
| `binding_id` | string |  | Binding GUID, as an alternative to actor_name |

### `set_animation_section_params`

Tune one animation section: trim its start (start_frame_offset), retime it (play_rate), route it through a montage slot (slot_name), mirror it left-to-right (mirror_data_table), or move it on the timeline (start_seconds / end_seconds). Section indices come from list_animation_sections. 5.8 note: play_rate is stored as a time-warp variant. Setting a number here makes it a constant rate; a section already carrying a time-warp curve is overwritten by that constant.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `sequence_path` | string | yes | Content path of the ULevelSequence |
| `section_index` | integer | yes | Index of the section, from list_animation_sections |
| `actor_name` | string |  | Label of the bound actor |
| `binding_id` | string |  | Binding GUID, as an alternative to actor_name |
| `start_seconds` | number |  | Move the section's start to this time on the sequence timeline |
| `end_seconds` | number |  | Move the section's end to this time (shortens or extends the clip) |
| `play_rate` | number |  | Constant playback rate; 1.0 = authored speed |
| `start_frame_offset_seconds` | number |  | Trim this many seconds off the front of the source animation |
| `slot_name` | string |  | Montage slot to play through, so the clip layers over the AnimBP rather than replacing it |
| `mirror_data_table` | string |  | Content path to a UMirrorDataTable to mirror the clip, or empty to clear |

## Snapshots (4 tools)

### `capture_editor_snapshot`

Capture an owned, expiring structural snapshot of actors in one explicit world: stable refs, paths, labels, classes, transforms, mobility, component lists and up to 32 named properties, plus selection, dirty packages, map and editor camera. targets is a list of refs/labels/paths, or the single word 'selection' or 'all' (bounded to 500 actors). Returns snapshot_id and a content hash. Structural only: no image, no asset contents; pair with capture_editor_surface for pixels. Snapshots live in memory for ttl_seconds (default 900) and are removed on session cleanup.

*read-only · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `world_id` | string |  | World path from list_worlds, 'editor' (default) or 'pie' |
| `targets` | array<string> | yes | Refs, labels or paths; or ['selection'] or ['all'] |
| `properties` | array<string> |  | Up to 32 actor property names to include |
| `ttl_seconds` | integer |  | 60..3600, default 900 |

### `compare_editor_snapshots`

Compare two owned snapshots of the same world: added and removed actors (by stable ref), and for shared actors changed transforms (with a stated tolerance), labels, classes, hidden flags, mobility, component lists and included properties; plus selection and dirty-package changes. Identical snapshots produce an empty diff. Structural by default; pass before_frame_id/after_frame_id (owned observer or retained capture frames of equal size) to add a deterministic pixel comparison (changed_pct, changed_bbox, 8x8 grid) as `visual`, or give only the frame ids for a visual-only result.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `before_id` | string |  | Earlier snapshot (optional when only frames are compared) |
| `after_id` | string |  | Later snapshot (optional when only frames are compared) |
| `transform_tolerance` | number |  | Absolute tolerance per component, 0..1000 (default 0.001) |
| `before_frame_id` | string |  | v5: earlier owned frame (observer or retained capture) for a pixel comparison |
| `after_frame_id` | string |  | v5: later owned frame; both frames must share size |
| `pixel_threshold` | integer |  | v5: per-channel difference above which a pixel counts as changed, 0..255 (default 16) |

### `get_editor_snapshot`

Return an owned snapshot's structural content (actors, selection, dirty packages, camera) with its metadata.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `snapshot_id` | string | yes | Snapshot identifier |

### `resolve_object_refs`

Resolve up to 64 queries (actor labels, object paths, names, or previously returned refs) inside one explicit world to stable references of the form actor:<world path>:<actor GUID>. Each result is resolved, missing or ambiguous with candidates; labels shared by several actors are reported as ambiguous rather than picked silently. Refs are bound to their world and to the actor's GUID, so a reloaded map, a PIE copy or a replaced actor does not resolve. Nothing is loaded or modified.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `world_id` | string |  | World path from list_worlds, 'editor' (default) or 'pie' |
| `queries` | array<string> | yes | 1..64 labels, paths, names or refs |

## SourceControl (8 tools)

### `sc_check_out`

Check out one or more files for editing. Accepts filesystem paths, /Game/... package paths, or asset object paths (e.g., '/Game/Foo/Bar.Bar'). Returns per-path status.

| Argument | Type | Required | Description |
|---|---|---|---|
| `paths` | array<string> | yes | Files to check out. Mix of filesystem and package paths is allowed. |

### `sc_diff_against_revision`

Fetch the depot copy of a file at a given revision and return its on-disk path. If revision is omitted, fetches the head revision. The caller can then read the file or hand it to an external diff tool. Binary asset diff is not attempted.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | File path (filesystem or /Game/...) to fetch. |
| `revision` | integer |  | Revision number to fetch (default: head). |

### `sc_get_history`

Return the revision history for a single file: list of {revision, author, date, description}. Up to max_entries items (default 20).

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | File path (filesystem or /Game/... package path). |
| `max_entries` | integer |  | Maximum revisions to return (default 20, max 200). |

### `sc_pending_changelist`

List locally modified, added, and deleted files in the project's content/source directories according to the active provider. Returns categorized arrays.

*read-only · idempotent*

_No arguments._

### `sc_provider_status`

Report the active source control provider. Returns {enabled, available, provider, project_path}. Always succeeds — when no provider is loaded, returns enabled=false.

*read-only · idempotent*

_No arguments._

### `sc_resolve_conflict`

Mark a conflicted file as resolved. resolve='accept_yours' keeps local; 'accept_theirs' takes depot; 'manual' simply marks the conflict resolved (assumes the agent already merged manually). Provider-specific: not all providers honor every mode.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | File path. |
| `resolve` | enum(accept_yours\|accept_theirs\|manual) | yes | Resolution mode. |

### `sc_revert`

Revert local changes on one or more files, restoring them to the depot revision. Destructive: discards uncommitted edits. Requires destructive scope.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `paths` | array<string> | yes | Files to revert. Mix of filesystem and package paths is allowed. |

### `sc_submit`

Submit (commit) one or more checked-out files to the depot with a description. Most powerful operation in this family — gated as Destructive.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `paths` | array<string> | yes | Files to submit. Must already be checked out / locally modified. |
| `description` | string | yes | Commit / changelist description. Required. |

## Spatial (10 tools)

### `align_actors`

Align/snap actors relative to each other (min/max/center on any axis) or snap them all to a grid.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_names` | array<string> | yes | Array of actor labels to align |
| `align_mode` | enum(min_x\|max_x\|center_x\|min_y\|max_y\|center_y\|min_z\|max_z\|center_z\|grid) | yes | Alignment mode |
| `grid_size` | number |  | Grid cell size for 'grid' mode (default: 100) |

### `find_placement_position`

AI-friendly tool: find a good position to place something. Tries the desired position, checks overlap, and spirals outward to find clear space if blocked. Optionally snaps to ground.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `near_x` | number | yes | Desired X position |
| `near_y` | number | yes | Desired Y position |
| `near_z` | number | yes | Desired Z position |
| `required_size_x` | number | yes | Required size X (full width of object to place) |
| `required_size_y` | number | yes | Required size Y (full depth) |
| `required_size_z` | number | yes | Required size Z (full height) |
| `on_ground` | boolean |  | Trace to ground and place on surface (default: true) |
| `min_distance_from_actors` | number |  | Minimum distance from any existing actor (default: 0) |
| `prefer_direction` | enum(any\|north\|south\|east\|west) |  | Preferred direction to search if desired position is blocked |

### `get_actor_bounds`

Get the world-space bounding box of an actor. Returns origin, extent, min/max corners, size, and center. The fundamental tool for understanding actor dimensions.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to get bounds for |

### `get_mesh_asset_bounds`

Get the bounding box of a StaticMesh ASSET (before placing it in the level). Critical for calculating how many pieces span a distance or how things fit together.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `mesh_path` | string | yes | Content path of the static mesh asset (e.g., '/Game/Meshes/SM_Wall') |

### `get_spatial_context`

High-level spatial analysis of the current scene. Returns scene bounds, actor density, ground level, nearest actors, density map by quadrant, and empty spaces. Gives the AI a bird's-eye understanding of the scene layout.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `center_x` | number |  | Center X of analysis region (default: scene center) |
| `center_y` | number |  | Center Y of analysis region |
| `center_z` | number |  | Center Z of analysis region |
| `radius` | number |  | Radius of analysis region in units (default: 5000) |

### `line_trace`

Cast a ray from A to B and report what it hits. Critical for finding ground level, checking line of sight, and placing on surfaces.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `start_x` | number | yes | Start X position |
| `start_y` | number | yes | Start Y position |
| `start_z` | number | yes | Start Z position |
| `end_x` | number | yes | End X position |
| `end_y` | number | yes | End Y position |
| `end_z` | number | yes | End Z position |
| `trace_channel` | string |  | Collision channel (default: 'Visibility'). Options: Visibility, Camera, WorldStatic, WorldDynamic, Pawn, PhysicsBody |
| `ignore_actors` | array<string> |  | Array of actor labels to ignore during the trace |

### `measure_distance`

Measure distance between two actors or two points. Returns total distance, per-axis distances, and direction vector. Critical for spacing verification.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `from_actor` | string |  | Start actor label (alternative to from_x/y/z) |
| `from_x` | number |  | Start X (alternative to from_actor) |
| `from_y` | number |  | Start Y |
| `from_z` | number |  | Start Z |
| `to_actor` | string |  | End actor label (alternative to to_x/y/z) |
| `to_x` | number |  | End X (alternative to to_actor) |
| `to_y` | number |  | End Y |
| `to_z` | number |  | End Z |

### `overlap_test`

Check if an actor overlaps with anything at its current position, or test a hypothetical box at a position. Critical for collision-free placement.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string |  | Actor to test overlap for (uses actor's current bounds). Mutually exclusive with test_x/y/z + test_extent_x/y/z. |
| `test_x` | number |  | X position of test box center (use with test_extent) |
| `test_y` | number |  | Y position of test box center |
| `test_z` | number |  | Z position of test box center |
| `test_extent_x` | number |  | Half-extent X of test box (default: 50) |
| `test_extent_y` | number |  | Half-extent Y of test box (default: 50) |
| `test_extent_z` | number |  | Half-extent Z of test box (default: 50) |
| `ignore_actors` | array<string> |  | Array of actor labels to ignore |

### `place_actor_on_ground`

Move an actor down (or up) to sit on the ground/surface below it. Traces straight down to find the surface, then positions the actor so its bottom sits on that surface.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor to place on ground |
| `offset_z` | number |  | Extra height above the ground surface (default: 0) |

### `stack_actors`

Stack actors on top of / next to each other with proper spacing based on their bounds. Critical for building walls, stacking crates, assembling modular pieces.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_names` | array<string> | yes | Array of actor labels to stack, in order |
| `direction` | enum(up\|right\|forward) | yes | Stacking direction |
| `gap` | number |  | Gap between stacked actors in units (default: 0) |

## Spline (7 tools)

### `add_spline_point`

Add a point to an existing spline at the specified local-space position. If index is omitted the point is appended at the end.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor with a SplineComponent |
| `x` | number | yes | X position of the new point (local space) |
| `y` | number | yes | Y position of the new point (local space) |
| `z` | number | yes | Z position of the new point (local space) |
| `index` | integer |  | Insert at this index. If omitted, appends to the end. |

### `create_spline_actor`

Spawn a new actor with a USplineComponent. Creates an initial spline with the specified number of points evenly spaced along the X axis.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `label` | string |  | Actor label in the scene outliner |
| `x` | number |  | X position (default: 0) |
| `y` | number |  | Y position (default: 0) |
| `z` | number |  | Z position (default: 0) |
| `num_points` | integer |  | Number of initial spline points (default: 2) |
| `point_spacing` | number |  | Distance between initial points along X axis (default: 500) |

### `get_spline_info`

Get detailed information about a spline: point count, total length, closed-loop state, and all point positions with tangents.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor with a SplineComponent |

### `remove_spline_point`

Remove a spline point by index. Remaining points are re-indexed automatically.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor with a SplineComponent |
| `index` | integer | yes | Index of the spline point to remove |

### `set_spline_closed`

Set whether the spline forms a closed loop. When closed, the last point connects back to the first.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor with a SplineComponent |
| `closed` | boolean | yes | True to close the spline loop, false to open it |

### `set_spline_point`

Modify the position and/or tangents of an existing spline point. Only provided fields are changed; omitted fields keep their current values.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor with a SplineComponent |
| `index` | integer | yes | Index of the spline point to modify |
| `x` | number |  | New X position (local space) |
| `y` | number |  | New Y position (local space) |
| `z` | number |  | New Z position (local space) |
| `arrive_tangent_x` | number |  | Arrive tangent X component |
| `arrive_tangent_y` | number |  | Arrive tangent Y component |
| `arrive_tangent_z` | number |  | Arrive tangent Z component |
| `leave_tangent_x` | number |  | Leave tangent X component |
| `leave_tangent_y` | number |  | Leave tangent Y component |
| `leave_tangent_z` | number |  | Leave tangent Z component |

### `set_spline_type`

Set the interpolation type of a spline point. Linear produces straight segments, Curve uses smooth Hermite interpolation, Constant holds the value, CurveClamped prevents overshoot.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor with a SplineComponent |
| `index` | integer | yes | Index of the spline point to modify |
| `type` | enum(Linear\|Curve\|Constant\|CurveClamped) | yes | Spline point interpolation type |

## StateTree (5 tools)

### `add_state_tree_state`

Add a new state to a StateTree asset. States can contain tasks (what to do), transitions (when to move), and child states. Uses Python bridge for compatibility. Use get_state_tree_info to inspect the tree before modifying.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the StateTree asset |
| `state_name` | string | yes | Name for the new state |
| `parent_state` | string |  | Parent state name (optional, for nested states) |
| `type` | enum(State\|Group\|Linked) |  | State type (default: State) |

### `create_state_tree`

Create a new StateTree asset. State Trees are UE5's modern replacement for Behavior Trees — more flexible, data-driven, and performant. Requires the StateTree plugin to be enabled. Uses the Python bridge for maximum compatibility.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new StateTree (e.g., '/Game/AI/ST_EnemyBehavior') |
| `schema` | enum(StateTree\|StateTreeComponent) |  | StateTree schema type (default: StateTree) |

### `get_state_tree_info`

Get information about a StateTree asset. Uses Python bridge for compatibility. Reports states, transitions, evaluators, and tasks.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the StateTree asset |

### `list_state_trees`

List all StateTree assets in the project. Returns asset name and content path.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (default: '/Game/') |
| `name_filter` | string |  | Filter by name (substring) |
| `limit` | integer |  | Maximum results (default: 50) |

### `set_state_tree_evaluator`

Configure an evaluator or condition on a StateTree state. Evaluators compute values each tick, and conditions gate transitions. Uses Python bridge. Common evaluators: StateTreeCompareIntCondition, StateTreeCompareFloatCondition, StateTreeCompareEnumCondition.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the StateTree asset |
| `state_name` | string | yes | Name of the state to configure |
| `evaluator_class` | string | yes | Class name of the evaluator (e.g., 'StateTreeCompareIntCondition') |
| `parameters_json` | string |  | JSON object with evaluator parameters (e.g., '{"Left": 5, "Right": 10}') |

## StaticMesh (7 tools)

### `configure_mesh_lod`

Configure LOD (Level of Detail) settings on a static mesh. Set the number of auto-generated LODs, screen size thresholds for each LOD transition, and triangle reduction ratio. LODs reduce rendering cost by showing simpler meshes at distance. Use get_mesh_complexity_report to see current LOD state.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the StaticMesh asset |
| `lod_count` | integer |  | Number of LODs to generate (2-8). LOD0 is the original mesh. |
| `auto_compute_lod_distances` | boolean |  | Auto-compute screen sizes for each LOD (default: true) |
| `screen_sizes_json` | string |  | JSON array of screen size thresholds per LOD, e.g. '[1.0, 0.5, 0.25, 0.1]'. LOD0=1.0 means full size. |
| `reduction_percent_per_lod` | number |  | Triangle reduction percentage per LOD level (default: 50, meaning each LOD has 50%% of previous) |

### `create_static_mesh_actor`

Convenience tool: spawn a StaticMeshActor, set its mesh, and optionally assign a material — all in one call.

| Argument | Type | Required | Description |
|---|---|---|---|
| `mesh_path` | string | yes | Content path of the static mesh asset |
| `material_path` | string |  | Content path of a material to assign (optional) |
| `x` | number |  | X position (default: 0) |
| `y` | number |  | Y position (default: 0) |
| `z` | number |  | Z position (default: 0) |
| `pitch` | number |  | Pitch rotation in degrees (default: 0) |
| `yaw` | number |  | Yaw rotation in degrees (default: 0) |
| `roll` | number |  | Roll rotation in degrees (default: 0) |
| `scale_x` | number |  | X scale (default: 1) |
| `scale_y` | number |  | Y scale (default: 1) |
| `scale_z` | number |  | Z scale (default: 1) |
| `label` | string |  | Actor label in the scene outliner |
| `folder` | string |  | Folder path in the scene outliner |

### `enable_nanite`

Enable or disable Nanite virtualized geometry on a static mesh asset. Nanite provides automatic LOD with virtually unlimited polygon counts. The mesh is saved after modification.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the StaticMesh asset |
| `enabled` | boolean | yes | True to enable Nanite, false to disable |

### `get_mesh_complexity_report`

Get detailed complexity report for a static mesh: triangle count, vertex count, LOD count, screen sizes, Nanite state, material slot count, collision complexity, and estimated VRAM usage.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path to the StaticMesh asset |

### `get_static_mesh_info`

Get detailed information about a static mesh asset: vertex/triangle count, bounds, LOD count, material slots, and collision info.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `mesh_path` | string | yes | Content path of the static mesh asset |

### `set_mesh_material_slots`

Batch-assign materials to all material slots on an actor's static mesh. Provide an array of material paths matching slot indices.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor with a StaticMeshComponent |
| `materials` | array<string> | yes | Array of material content paths, one per slot. Use empty string to skip a slot. |

### `set_static_mesh`

Set the static mesh asset on an actor's StaticMeshComponent. Works on any actor with a StaticMeshComponent.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the StaticMeshActor |
| `mesh_path` | string | yes | Content path of the static mesh asset (e.g., '/Game/StarterContent/Shapes/Shape_Cube') |

## Substrate (1 tools)

### `substrate_get_status`

Report whether the Substrate material system is enabled for the project and the key Substrate rendering settings (bytes/closures per pixel, layer support, experimental). Substrate is a project setting requiring an editor restart to change. When enabled, author materials with Substrate expression nodes (Substrate Slab/BSDF), not the legacy shading models.

*read-only · idempotent*

_No arguments._

## TestAuthoring (5 tools)

### `add_functional_test_actor`

Spawn an AFunctionalTest into the editor world at the given location. The placed actor can be configured in the Details panel and runs as part of any 'FunctionalTest' suite.

*PIE-off*

| Argument | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Editor label for the new actor. |
| `location_x` | number |  | World location X (default 0). |
| `location_y` | number |  | World location Y (default 0). |
| `location_z` | number |  | World location Z (default 0). |

### `create_automation_spec`

Scaffold a UE Automation Spec .cpp under the project's Source/ tree. The file compiles into the project on the next build / Live Coding patch. Returns the absolute path written. Use list_automation_specs / run_automation_specs once the spec is registered.

| Argument | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Spec name (PascalCase). Becomes class F<name>Spec and 'UnrealMCP.<name>' in the test catalogue. |
| `tags` | array<string> |  | Optional tags appended into the test flags (e.g., 'EditorContext'). Default: EditorContext + EngineFilter. |
| `body` | string |  | Optional custom Spec body inserted inside Describe(...) — should be raw C++ such as 'It(...)' calls. Defaults to a single passing assertion. |
| `target_dir` | string |  | Subdirectory under <Project>/Source for the file (default 'MCPGenerated/Specs'). Created if missing. |

### `get_last_test_report`

Return the most recent run_automation_specs report. Returns ran=false if no run has occurred in this editor session.

*read-only · idempotent*

_No arguments._

### `list_automation_specs`

Enumerate registered automation tests known to FAutomationTestFramework. Filter by substring on the test name.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `name_filter` | string |  | Substring filter applied to the test name (case-insensitive). |

### `run_automation_specs`

Run automation specs whose name contains the filter substring. Synchronous: blocks until the queued tests have completed (up to timeout_sec). Stores the result in the last-report buffer so get_last_test_report can fetch it again. Returns aggregate {pass, fail, duration_sec}.

*destructive · long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `filter` | string |  | Substring matched against test names. Empty matches all. |
| `timeout_sec` | integer |  | Maximum seconds to wait (default 60, max 600). |

## UIImage (2 tools)

### `generate_ui_image`

Generate an AI image using fal.ai and import it into the UE content browser as a texture. Models: flux-2-flash (fastest, cheapest, supports transparency), nano-banana-2 (concept art), flux-pro (highest quality). Supports style presets and optional background removal via birefnet/v2. Requires fal.ai API key in Project Settings and PythonScriptPlugin.

*long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `prompt` | string | yes | Image description prompt (e.g., 'sci-fi health bar icon, glowing cyan') |
| `destination_path` | string |  | Content path destination for imported texture (default: '/Game/UI/Generated/') |
| `model` | enum(flux-2-flash\|nano-banana-2\|nano-banana\|flux-dev\|flux-pro) |  | fal.ai model to use (default: flux-2-flash). flux-2-flash is fastest/cheapest with transparency support. nano-banana-2 for concept art. flux-pro for highest quality. |
| `style_preset` | enum(ui_icon\|ui_background\|ui_button\|ui_frame\|ui_portrait\|custom) |  | Style preset that appends style keywords to the prompt |
| `image_size` | enum(square\|square_hd\|landscape_4_3\|landscape_16_9\|portrait_4_3\|portrait_16_9) |  | Output image aspect ratio |
| `remove_background` | boolean |  | Remove background using fal-ai/birefnet/v2 after generation (default: false) |
| `image_name` | string |  | Override the generated texture asset name (auto-generated if omitted) |

### `remove_background`

Remove background from an image using fal-ai/birefnet/v2. Provide a publicly accessible image URL. The result transparent PNG is imported into the UE content browser. Requires fal.ai API key and PythonScriptPlugin.

*long-running*

| Argument | Type | Required | Description |
|---|---|---|---|
| `image_url` | string | yes | URL of the image to remove background from (must be publicly accessible) |
| `destination_path` | string |  | Content path destination for imported texture (default: '/Game/UI/Generated/') |
| `image_name` | string |  | Name for the output texture asset |

## Visual (9 tools)

### `capture_editor_surface`

Capture the actual pixels of one discovered editor surface (window, foreground tab content or level viewport) through a synchronous Slate render and readback, returning inline image content plus frame identity, geometry, pixel hash and freshness. Refuses hidden, minimized or background targets instead of returning another surface's pixels; refuses headless/NullRHI processes. Region is in physical pixels relative to the surface origin. Frames are not retained.

*read-only · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `surface_id` | string | yes | Surface ID from list_editor_surfaces |
| `format` | enum(png\|jpeg) |  | png (default, text/graphs) or jpeg (viewports) |
| `max_long_edge` | integer |  | Downscale so the long edge is at most this many pixels: 64..4096, default 1280; never upscales |
| `jpeg_quality` | integer |  | 1..100, default 85 |
| `max_bytes` | integer |  | Encoded byte cap: 16384..8388608, default 1048576; exceeding it fails explicitly |
| `region` | object |  | Optional crop in physical pixels relative to the surface origin; keeps busy editor chrome out of change detection |
| `after_operation_id` | string |  | v5: owned operation the frame must follow; fresh only after the operation finished and Slate painted at least once since |
| `wait_policy` | enum(none\|next_paint\|stable) |  | v5: none (default) \| next_paint (require a paint after the operation) \| stable (require the same pixels as the previous capture of this surface) |
| `allow_stale` | boolean |  | v5: return a frame marked stale/changing instead of not_ready (default false) |
| `retain` | boolean |  | v5: keep the frame for 10 minutes as unreal://visual/frames/{frame_id} for comparison and re-reading (default false; 8 per session) |

### `focus_editor_surface`

Explicitly reveal a surface. Pass surface_id to activate a background tab, restore a minimized window and bring its window to the front; or pass asset_path to open (or focus) that asset's editor and return the tabs it owns. This is a UI effect requiring Scene scope; it never closes, saves or edits content. Refuses when expected_generation no longer matches. Verify with list_editor_surfaces or capture_editor_surface afterwards.

*idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `surface_id` | string |  | Surface ID from list_editor_surfaces (or use asset_path) |
| `asset_path` | string |  | Loaded or loadable asset path, e.g. /Game/UI/WBP_HUD; opens its asset editor |
| `expected_generation` | integer |  | Refuse if the surface generation changed since discovery |
| `restore_minimized` | boolean |  | Restore a minimized window (default true) |

### `get_editor_frame`

Return one retained observer frame by frame_id for the owning session, inline. Frames disappear when evicted by history limits, lease expiry, stop or session cleanup.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `frame_id` | string | yes | Frame ID from an observation result |

### `get_editor_observation`

Poll an owned observer. Returns the newest retained frame inline when its sequence is above after_sequence, otherwise no_change with counters (samples, unchanged, dropped, failures) and the last error. Never blocks the editor; use next_poll_ms as the polling hint. Expired or invalidated observers report their terminal state and hold no frames.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `observer_id` | string | yes | Observer ID from start_editor_observation |
| `after_sequence` | integer |  | Return a frame only if newer than this sequence (default 0) |
| `include_history` | boolean |  | Include metadata for all retained frames (default false) |
| `wait_ms` | integer |  | v5: bounded wait budget 0..30000 the client is willing to spend; the server never blocks the editor and answers with retry_after_ms = min(wait_ms, next_poll_ms) when no newer frame exists |

### `list_editor_surfaces`

Discover editor-owned windows, modal/overlay windows, dock tabs and level viewports with stable surface IDs, generation, geometry, visibility and honest capture availability. Never activates or reveals UI. Surface IDs are invalidated when their widget or window closes; generation advances when a descriptor changes. Headless/NullRHI processes report no visual backend.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `kind` | enum(any\|window\|modal\|overlay\|tab\|level_viewport) |  | Filter by surface kind (default any) |
| `title_contains` | string |  | Case-insensitive substring filter on title/label |
| `offset` | integer |  | Skip this many matches (default 0) |
| `limit` | integer |  | Maximum surfaces to return: 1..500, default 100 |

### `list_visual_verifications`

List this session's visual verification records, newest first, bounded by limit (1..64, default 20).

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `limit` | integer |  | 1..64, default 20 |

### `record_visual_verification`

Record the outcome of a visual check separately from capture success. outcome is pass, fail or inconclusive; method is assistant (a vision judgment) or deterministic (a validator/assertion). Reference the frame IDs inspected; the record notes whether each frame was still retained. Records are owned by the session, bounded (64 per session), and never imply editor content changed. Use inconclusive when frames were stale, blocked or unreadable.

*read-only · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `outcome` | enum(pass\|fail\|inconclusive) | yes | pass \| fail \| inconclusive |
| `method` | enum(assistant\|deterministic) | yes | assistant \| deterministic |
| `criteria` | string | yes | What was checked, e.g. 'no text overlaps the ammo panel' |
| `frame_ids` | array<string> | yes | 1..8 frame IDs that were inspected |
| `surface_id` | string |  | Surface the frames came from |
| `notes` | string |  | Free-form evidence notes, at most 2000 characters |

### `start_editor_observation`

Start an owned, leased periodic observer of one discovered surface. The server samples on the editor tick at the negotiated rate (0.2..2 Hz), skips encoding when pixels are unchanged, retains at most `history` frames under a global byte budget, and adapts the rate when capture is slow. Poll with get_editor_observation; clients never receive unsolicited frames. Requires Read scope only, plus observation-resource limits: 2 observers per session, 8 per editor.

*read-only · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `surface_id` | string | yes | Surface ID from list_editor_surfaces |
| `rate_hz` | number |  | Requested sampling rate 0.2..2 (default 1) |
| `lease_seconds` | integer |  | Lease 30..600 seconds (default 300); expiry releases all frames |
| `history` | integer |  | Retained frames 1..3 (default 1) |
| `format` | enum(png\|jpeg) |  | png (default) or jpeg |
| `max_long_edge` | integer |  | 64..4096, default 1280 |
| `max_bytes` | integer |  | Per-frame encoded cap 16384..8388608, default 1048576 |
| `region` | object |  | Optional crop in physical pixels relative to the surface origin; keeps busy editor chrome out of change detection |
| `mode` | enum(periodic\|on_change\|after_operation) |  | v5: periodic (default: sample at rate, skip identical pixels) \| on_change (sample only when Slate painted since the last sample, coalesced to the rate) \| after_operation (wait for after_operation_id to finish and paint, capture once tagged with the operation, then continue periodically) |
| `after_operation_id` | string |  | v5: owned operation for mode=after_operation |

### `stop_editor_observation`

Stop an owned observer and release its frames and ticker work. Idempotent: stopping an already released observer succeeds with state unknown. Returns the final counters and last frame metadata.

*read-only · idempotent · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `observer_id` | string | yes | Observer ID |

## Widget (41 tools)

### `add_widget`

Add a widget to a Widget Blueprint's widget tree. Supports all standard UMG widgets by type name (any concrete UWidget subclass resolves by reflection: ListView, WidgetSwitcher, NamedSlot, ...), or an instance of another Widget Blueprint via widget_class_path (composition, e.g. put WBP_HealthBar inside WBP_HUD). Returns the created widget's name.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_type` | string |  | Native widget type to add (CanvasPanel, VerticalBox, HorizontalBox, GridPanel, Overlay, SizeBox, ScaleBox, Border, WrapBox, UniformGridPanel, ScrollBox, Button, TextBlock, Image, EditableTextBox, Slider, ProgressBar, CheckBox, ComboBoxString, Spacer, RichTextBlock, WidgetSwitcher, ListView, NamedSlot, ... any UWidget subclass name). Ignored when widget_class_path is set. |
| `widget_class_path` | string |  | Widget Blueprint asset path to instance instead of a native type (e.g. '/Game/UI/WBP_HealthBar') |
| `widget_name` | string |  | Custom name for the widget (auto-generated if omitted) |
| `parent_widget_name` | string |  | Name of parent widget. If omitted, adds to root widget (or becomes the root if the tree is empty). |
| `index` | integer |  | Insertion index among siblings (appends to end if omitted) |
| `is_variable` | boolean |  | Expose the widget as a Blueprint variable ('Is Variable' checkbox in the designer). Engine default: true for leaf widgets, false for layout panels. |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true). Pass false when batching many edits, then call compile_widget_blueprint. |

### `add_widget_animation_key`

Add a keyframe at time_seconds to an existing track. Opacity/Float: value. Transform: translation_x/y, rotation, scale_x/y, shear_x/y (only provided channels get keys). Color: color_r/g/b/a. Visibility: visibility enum. Margin: left/top/right/bottom. Bool: bool_value. The section and playback range grow to include the key.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `animation_name` | string | yes | Animation to edit |
| `widget_name` | string | yes | Widget bound in the animation, or 'Self' |
| `track_type` | enum(Opacity\|Transform\|Color\|Visibility\|Margin\|Float\|Bool) | yes | Track to key (same as add_widget_animation_track) |
| `target` | enum(widget\|slot) |  | widget (default) or slot |
| `property_name` | string |  | Property for Float/Bool/Color/Margin tracks (must match the track) |
| `time_seconds` | number | yes | Key time in seconds |
| `interpolation` | enum(Auto\|Linear\|Constant) |  | Key interpolation for float channels (default Auto/cubic) |
| `value` | number |  | Float value (Opacity / Float) |
| `translation_x` | number |  | Transform: translation X |
| `translation_y` | number |  | Transform: translation Y |
| `rotation` | number |  | Transform: rotation angle in degrees |
| `scale_x` | number |  | Transform: scale X |
| `scale_y` | number |  | Transform: scale Y |
| `shear_x` | number |  | Transform: shear X |
| `shear_y` | number |  | Transform: shear Y |
| `color_r` | number |  | Color: red |
| `color_g` | number |  | Color: green |
| `color_b` | number |  | Color: blue |
| `color_a` | number |  | Color: alpha |
| `visibility` | enum(Visible\|Collapsed\|Hidden\|HitTestInvisible\|SelfHitTestInvisible) |  | Visibility track value |
| `left` | number |  | Margin: left |
| `top` | number |  | Margin: top |
| `right` | number |  | Margin: right |
| `bottom` | number |  | Margin: bottom |
| `bool_value` | boolean |  | Bool track value |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `add_widget_animation_track`

Add a property track for a widget (or its slot, or the user widget itself with widget_name='Self') to a widget animation. track_type: Opacity (RenderOpacity), Transform (RenderTransform: translation/rotation/scale/shear), Color (ColorAndOpacity or property_name), Visibility (ESlateVisibility), Margin (slot 'LayoutData.Offsets' or Padding), Float / Bool (any property via property_name). Creates the binding and an empty section spanning the playback range; then add keys.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `animation_name` | string | yes | Animation to edit |
| `widget_name` | string | yes | Widget to animate, or 'Self' for the user widget |
| `track_type` | enum(Opacity\|Transform\|Color\|Visibility\|Margin\|Float\|Bool) | yes | Kind of track |
| `target` | enum(widget\|slot) |  | Animate the widget (default) or its slot (for Margin / canvas offsets) |
| `property_name` | string |  | Property (or dotted path) for Float/Bool/Color/Margin tracks, e.g. 'RenderOpacity', 'ColorAndOpacity', 'LayoutData.Offsets' |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `apply_widget_patch`

Typed widget patch with preflight. operations[] are add, remove, rename, reparent, set_property and set_slot_property (values in Unreal text syntax). mode=preview (default, or dry_run=true) simulates the whole patch against the current tree, resolves names, parents and property paths, and returns applicability, problems and the tree revision without changing anything. mode=apply re-runs the preflight, refuses if expected_revision no longer matches, applies in order under the registry undo step, reads each change back, and reports revision_after; a mid-patch failure lists surviving operations in results (atomic=false) and recovery=not_attempted. save_policy defaults to leave_dirty. Requires Scene scope; compile separately with compile_widget_blueprint.

*PIE-off · preview · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `mode` | enum(preview\|apply) |  | preview (default) or apply |
| `expected_revision` | string |  | Tree revision returned by a previous preview; apply refuses on mismatch |
| `save_policy` | enum(leave_dirty\|save) |  | leave_dirty (default) or save |
| `operations` | array<object> | yes | Ordered typed operations (1..64) |

### `batch_add_widgets`

Add multiple widgets to a Widget Blueprint in one call. Provide a JSON array of widget definitions, each with 'type' (required), 'name' (optional), 'parent' (optional, defaults to root), and 'is_variable' (optional bool, exposes the widget as a Blueprint variable). Much faster than individual add_widget calls for building complex UIs.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widgets_json` | string | yes | JSON array of widget definitions: [{"type":"TextBlock","name":"Txt_Title","parent":"RootPanel","is_variable":true}, {"class_path":"/Game/UI/WBP_HealthBar","name":"HealthBar","parent":"RootPanel"} ...] |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `batch_set_widget_properties`

Set properties on multiple widgets in one call. Each operation specifies a widget name and the properties to set (same as set_widget_properties: text, font_size, visibility, render_opacity, is_enabled, is_variable, tooltip_text, color_r/g/b/a, percent, width_override, height_override, justification). Much faster than individual calls.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `operations_json` | string | yes | JSON array of operations: [{"widget":"Txt_Title","text":"Hello","font_size":24}, {"widget":"Img_Icon","render_opacity":0.5}, {"widget":"Btn","property":"WidgetStyle.Normal.TintColor.SpecifiedColor","value":"(R=1,G=0,B=0,A=1)"}] |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `bind_widget_event`

Create an event binding for a widget in a Widget Blueprint's event graph (the details-panel '+' button): adds a ComponentBoundEvent node for the widget's delegate (Button OnClicked, Slider OnValueChanged, ListView OnItemClicked, ...). The widget is promoted to a Blueprint variable if needed. If function_name is given, the event's exec pin is wired to a call of that function; if no such function exists a custom event with that name is created and called. Idempotent: an existing binding is reused.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget to bind event on (e.g., 'Btn_Start') |
| `event_name` | string | yes | Multicast delegate on the widget class: OnClicked, OnPressed, OnReleased, OnHovered, OnUnhovered, OnValueChanged, OnCheckStateChanged, OnTextChanged, OnTextCommitted, OnSelectionChanged, OnItemClicked, ... (any delegate property name; the error lists what is available) |
| `function_name` | string |  | Blueprint function or custom event to call when the event fires. Created as a custom event if it does not exist. |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `bind_widget_property`

Create a property binding (the designer's 'Bind' dropdown) so a widget property such as Text, Percent, Visibility, ColorAndOpacity or Brush is driven every frame by a Blueprint function (function_name) or a member variable (variable_name) of matching type. Editor-only data with no Python surface. Note: polled bindings cost game-thread time; prefer direct setters for shipping UI.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget |
| `property_name` | string | yes | Bindable property (one that has a matching '<Name>Delegate' on the widget class): Text, Percent, Visibility, ColorAndOpacity, Brush, IsEnabled, ToolTipText, ... |
| `function_name` | string |  | Pure Blueprint function on this Widget Blueprint returning the property type (create with add_function_graph + add_function_return_node first) |
| `variable_name` | string |  | Member variable on this Widget Blueprint of the property type (alternative to function_name) |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `compile_widget_blueprint`

Compile a Widget Blueprint and report errors/warnings plus the widget and animation member variables that exist on the generated class - use it to verify is_variable changes, bindings and reparenting. Optionally saves.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `save` | boolean |  | Save the asset to disk after a successful compile (default false) |

### `create_widget_animation`

Create a widget animation (UMG Animations tab '+ Animation') in a Widget Blueprint. The animation becomes a member variable (playable with PlayAnimation). Add tracks with add_widget_animation_track, keys with add_widget_animation_key.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `animation_name` | string | yes | Name of the animation (unique among widgets, variables and animations), e.g. 'FadeIn' |
| `length_seconds` | number |  | Initial playback length in seconds (default 1.0; keys beyond it extend the range) |
| `frame_rate` | number |  | Display frame rate (default 20, the UMG default) |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `create_widget_blueprint`

Create a new Widget Blueprint (UMG) asset with a specified root panel type. The Widget Blueprint is saved and ready for editing with add_widget, set_widget_properties, etc.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path for the new Widget Blueprint (e.g., '/Game/UI/WBP_MainMenu') |
| `root_widget_type` | enum(CanvasPanel\|VerticalBox\|HorizontalBox\|Overlay\|GridPanel) |  | Root panel widget type (default: CanvasPanel) |
| `parent_class` | string |  | Parent UserWidget class: '/Script/Game.MyHUDBase', a short native class name ('CommonActivatableWidget'), or another Widget Blueprint asset path. Default: UserWidget. |

### `duplicate_widget`

Duplicate a widget and all of its children (designer copy/paste). The copy is inserted next to the original (or under target_parent_name) with unique names; property values and slot layout are preserved.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget to duplicate |
| `new_name` | string |  | Optional name for the top-level copy (children get auto-unique names) |
| `target_parent_name` | string |  | Panel to receive the copy (default: the original's parent) |
| `index` | integer |  | Insertion index in the target parent (default: right after the original, or at the end) |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `get_widget_properties`

Inspect a widget's current properties including type, visibility, opacity, and type-specific properties (text, colors, sizes). Also reports slot/layout information from the parent container.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget to inspect |

### `get_widget_property`

Read any property on a widget (or on its slot with target='slot') by name or dotted path, exported in Unreal text syntax.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget |
| `property_name` | string | yes | Property name or dotted path |
| `target` | enum(widget\|slot) |  | Read from the widget (default) or its slot |

### `get_widget_tree`

Read the widget hierarchy of a Widget Blueprint. Returns a JSON tree showing all widgets, their types, names, children, and optionally their properties and slot configuration.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `include_properties` | boolean |  | Include basic properties like text, color, anchors (default: false) |

### `list_widget_animations`

List the animations of a Widget Blueprint with their length, bindings, tracks and key counts.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |

### `list_widget_bindings`

List the property bindings (designer 'Bind' dropdown) and bound event nodes in a Widget Blueprint.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string |  | Optional: only bindings on this widget |

### `list_widget_blueprints`

List all Widget Blueprint (UMG) assets in the project. Returns asset name, content path, and parent class for each widget.

*read-only*

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string |  | Content path to search (e.g., '/Game/'). Default: '/Game/' |
| `name_filter` | string |  | Filter by asset name (substring match) |
| `limit` | integer |  | Maximum number of results (default: 100) |

### `list_widget_properties`

List the reflected properties of a widget (or its slot) with type, category, editability and current value, so property names can be discovered without engine headers. Use with set_widget_property.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget |
| `target` | enum(widget\|slot) |  | List the widget's properties (default) or its slot's |
| `filter` | string |  | Optional substring filter on property name / category |
| `include_inherited` | boolean |  | Include properties from parent classes (default true) |

### `move_widget`

Move a widget to a different parent in the widget tree, or reorder within the same parent (index). Uses the designer's move logic, so cycles are refused and slot data is rebuilt for the new parent type.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget to move |
| `new_parent_name` | string |  | Name of the new parent widget (must be a panel). Omit to reorder within the current parent. |
| `index` | integer |  | Position among the parent's children (appends to end if omitted) |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `pie_add_widget_to_viewport`

Create an instance of a Widget Blueprint in the running Play-In-Editor session and add it to the viewport (owned by the first player controller). Returns a handle for pie_remove_widget. Use pie_screenshot afterwards to see it. Requires an active PIE session (pie_start).

| Argument | Type | Required | Description |
|---|---|---|---|
| `widget_class_path` | string | yes | Widget Blueprint asset path (e.g. '/Game/UI/WBP_HUD') |
| `z_order` | integer |  | Viewport Z order (default 0) |
| `show_mouse_cursor` | boolean |  | Show the mouse cursor and switch to Game+UI input mode (default false) |

### `pie_list_widgets`

List widgets added through pie_add_widget_to_viewport that are still alive, and all UserWidgets currently in the PIE viewport.

*read-only*

_No arguments._

### `pie_remove_widget`

Remove a widget added with pie_add_widget_to_viewport (by handle, or 'all').

| Argument | Type | Required | Description |
|---|---|---|---|
| `handle` | string | yes | Handle returned by pie_add_widget_to_viewport, or 'all' |

### `remove_widget`

Remove a widget from a Widget Blueprint's widget tree by name. Also removes all children of that widget.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget to remove |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `remove_widget_animation`

Delete a widget animation from a Widget Blueprint.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `animation_name` | string | yes | Animation to delete |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `remove_widget_binding`

Remove a property binding from a widget (property_name), or a bound event node (event_name) from the graph.

*destructive*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget |
| `property_name` | string |  | Bound property to unbind (e.g. Text) |
| `event_name` | string |  | Bound event to remove (e.g. OnClicked) |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `rename_widget`

Rename a widget in a Widget Blueprint the way the designer does: the member variable, event nodes, variable get/set nodes, property bindings, animation bindings and navigation rules are all retargeted. (Renaming the UObject from Python breaks those references.)

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Current name of the widget |
| `new_name` | string | yes | New name (must be unique in the Blueprint and a valid identifier) |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `reparent_widget_blueprint`

Change the parent class of a Widget Blueprint (File > Reparent Blueprint), e.g. to a C++ base with BindWidget members, or to CommonActivatableWidget. Nodes are refreshed and the Blueprint recompiled; compile messages are returned so a broken reparent is visible. Python has no reparent API.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `parent_class` | string | yes | New parent: '/Script/Game.MyHUDBase', a short native class name, or another Widget Blueprint asset path |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `replace_widget`

Replace a widget with a new widget of a different type, keeping its name, position and (where the classes are compatible) its children and property values. Graph references to compatible members are retargeted. Pass replace_with_child=true instead to remove a single-child panel and promote its child.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget to replace |
| `new_type` | string |  | Replacement widget type (e.g. 'Border' for a SizeBox, 'RichTextBlock' for a TextBlock) |
| `replace_with_child` | boolean |  | Remove this single-child panel and put its child in its place |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `set_named_slot_content`

Put a widget into a named slot exposed by a Widget Blueprint instance placed in this tree (a child WBP that contains NamedSlot widgets). A plain NamedSlot widget in this tree is a panel: use add_widget with parent_widget_name instead. Either create the content (widget_type / widget_class_path) or move an existing widget (content_widget_name). Call with no content arguments to list the host's slots.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `host_widget_name` | string | yes | Widget that owns the named slot(s) |
| `slot_name` | string |  | Named slot to fill (omit to just list slots; for a NamedSlot widget this is the widget's own name) |
| `widget_type` | string |  | Native widget type to create as content |
| `widget_class_path` | string |  | Widget Blueprint asset to instance as content |
| `widget_name` | string |  | Name for newly created content |
| `content_widget_name` | string |  | Existing widget to move into the slot |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `set_widget_blueprint_defaults`

Set class-default properties of the Widget Blueprint itself (its UserWidget CDO): e.g. 'bIsFocusable'='true', 'Priority'='10', 'TickFrequency'='Never', plus the designer preview size (design_size_mode / design_width / design_height). Only provided fields change.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `property_name` | string |  | CDO property name or dotted path |
| `value` | string |  | Value in Unreal text-import syntax |
| `design_size_mode` | enum(FillScreen\|Custom\|CustomOnScreen\|Desired\|DesiredOnScreen) |  | Designer preview size mode |
| `design_width` | number |  | Designer preview width (Custom modes) |
| `design_height` | number |  | Designer preview height (Custom modes) |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `set_widget_component_property`

Set one or more properties on the first WidgetComponent found on an actor. Only provided fields are modified. Supports draw size, widget class, render space, tint color, interaction distance, and two-sided rendering.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the actor that owns the WidgetComponent |
| `draw_size_x` | number |  | New draw width in world units |
| `draw_size_y` | number |  | New draw height in world units |
| `widget_class_path` | string |  | Content path to a Widget Blueprint to assign as the widget class (e.g., '/Game/UI/WBP_HUD.WBP_HUD') |
| `space` | enum(World\|Screen) |  | Render space: 'World' for 3D world space, 'Screen' for screen-space overlay |
| `tint_r` | number |  | Tint color red channel (0.0 - 1.0) |
| `tint_g` | number |  | Tint color green channel (0.0 - 1.0) |
| `tint_b` | number |  | Tint color blue channel (0.0 - 1.0) |
| `tint_a` | number |  | Tint color alpha channel (0.0 - 1.0) |
| `max_interaction_distance` | number |  | Maximum distance at which the player can interact with this widget (in world units) |
| `is_two_sided` | boolean |  | Whether the widget geometry is rendered two-sided |

### `set_widget_designer_flags`

Set the designer-only flags of a widget (the eye / lock icons and tree expansion in the UMG hierarchy). These are plain UPROPERTY() fields that Python cannot set. Only provided fields change.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget |
| `hidden_in_designer` | boolean |  | Hide in the designer preview (eye icon) |
| `locked_in_designer` | boolean |  | Lock against designer selection/drag (lock icon) |
| `expanded_in_designer` | boolean |  | Expanded in the hierarchy tree |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `set_widget_image`

Set a texture on a UImage widget in a Widget Blueprint. Loads a Texture2D asset and assigns it as the image brush. Optionally set tint color and display size.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the UImage widget |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |
| `texture_path` | string | yes | Content path to a Texture2D asset (e.g., '/Game/Textures/T_MyIcon') |
| `tint_r` | number |  | Tint red (0-1) |
| `tint_g` | number |  | Tint green (0-1) |
| `tint_b` | number |  | Tint blue (0-1) |
| `tint_a` | number |  | Tint alpha (0-1) |
| `size_x` | number |  | Override image display width |
| `size_y` | number |  | Override image display height |

### `set_widget_properties`

Set properties on any widget in a Widget Blueprint. Supports common properties (visibility, opacity, tooltip) and type-specific properties (text/font for TextBlock, color for TextBlock/Button/Image, percent for ProgressBar, size overrides for SizeBox, padding for Border). Only provided fields are modified.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget to modify |
| `visibility` | enum(Visible\|Collapsed\|Hidden\|HitTestInvisible\|SelfHitTestInvisible) |  | Widget visibility |
| `is_enabled` | boolean |  | Whether the widget is interactive |
| `is_variable` | boolean |  | Expose the widget as a Blueprint variable ('Is Variable' checkbox). Required for BindWidget / graph access. Triggers a structural recompile. |
| `tooltip_text` | string |  | Tooltip text |
| `text_namespace` | string |  | Localization namespace for text/tooltip_text (with text_key, makes the text localizable instead of a culture-invariant literal) |
| `text_key` | string |  | Localization key for text/tooltip_text |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |
| `render_opacity` | number |  | Render opacity (0.0 - 1.0) |
| `text` | string |  | Text content (for TextBlock/RichTextBlock) |
| `font_size` | integer |  | Font size in points (for TextBlock) |
| `justification` | enum(Left\|Center\|Right) |  | Text justification (for TextBlock) |
| `color_r` | number |  | Color red channel (0.0 - 1.0) |
| `color_g` | number |  | Color green channel (0.0 - 1.0) |
| `color_b` | number |  | Color blue channel (0.0 - 1.0) |
| `color_a` | number |  | Color alpha channel (0.0 - 1.0) |
| `percent` | number |  | Progress bar fill percent (0.0 - 1.0) |
| `width_override` | number |  | Width override for SizeBox (0 to clear) |
| `height_override` | number |  | Height override for SizeBox (0 to clear) |
| `padding_left` | number |  | Left padding |
| `padding_top` | number |  | Top padding |
| `padding_right` | number |  | Right padding |
| `padding_bottom` | number |  | Bottom padding |
| `brush_size_x` | number |  | Image brush width |
| `brush_size_y` | number |  | Image brush height |

### `set_widget_property`

Set ANY property on a widget by name using reflection, including nested paths ('Font.Size', 'WidgetStyle.Normal.TintColor', 'ColorAndOpacity.SpecifiedColor') and properties the Python API refuses (bIsVariable, designer flags). Values use Unreal text syntax: numbers, true/false, enum names ('Collapsed'), structs '(R=1,G=0,B=0,A=1)' / '(X=10,Y=20)', asset paths for object refs. Replaces most execute_python fallbacks for UMG.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget |
| `property_name` | string | yes | Property name or dotted path (case-insensitive; 'IsVariable' resolves to 'bIsVariable') |
| `value` | string | yes | Value in Unreal text-import syntax |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `set_widget_slot`

Configure a widget's slot properties (positioning within its parent container). CanvasPanel: anchors, offsets, alignment, z-order, auto_size. VerticalBox/HorizontalBox: size rule (Auto/Fill), fill weight, alignment, padding. Overlay/ScrollBox/SizeBox/Border/ScaleBox/WidgetSwitcher/StackBox: alignment, padding. GridPanel: row/column/spans/layer + alignment/padding. UniformGrid: row/column + alignment. WrapBox: alignment, padding, fill_empty_space. Only provided fields are modified. For anything else use set_widget_slot_property.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget to configure slot for |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |
| `row` | integer |  | Row (GridPanel / UniformGridPanel slot) |
| `column` | integer |  | Column (GridPanel / UniformGridPanel slot) |
| `row_span` | integer |  | Row span (GridPanel slot) |
| `column_span` | integer |  | Column span (GridPanel slot) |
| `layer` | integer |  | Layer (GridPanel slot) |
| `fill_empty_space` | boolean |  | Fill remaining line space (WrapBox slot) |
| `fill_span_when_less_than` | number |  | Fill the line when remaining space is less than this (WrapBox slot) |
| `anchor_min_x` | number |  | Anchor minimum X (0-1). CanvasPanel slot. |
| `anchor_min_y` | number |  | Anchor minimum Y (0-1). CanvasPanel slot. |
| `anchor_max_x` | number |  | Anchor maximum X (0-1). CanvasPanel slot. |
| `anchor_max_y` | number |  | Anchor maximum Y (0-1). CanvasPanel slot. |
| `offset_left` | number |  | Left offset in pixels. CanvasPanel slot. |
| `offset_top` | number |  | Top offset in pixels. CanvasPanel slot. |
| `offset_right` | number |  | Right offset/width. CanvasPanel slot. |
| `offset_bottom` | number |  | Bottom offset/height. CanvasPanel slot. |
| `alignment_x` | number |  | Alignment X (0-1). CanvasPanel slot. |
| `alignment_y` | number |  | Alignment Y (0-1). CanvasPanel slot. |
| `auto_size` | boolean |  | Auto size to content. CanvasPanel slot. |
| `z_order` | integer |  | Z-order for rendering. CanvasPanel slot. |
| `size_rule` | enum(Auto\|Fill) |  | Size rule for box slots |
| `fill_weight` | number |  | Fill weight when size_rule is Fill (default: 1.0) |
| `halign` | enum(Fill\|Left\|Center\|Right) |  | Horizontal alignment |
| `valign` | enum(Fill\|Top\|Center\|Bottom) |  | Vertical alignment |
| `padding_left` | number |  | Left padding |
| `padding_top` | number |  | Top padding |
| `padding_right` | number |  | Right padding |
| `padding_bottom` | number |  | Bottom padding |

### `set_widget_slot_property`

Set any property on a widget's slot by name (fallback for slot types / fields not covered by set_widget_slot), e.g. 'LayoutData.Offsets' = '(Left=0,Top=0,Right=200,Bottom=50)', 'Size.SizeRule' = 'Fill', 'ZOrder' = '3'.

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget whose slot to modify |
| `property_name` | string | yes | Slot property name or dotted path |
| `value` | string | yes | Value in Unreal text-import syntax |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

### `spawn_widget_component`

Add a WidgetComponent to an existing actor for in-world UI display. The component can render a Widget Blueprint in 3D world space or screen space. Returns the component name on success.

| Argument | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Label of the target actor to add the WidgetComponent to |
| `widget_class_path` | string |  | Optional content path to a Widget Blueprint asset (e.g., '/Game/UI/WBP_HUD.WBP_HUD') |
| `draw_size_x` | number |  | Width of the widget in world units (default: 500) |
| `draw_size_y` | number |  | Height of the widget in world units (default: 500) |
| `relative_x` | number |  | X offset from actor root (default: 0) |
| `relative_y` | number |  | Y offset from actor root (default: 0) |
| `relative_z` | number |  | Z offset from actor root (default: 0) |
| `space` | enum(World\|Screen) |  | Render space for the widget. 'World' renders in 3D world space, 'Screen' renders as a screen-space overlay. Default: 'World' |

### `validate_widget_layout`

Deterministically lay out a compiled Widget Blueprint at an explicit viewport size and report measurable layout issues: leaf widgets overlapping outside an Overlay (overlap), leaf widgets outside the viewport (out_of_bounds), visible leaves with zero area (zero_size), widgets whose desired size exceeds the space they were given (clipped) and leaves extending outside an ancestor's bounds (overflow). Uses Slate prepass and arrangement on a transient instance; it never renders, edits or saves. Results are validator facts, suitable for record_visual_verification with method=deterministic. Requires a compiled generated class and an idle (non-PIE) editor.

*read-only · idempotent · PIE-off · structured result*

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `viewport_width` | integer |  | Layout width in pixels, 64..8192 (default 1280) |
| `viewport_height` | integer |  | Layout height in pixels, 64..8192 (default 720) |
| `overlap_tolerance_px` | number |  | Overlaps thinner than this in both axes are ignored (default 1) |
| `include_rects` | boolean |  | Return the rect of every placed widget (default false) |

### `wrap_widget`

Wrap an existing widget in a new panel widget (designer 'Wrap With...'): e.g. put a TextBlock inside a new SizeBox or Border after the fact. The wrapper takes the widget's place in its parent; works on the root widget too.

| Argument | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Content path of the Widget Blueprint |
| `widget_name` | string | yes | Name of the widget to wrap |
| `wrapper_type` | string | yes | Panel widget type for the wrapper (SizeBox, Border, Overlay, VerticalBox, HorizontalBox, CanvasPanel, ScaleBox, ScrollBox, ...) |
| `wrapper_name` | string |  | Optional name for the new wrapper widget |
| `save` | boolean |  | Compile and save the asset to disk after the change (default true) |

## WorldPartition (2 tools)

### `get_world_partition_info`

Get World Partition configuration and status for the current level. v5: accepts world_id, reports loaded/unloaded actor descriptor counts by grid and class, and can list descriptors bounded by limit/offset with loaded/class filters (merges the planned inspect_world_partition). Reports whether World Partition is enabled, data layers, and world bounds. Works with both World Partition and non-World Partition levels.

*read-only · idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `world_id` | string |  | v5: world path from list_worlds (default: current editor world) |
| `include_descriptors` | boolean |  | v5: list actor descriptors (bounded by limit/offset) with loaded state, grid, class, label, bounds and data layers |
| `loaded` | enum(any\|loaded\|unloaded) |  | v5: descriptor filter: any (default), loaded, unloaded |
| `class_filter` | string |  | v5: descriptor native class name substring filter |
| `limit` | integer |  | v5: descriptors to return, 1..500 (default 100) |
| `offset` | integer |  | v5: descriptors to skip (default 0) |

### `load_world_partition_region`

Load World Partition editor cells within a bounding box region. Uses the console command 'wp.Editor.LoadRegion' to trigger cell loading. Requires World Partition to be enabled on the current level. Coordinates are in Unreal units (cm).

*idempotent*

| Argument | Type | Required | Description |
|---|---|---|---|
| `min_x` | number | yes | Minimum X coordinate of the region to load (Unreal units / cm) |
| `min_y` | number | yes | Minimum Y coordinate of the region to load (Unreal units / cm) |
| `min_z` | number | yes | Minimum Z coordinate of the region to load (Unreal units / cm) |
| `max_x` | number | yes | Maximum X coordinate of the region to load (Unreal units / cm) |
| `max_y` | number | yes | Maximum Y coordinate of the region to load (Unreal units / cm) |
| `max_z` | number | yes | Maximum Z coordinate of the region to load (Unreal units / cm) |

