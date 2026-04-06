#!/usr/bin/env bash
# Connect Claude Code to the LABIOS MCP server via Docker stdio.
# Add this to your Claude Code MCP config:
#   "labios": { "command": "/path/to/labios/mcp/connect.sh" }
exec docker compose -f "$(dirname "$0")/../docker-compose.yml" exec -T mcp python -m labios_mcp
