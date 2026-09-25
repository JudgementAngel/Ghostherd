# Editor Observer Instructions — seeing the editor while you work (Unreal MCP Server 5.0)

These instructions are for AI agents connected to the Unreal MCP Server. They cover the tools that let you look at the editor, watch it change, verify your own edits and correct them. Load them together with the main system prompt.

## The loop you should follow

**Observe → act → wait for the relevant update → capture → inspect → verify → correct.**

A successful tool call is only one piece of evidence. A screenshot proves what the editor displayed, not that the result is correct. Structured validators prove measurable properties. Your own inspection of the image is a judgment. Keep the three apart and record which one you used.

## 1. Find the surface you are working on

- `list_editor_surfaces` returns every editor window, modal or overlay window, dock tab and level viewport with a stable `surface_id`, a `generation`, geometry, `visible`/`active` flags and an honest `capture.supported` with a reason when capture is impossible. It never activates anything.
- Filter with `kind` (`window`, `modal`, `overlay`, `tab`, `level_viewport`) and `title_contains`. Asset editors report the assets they own, so you can find "the Designer tab of WBP_HUD".
- The listing also names `active_modal_surface_id` (with `modal_blocking=true`) and `visible_menu_surface_id`. If a modal is blocking, say so; do not dismiss it and do not capture something else as if it were the target.
- `visual_backend.available=false` means the process cannot render (for example NullRHI). Structural discovery still works; no image can be produced. Never describe a frame you did not receive.
- Surface ids are invalidated when the window or tab closes; `generation` advances when the descriptor changes. Re-list after opening or closing editors.

## 2. Reveal before you capture (only when needed)

- `focus_editor_surface` with a `surface_id` activates a background tab, restores a minimized window and brings its window forward; with `asset_path` it opens or focuses that asset's editor and returns its tabs. It is a UI effect (Scene scope). It never closes, saves or edits content.
- Pass `expected_generation` when you must be sure you are revealing the surface you listed.
- Capture is read-only and cannot reveal. If `capture.supported=false` with a "reveal required" reason, focus first, then capture.

## 3. Capture a fresh frame

- `capture_editor_surface {surface_id}` returns the image inline plus `frame_id`, `sequence`, `paint_sequence`, `captured_at`, `pixel_hash`, dimensions, `scale`, `dpi_scale`, `freshness` and `warnings` (shader compilation in progress, PIE active, surface not active).
- Use `region` (physical pixels relative to the surface) and `max_long_edge` to get readable crops of graphs and panels instead of one shrunken screenshot. Prefer `png` for text and graphs, `jpeg` for viewports.
- **After an edit**, pass `after_operation_id` (any operation id you own: scenario, measurement, generation job) with `wait_policy=next_paint`. The frame is `fresh` only when the operation finished and Slate painted at least once since; otherwise the server answers `not_ready` with `retry_after_ms`. Retry after that delay; never assume the change is visible because time passed.
- For synchronous tool edits (no operation id), capture, then capture again with `wait_policy=stable`: `unchanged` means two consecutive captures differ in less than 0.5% of pixels; `changing` (with `allow_stale=true`) means the surface is still animating or compiling.
- `retain=true` keeps the frame for 10 minutes as `unreal://visual/frames/{frame_id}` so you can compare it later.
- Under `allow_stale=true` a frame can come back marked `stale` or `changing`. Report it as such; it is not evidence of the final state.

## 4. Watch a surface while something runs

- `start_editor_observation {surface_id, mode, rate_hz, lease_seconds, history, region}` creates an owned observer sampled by the editor at 0.2–2 Hz with identical frames skipped. Modes: `periodic`, `on_change` (sample only when Slate painted since the previous sample), `after_operation` (with `after_operation_id`: wait for the operation to finish and paint, capture once tagged with the operation, then continue).
- Poll with `get_editor_observation {observer_id, after_sequence, wait_ms}`. You receive the newest frame when it is newer than `after_sequence`; otherwise `no_change` with counters (`samples`, `unchanged`, `dropped`, `failures`) and `retry_after_ms`. Wait that long on your side; the server never blocks the editor.
- `stop_editor_observation` releases frames and ticker work. Stop observers you no longer need; each session may own two.
- Observers are bounded (three frames of history, a global byte budget, a lease that expires). An expired or invalidated observer reports its terminal state and holds no frames. Do not read stale frames as current.

## 5. Verify, then record what you verified

- Deterministic checks first: `validate_widget_layout` (overlaps, out-of-bounds, clipped, zero-size), `validate_blueprint`, `validate_material_setup`, `validate_animation_setup`, `validate_pcg_graph`, `validate_assets`. They return `issues[]` with rules and locations.
- Pixel comparison: `compare_editor_snapshots {before_frame_id, after_frame_id, pixel_threshold}` returns `visual.changed_pct`, `changed_bbox` and an 8×8 grid of per-cell change. It tells you *where* pixels changed, never whether the change is right.
- Structural comparison: `capture_editor_snapshot` before and after, then `compare_editor_snapshots {before_id, after_id}` for added, removed and changed actors.
- Record the outcome with `record_visual_verification {outcome: pass|fail|inconclusive, method: assistant|deterministic, criteria, frame_ids, surface_id, notes}`. `assistant` means you looked at the image; `deterministic` means a validator or comparison proved it. Use `fail` or `inconclusive` freely; a capture that succeeded is not a pass.
- If a person changed the editor between your action and the capture, report ambiguity, recapture and revalidate before correcting anything.

## 6. Correct and repeat, within limits

- Make one specific correction per pass with the typed tools (`apply_widget_patch`, `apply_blueprint_patch`, `plan_actor_changes` + `apply_change_plan`), then repeat steps 3 to 5.
- Stop when the target is blocked (modal, no backend, invalidated surface), when evidence is stale and cannot be refreshed, or when two passes made no progress. Say what you saw and what you could not verify.

## Reference: what each field means

| Field | Meaning |
|---|---|
| `freshness` | `fresh` (new capture after readiness), `unchanged` (stable), `stale` (operation not finished or not painted; only with `allow_stale`), `changing` (stable policy not reached), `unknown` (no policy) |
| `paint_sequence` / `paints_since_operation` | Slate post-tick counter; a frame after an operation is fresh only when paints occurred after the operation ended |
| `pixel_hash` | SHA-1 of the raw pixels; identical hashes mean identical pixels |
| `warnings` | `shaders_compiling`, `pie_active` (single temporal sample), `surface_not_active` |
| `image_delivery` | `inline_content`: the image is in the tool result; frames are also readable as `unreal://visual/frames/{frame_id}` |

The `observe_edit_verify` prompt on the server walks through this loop with concrete tool calls for a given asset.
