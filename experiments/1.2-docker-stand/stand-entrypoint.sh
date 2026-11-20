#!/usr/bin/env bash
# Точка входа инициатора: проверяет модель, ждёт исполнителей и передаёт
# llama.cpp их адреса. Параметры уходят в llama.cpp через переменные LLAMA_ARG_*,
# поэтому работают и для llama-server, и для llama-cli.
set -euo pipefail

: "${MODEL:?Укажите MODEL — имя файла из models/, например MODEL=Qwen3-0.6B-Q3_K_M.gguf}"
WORKERS="${WORKERS:-0}"
WORKER_HOST="${WORKER_HOST:-worker}"
RPC_PORT="${RPC_PORT:-50052}"
WAIT_TIMEOUT="${WAIT_TIMEOUT:-120}"

model_path="/models/${MODEL}"
if [[ ! -f "${model_path}" ]]; then
    echo "Модель ${MODEL} не найдена в models/. Доступные файлы:" >&2
    ls -1 /models >&2 || true
    exit 1
fi
export LLAMA_ARG_MODEL="${model_path}"

if (( WORKERS > 0 )); then
    echo "Ожидание исполнителей: ${WORKERS} (до ${WAIT_TIMEOUT} с)"
    deadline=$(( SECONDS + WAIT_TIMEOUT ))
    while :; do
        # DNS Docker возвращает адреса всех реплик сервиса.
        mapfile -t ips < <(getent ahostsv4 "${WORKER_HOST}" | awk '{ print $1 }' | sort -u)
        peers=()
        for ip in "${ips[@]}"; do
            if timeout 1 bash -c "exec 3<>/dev/tcp/${ip}/${RPC_PORT}" 2>/dev/null; then
                peers+=("${ip}:${RPC_PORT}")
            fi
        done
        (( ${#peers[@]} >= WORKERS )) && break
        if (( SECONDS >= deadline )); then
            echo "Готово исполнителей: ${#peers[@]} из ${WORKERS}. Запустите их: docker compose up -d worker" >&2
            exit 1
        fi
        sleep 1
    done
    LLAMA_ARG_RPC="$(IFS=,; echo "${peers[*]}")"
    export LLAMA_ARG_RPC
    # Без -ngl слои не уходят на RPC-устройства исполнителей.
    export LLAMA_ARG_N_GPU_LAYERS="${LLAMA_ARG_N_GPU_LAYERS:-99}"
    echo "Исполнители: ${LLAMA_ARG_RPC}"
else
    echo "Исполнителей нет, модель работает только на инициаторе"
fi

exec "$@"
