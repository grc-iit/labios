# =============================================================================
# Stage 1: Builder
# =============================================================================
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    pkg-config \
    libssl-dev \
    python3-dev \
    && rm -rf /var/lib/apt/lists/*

COPY . /src
WORKDIR /src

RUN cmake --preset release \
    && cmake --build build/release -j"$(nproc)"

# =============================================================================
# Stage 2: Dispatcher
# =============================================================================
FROM debian:bookworm-slim AS dispatcher

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/release/src/services/labios-dispatcher /usr/local/bin/
COPY --from=builder /src/build/release/src/services/labios-demo /usr/local/bin/
COPY conf/ /etc/labios/

ENV LABIOS_CONFIG_PATH=/etc/labios/labios.toml

ENTRYPOINT ["labios-dispatcher"]

# =============================================================================
# Stage 3: Worker
# =============================================================================
FROM debian:bookworm-slim AS worker

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/release/src/services/labios-worker /usr/local/bin/
COPY conf/ /etc/labios/

ENV LABIOS_CONFIG_PATH=/etc/labios/labios.toml

ENTRYPOINT ["labios-worker"]

# =============================================================================
# Stage 4: Manager
# =============================================================================
FROM debian:bookworm-slim AS manager

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/release/src/services/labios-manager /usr/local/bin/
COPY conf/ /etc/labios/

ENV LABIOS_CONFIG_PATH=/etc/labios/labios.toml

ENTRYPOINT ["labios-manager"]

# =============================================================================
# Stage 5: Test runner
# =============================================================================
FROM debian:bookworm-slim AS test

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m pip install --break-system-packages --no-cache-dir \
       flatbuffers==24.3.25 pytest==8.3.5

COPY --from=builder /src/build/release/tests/labios-smoke-test /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-data-path-test /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-catalog-manager-test /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-intercept-test /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-benchmark-test /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-bench_trace_guided_selection /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-scheduling-test /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-live-correctness-driver /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-elastic-flood-test /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-kernel-cm1-test /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-kernel-hacc-test /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-kernel-montage-test /usr/local/bin/
COPY --from=builder /src/build/release/tests/labios-kernel-kmeans-test /usr/local/bin/
COPY --from=builder /src/build/release/src/services/labios-demo /usr/local/bin/
COPY --from=builder /src/build/release/lib/liblabios_intercept.so /usr/local/lib/
COPY --from=builder /src/build/release/python/ /usr/local/lib/labios/python/
COPY --from=builder /src/tests/python/ /opt/labios/tests/python/
COPY --from=builder /src/examples/python/ /opt/labios/examples/python/
COPY --from=builder /src/conf/ /etc/labios/

RUN ldconfig

ENV LABIOS_NATS_URL=nats://nats:4222
ENV LABIOS_REDIS_HOST=redis
ENV LABIOS_CONFIG_PATH=/etc/labios/labios.toml
ENV PYTHONPATH=/usr/local/lib/labios/python

CMD ["labios-smoke-test"]
