# Образ стенда: llama.cpp (CPU + RPC) одной версии для инициатора и исполнителей.
# Версия закреплена в experiments/manifest.md.
ARG UBUNTU_VERSION=26.04

FROM ubuntu:${UBUNTU_VERSION} AS build

ARG LLAMA_TAG=b12110
ARG LLAMA_COMMIT=cd959bae99ff4bed22bef2604f45241c09bbb8c8
# ON — оптимизация под CPU машины, на которой собирается образ.
# OFF — переносимый образ для запуска на других ПК (медленнее).
ARG GGML_NATIVE=ON

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git ca-certificates \
 && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch "${LLAMA_TAG}" \
      https://github.com/ggml-org/llama.cpp.git /src \
 && test "$(git -C /src rev-parse HEAD)" = "${LLAMA_COMMIT}"

RUN cmake -S /src -B /build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DGGML_RPC=ON \
      -DGGML_NATIVE=${GGML_NATIVE} \
      -DBUILD_SHARED_LIBS=OFF \
      -DLLAMA_OPENSSL=OFF \
      -DLLAMA_BUILD_TESTS=OFF \
      -DLLAMA_BUILD_EXAMPLES=OFF \
      -DLLAMA_BUILD_NUMBER=${LLAMA_TAG#b} \
 && cmake --build /build --target llama-cli llama-server ggml-rpc-server

FROM ubuntu:${UBUNTU_VERSION}

RUN apt-get update \
 && apt-get install -y --no-install-recommends libgomp1 curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /build/bin/llama-cli /build/bin/llama-server /build/bin/ggml-rpc-server /usr/local/bin/
COPY stand-entrypoint.sh /usr/local/bin/stand-entrypoint

RUN chmod +x /usr/local/bin/stand-entrypoint

WORKDIR /models
