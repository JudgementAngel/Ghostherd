#!/usr/bin/env python3
"""
Unreal MCP Server - STDIO Bridge

This script acts as a bridge between MCP clients that use STDIO transport
(Claude Code, Cursor, Windsurf, etc.) and the Unreal MCP Server HTTP endpoint.

Usage in Claude Code's claude_desktop_config.json or .mcp.json:
{
    "mcpServers": {
        "unreal": {
            "command": "python",
            "args": ["C:/path/to/UnrealMCPServer/Bridge/bridge.py"],
            "env": {
                "UNREAL_MCP_PORT": "13579"
            }
        }
    }
}

Or connect directly via URL (no bridge needed):
{
    "mcpServers": {
        "unreal": {
            "url": "http://localhost:13579/mcp"
        }
    }
}
"""

import sys
import json
import os
import urllib.request
import urllib.error

# Configuration
MCP_PORT = int(os.environ.get("UNREAL_MCP_PORT", "13579"))
MCP_URL = f"http://localhost:{MCP_PORT}/mcp"


def send_response(response: dict):
    """Send a JSON-RPC response to stdout."""
    msg = json.dumps(response)
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()


def forward_to_unreal(request: dict) -> dict:
    """Forward a JSON-RPC request to the Unreal MCP Server."""
    data = json.dumps(request).encode("utf-8")

    req = urllib.request.Request(
        MCP_URL,
        data=data,
        headers={
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            body = resp.read().decode("utf-8")
            return json.loads(body)
    except urllib.error.URLError as e:
        return {
            "jsonrpc": "2.0",
            "id": request.get("id"),
            "error": {
                "code": -32000,
                "message": f"Cannot connect to Unreal MCP Server at {MCP_URL}. "
                f"Make sure Unreal Editor is running with the UnrealMCPServer plugin enabled. "
                f"Error: {e}",
            },
        }
    except Exception as e:
        return {
            "jsonrpc": "2.0",
            "id": request.get("id"),
            "error": {
                "code": -32603,
                "message": f"Internal error: {e}",
            },
        }


def main():
    """Main loop: read JSON-RPC from stdin, forward to Unreal, respond on stdout."""
    # Log to stderr (not stdout, which is reserved for JSON-RPC)
    print(
        f"Unreal MCP Bridge started. Forwarding to {MCP_URL}",
        file=sys.stderr,
    )

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        try:
            request = json.loads(line)
        except json.JSONDecodeError as e:
            send_response(
                {
                    "jsonrpc": "2.0",
                    "id": None,
                    "error": {
                        "code": -32700,
                        "message": f"Parse error: {e}",
                    },
                }
            )
            continue

        # Check if it's a notification (no id = no response needed)
        is_notification = "id" not in request

        # Forward to Unreal
        response = forward_to_unreal(request)

        # Send response (unless it's a notification)
        if not is_notification and response:
            send_response(response)
        elif is_notification:
            # For notifications like initialized, still forward but don't respond
            pass


if __name__ == "__main__":
    main()
