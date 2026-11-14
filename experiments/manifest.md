# Манифест экспериментов

## 1. llama.cpp

- Версия: [`b12110`](https://github.com/ggml-org/llama.cpp/releases/tag/b12110)
- Коммит: `cd959bae99ff4bed22bef2604f45241c09bbb8c8`
- Опции CMake
    - `-DCMAKE_BUILD_TYPE=Release`
    - `-DGGML_RPC=ON` (только CPU-бэкенд, без GPU)

## 2. Модели

- Формат GGUF
- Квантование Q3_K_M
- Кладутся в `models/` (это может быть симлинком!)

### 2.1. Основная модель — Qwen3-30B-A3B

- Используется для НФТ.
- Характеристика: MoE, 30.5B параметров, 3.3B активных, 128 экспертов (8 активных), 48 слоёв
- Размер: 13.7 ГиБ

Файлы:

- Исходники: https://huggingface.co/Qwen/Qwen3-30B-A3B
- GGUF: https://huggingface.co/unsloth/Qwen3-30B-A3B-GGUF/resolve/d5b1d57bd0b504ac62ae6c725904e96ef228dc74/Qwen3-30B-A3B-Q3_K_M.gguf
- SHA256: `a63f06070fccd2e3a329f3bfe5e996709a7f305d04c6acf2c8ae900c4723fffa`

### 2.2. Малая модель — Qwen3-0.6B

- Используется для smoke-тестов, ФТ и тестов сбоев
— Характеристика: плотная (не MoE), 28 слоёв
- Размер: 331 МиБ

Файлы:

- Исходники: https://huggingface.co/Qwen/Qwen3-0.6B
- GGUF: https://huggingface.co/unsloth/Qwen3-0.6B-GGUF/resolve/50968a4468ef4233ed78cd7c3de230dd1d61a56b/Qwen3-0.6B-Q3_K_M.gguf
- SHA256: `30bb9f9222cf58c076277879881a24e7127a10be32076d76d8c70f8defc01f5d` 

## 3. Минимальный тест, что всё работает

```bash
# ПК соседа
ggml-rpc-server -H 0.0.0.0 -p 50052

# ПК инициатора
llama-cli -m models/Qwen3-0.6B-Q3_K_M.gguf -st -n 64 -p "hello world" \
  -ngl 99 --rpc {ip_соседа}:50052
```

Проверка пройдена, если модель выдала ответ, а в логе `ggml-rpc-server` видно
входящее подключение. `-ngl 99` нужен, чтобы слои ушли на соседа.

