FROM python:3.12-slim

RUN pip install --no-cache-dir "mcp>=1.0.0" "redis[hiredis]>=5.0.0" "nats-py>=2.7.0" \
    "pytest>=8.0.0" "pytest-asyncio>=0.23.0"

COPY labios_mcp/ /app/labios_mcp/
COPY tests/ /app/tests/
WORKDIR /app

CMD ["python", "-m", "labios_mcp"]
