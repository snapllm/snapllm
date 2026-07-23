FROM ubuntu:22.04 AS build

ARG SNAPLLM_CUDA=OFF
ARG SNAPLLM_ENABLE_DIFFUSION=ON
ARG SNAPLLM_ENABLE_MULTIMODAL=ON

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    pkg-config \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DSNAPLLM_CUDA=${SNAPLLM_CUDA} \
    -DSNAPLLM_ENABLE_DIFFUSION=${SNAPLLM_ENABLE_DIFFUSION} \
    -DSNAPLLM_ENABLE_MULTIMODAL=${SNAPLLM_ENABLE_MULTIMODAL} \
    -DSNAPLLM_ENABLE_PYTHON_BINDINGS=OFF

RUN cmake --build build --config Release --target snapllm_cli -j$(nproc)

FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

ENV SNAPLLM_MODELS_PATH=/models

WORKDIR /app

RUN groupadd --system snapllm \
    && useradd --system --gid snapllm --home-dir /app --shell /usr/sbin/nologin snapllm \
    && mkdir -p /workspace /models \
    && chown -R snapllm:snapllm /app /workspace /models

COPY --from=build /src/build/bin/snapllm /usr/local/bin/snapllm

USER snapllm:snapllm

EXPOSE 6930

CMD ["snapllm", "--server", "--host", "127.0.0.1", "--port", "6930", "--workspace-root", "/workspace"]
