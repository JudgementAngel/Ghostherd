# Features of Unreal MCP Server 5.0

## Safety model

- **Scopes** (Read, Scene, Destructive) applied to direct tools, nested calls, imported toolsets, resources and custom methods.
- **Session ownership**: plans, snapshots, observers, frames, operations, stored results and bundles are visible only to the session that created them; foreign or expired ids answer `not found`.
- **Explicit previews**: tools with `supportsDryRun` run a dedicated non-mutating preview; a preview can never trigger a mutating child call.
- **Undo**: mutating tools record named undo steps except while Play-In-Editor is active (reported as `undo_recorded=false`).
- **Bounded everything**: rate limit, request and result size caps, operation quotas, observer byte budgets, retention times.

## Seeing the editor

- `list_editor_surfaces`, `focus_editor_surface`, `capture_editor_surface`: stable surface ids, geometry and DPI, capture availability with reasons, fresh frames with `frame_id`, pixel hash and warnings; crops and size limits for readable graphs.
- **After-operation readiness**: a frame is fresh only after the operation finished and the editor painted; otherwise `not_ready` with a retry hint.
- **Observers**: leased, rate-limited observation of one surface (`periodic`, `on_change`, `after_operation`), bounded history, polling with retry hints; frames readable as resources.
- **Verification records** and **pixel comparison** on `compare_editor_snapshots`, plus the `observe_edit_verify` prompt.

## Validating and editing

- Deterministic validators with a shared `issues[]` shape: widget layout, Blueprint graphs, animation setups, materials and instances, Niagara systems, sound assets, PCG graphs, asset data validation, import preflight, shared-asset instance impact.
- Typed patches with revision fingerprints, preview and read-back: `apply_widget_patch`, `apply_blueprint_patch`.
- Preflight plans: `plan_actor_transform` and `plan_actor_changes` (property, transform, rename, create, delete) bound to captured state; `apply_change_plan` with idempotency keys, an effect journal and automatic rollback; `revert_change_plan` with verified restores.
- World-bound object references and structural snapshots with diffs; World Partition inspection with loaded/unloaded descriptors.

## Long work as operations

- `run_editor_scenario` (tick-driven Play-In-Editor with waits, assertions, input and capture), `measure_frame_times`, `submit_generation_job` (asynchronous external generation with a `mock` provider), `run_pcg_generation`.
- `get_editor_operation`, `cancel_editor_operation`, `list_editor_operations`; also exposed as the `tasks/*` extension. Cooperative cancellation, deadlines, quotas and a per-tick scheduler budget.

## Diagnostics and honesty

- `get_server_health` (with tool timing telemetry), `get_server_capabilities`, `list_worlds`, `export_diagnostic_bundle` (redacted, checksummed).
- Performance reports carry `provenance` (estimate or measurement) and run metadata; `measure_frame_times` measures real frames.
- Oversized results are stored and paged (`get_result_page`, `unreal://results/{id}`) instead of truncated silently.
- `execute_python` runs code as a script file with structured output and a safe data channel (`MCP_DATA`).

## Discovery

Catalog mode with `search_tools` (annotated), `list_tool_categories`, `get_tool_schemas`; paginated `tools/list`; resources and resource templates; prompts. The generated [tool reference](TOOL_REFERENCE.md) lists every tool with its arguments and annotations.
