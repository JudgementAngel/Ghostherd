# Getting started

## 1. Install

| Layout | Path | Notes |
|---|---|---|
| Project plugin (recommended) | `<Project>/Plugins/UnrealMCPServer/` | Copy the unpacked package here and open the project. |
| Engine plugin | `<Engine>/Engine/Plugins/Marketplace/UnrealMCPServer/` | Available to every project on that engine build. |

The prebuilt package contains Win64 editor binaries built against Unreal Engine 5.8.2; no compilation is needed. The source package compiles when the project opens (a C++ toolchain is required). Verify a package before installing by checking the files against `MANIFEST.json`.

Required engine plugins are enabled through the descriptor. `get_server_capabilities` reports which optional plugins are present in the running editor and why a feature is unavailable.

## 2. Connect

The server starts with the editor when **Auto Start Server** is enabled (default) on `http://127.0.0.1:13579/mcp`, protocol `2025-06-18`.

```bash
claude mcp add unreal -t http http://localhost:13579/mcp
```

| Client | Configuration |
|---|---|
| Claude Code | command above, or **Copy Claude Code Config** from the status-bar menu |
| Claude Desktop | STDIO bridge `Content/Python/bridge.py` with `UNREAL_MCP_PORT=13579` |
| Cursor, VS Code Copilot (agent mode), Windsurf | HTTP MCP server at the URL above |

Every response carries `Mcp-Session-Id`; clients keep sending it. Plans, snapshots, observers, operations, retained frames, stored results and diagnostic bundles belong to the session that created them and are released when the session ends.

Discovery defaults to **catalog** mode: a small core set plus `search_tools`, `list_tool_categories` and `get_tool_schemas` for everything else. Request `tools/list` with `{"exposure": "full"}` for the whole registry.

## 3. Permissions

Three scopes gate every tool, resource template and nested call:

| Scope | Allows |
|---|---|
| Read | inspection, validation, capture, observation, planning |
| Scene | creating and editing content, revealing UI, applying plans and patches |
| Destructive | deleting and overwriting, `execute_python`, console commands, external generation, imported toolsets |

Without tokens, loopback callers receive Scene (or Destructive when **Allow Destructive Scope** is on). With **Require Auth Token**, clients send `Authorization: Bearer <token>`; **Auth Token Scopes** maps a token to its scope, so a read-only token and an editing token can be issued separately. Binding to a non-loopback address is refused without tokens. Only loopback `Host` headers and allow-listed `Origin` values are accepted.

## 4. First session

1. `get_server_health` and `get_server_capabilities` to see the editor state and available features.
2. `list_worlds`, `list_actors` or `search_assets` to find what you will work on.
3. Validate before and after editing: `validate_blueprint`, `validate_widget_layout`, `validate_material_setup`, `validate_animation_setup`, `validate_pcg_graph`, `validate_assets`.
4. Look at the result: `list_editor_surfaces`, `capture_editor_surface`, and the observer tools described in `Prompts/editor_observer_instructions.md`.
5. For long work use operations: `run_editor_scenario`, `measure_frame_times`, `submit_generation_job`, `run_pcg_generation`; poll `get_editor_operation`, cancel with `cancel_editor_operation`.

## 5. Settings that matter

| Setting | Default | Purpose |
|---|---|---|
| Server Port / Bind Address | 13579 / 127.0.0.1 | listener |
| Tool Exposure Mode | Catalog | catalog or full `tools/list` |
| Max Requests Per Minute | 120 | rate limit per client |
| Tool Call Timeout Seconds | 60 | network-thread wait for the game thread |
| Max Tool Result KB | 1024 | larger results are stored and paged through `get_result_page` |
| Enable Python Bridge / fal.ai API Key | off / empty | `execute_python`; external generation |

## 6. Upgrading from 4.x

Tool names and arguments are kept. Behaviour changes: `execute_python` runs code as a script file and returns structured output; validators return structured `issues[]`; widget mutators take `save`; patch and plan tools leave assets dirty unless asked to save; previews are explicit and never mutate; performance reports state whether numbers are estimates or measurements; oversized results are paged. Settings keys are unchanged. Keep a backup of the previous plugin folder and your project `Config` before upgrading; restoring them returns the previous version.
