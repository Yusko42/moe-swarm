#!/usr/bin/env bash
set -euo pipefail
repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
src="$repo/external/llama"
out=$(realpath "${1:?Укажите каталог сборки вне репозитория}")
[[ "$out/" != "$repo/"* ]]
[[ $(uname -m) == x86_64 ]]
[[ $(git -C "$src" rev-parse HEAD) == cd959bae99ff4bed22bef2604f45241c09bbb8c8 ]]
[[ -z $(git -C "$src" status --porcelain) ]]
cmake -S "$src" -B "$out" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DGGML_RPC=ON -DGGML_NATIVE=OFF \
    -DBUILD_SHARED_LIBS=OFF -DLLAMA_OPENSSL=OFF -DLLAMA_BUILD_TESTS=OFF \
    -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_NUMBER=12110 \
    -DGGML_AVX=OFF -DGGML_AVX2=OFF -DGGML_AVX512=OFF \
    -DGGML_FMA=OFF -DGGML_F16C=OFF -DGGML_BMI2=OFF -DGGML_SSE42=OFF
cmake --build "$out" --parallel "${JOBS:-6}" --target llama-server ggml-rpc-server
(cd "$out/bin" && sha256sum llama-server ggml-rpc-server)
