# Build from the repository root so the MCP image contains the exact Python SDK
# produced from the same LABIOS sources:
#   docker build -f mcp/Dockerfile -t labios-mcp .
FROM python:3.12-slim AS sdk-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates pkg-config libssl-dev \
    && rm -rf /var/lib/apt/lists/*

COPY . /src
WORKDIR /src
RUN cmake --preset release \
    && cmake --build build/release -j"$(nproc)" --target _labios

FROM python:3.12-slim

RUN apt-get update && apt-get install -y --no-install-recommends libssl3 \
    && rm -rf /var/lib/apt/lists/*
COPY mcp/requirements.txt /tmp/requirements.txt
RUN python -m pip install --no-cache-dir -r /tmp/requirements.txt

COPY --from=sdk-builder /src/build/release/python/ /usr/local/lib/labios/python/
COPY mcp/labios_mcp/ /app/labios_mcp/
COPY mcp/tests/ /app/tests/
COPY examples/mcp/ /opt/labios/examples/mcp/
WORKDIR /app

ENV PYTHONPATH=/usr/local/lib/labios/python

HEALTHCHECK --interval=5s --timeout=5s --retries=10 --start-period=5s \
  CMD python -c "import flatbuffers, labios, labios_mcp" || exit 1

CMD ["python", "-m", "labios_mcp"]
