#!/usr/bin/env bash
# Connect Claude Code to the LABIOS MCP server via Docker stdio.
# Add this to your Claude Code MCP config:
#   "labios": { "command": "/path/to/labios/mcp/connect.sh" }
exec docker exec -i labios-mcp-1 python -m labios_mcp.server
