# Unreal MCP Server 5.0 (beta) for Unreal Engine 5.8

Unreal MCP Server exposes the Unreal Editor to MCP-compatible AI clients (Claude Code, Claude Desktop, Cursor, VS Code Copilot, Windsurf and others) over the Model Context Protocol (spec 2025-06-18) on a local HTTP endpoint. Version 5.0 adds what an AI agent needs to work safely and to check its own results: scoped permissions, session-owned state, editor surface capture and observation, deterministic validators for every authoring family, typed patches with revision checks, preflight plans with verified recovery, and honest diagnostics.

**Start here:** [GETTING_STARTED.md](docs/GETTING_STARTED.md) · [FEATURES.md](docs/FEATURES.md) · [TOOL_REFERENCE.md](docs/TOOL_REFERENCE.md) · [KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md) · [CHANGELOG.md](docs/CHANGELOG.md)

**Agent instructions:** `Prompts/unreal_mcp_system_prompt.md` (main system prompt) and `Prompts/editor_observer_instructions.md` (how to see and verify editor changes).

## Requirements

- Unreal Engine 5.8.2, Windows 64-bit (this package is built and validated on 5.8.2, changelist 56702186). Other platforms are not included.
- Optional: PythonScriptPlugin (for `execute_python` and the legacy generation tools), Epic's experimental ModelContextProtocol plugin (toolset interop).

## Install in two minutes

1. Copy the `UnrealMCPServer` folder into `<YourProject>/Plugins/`.
2. Open the project. With the prebuilt package the plugin loads immediately; with the source package the editor compiles it.
3. The server starts automatically on `http://127.0.0.1:13579/mcp`. The status-bar dot shows the port and tool count and offers **Copy Claude Code Config**.
4. Connect a client, for example `claude mcp add unreal -t http http://localhost:13579/mcp`.

Every package carries a `MANIFEST.json` listing each file with its SHA-256 and, for the prebuilt package, the binary's build identity.

## Support

Use `get_server_health` and `get_server_capabilities` from any client to check the server, and `export_diagnostic_bundle` to produce a redacted bundle (health, capabilities, your operations, tool timing, log tail) for a support request. API keys and auth tokens are never printed.

Copyright StraySpark Studio 2026. All rights reserved.
