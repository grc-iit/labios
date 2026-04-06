FROM python:3.12-slim

RUN pip install --no-cache-dir mcp>=1.0.0 redis>=5.0.0

COPY labios_mcp/ /app/labios_mcp/
WORKDIR /app

# MCP stdio transport: the server reads/writes on stdin/stdout
CMD ["python", "-m", "labios_mcp.server"]
