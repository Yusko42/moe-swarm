# GGUF и оценка RAM

Документ описывает фичу `feature/gguf-memory-estimator`. Её код пока не
включён в `dev`; команды ниже доступны в сборке с этой фичей.

`model_profile` работает без llama.cpp (`SWARM_BUILD_LLAMA=OFF`). Интерфейс:
`read_gguf_header`, `build_profile`, `estimate_ram` в
`src/initiator/model_profile/model_profile.hpp`.

```bash
./build/bin/swarm model-profile model.gguf --tensors
./build/bin/swarm model-profile model.gguf --header-only --tensors
./build/bin/swarm model-profile model.gguf --placement placement.txt --bytes > report.tsv
```

По умолчанию контекст 8192; `--context N` меняет его. `--header-only` разрешает
просмотр отдельного шарда и неподдерживаемой архитектуры. Весовые данные не
читаются и не отображаются в память. Проверка диапазонов использует размер файла.
Метаданные сохраняются как типы; числовые поля attention и архитектуры — как
значения. Большие массивы токенизатора пропускаются. Числа имеют порядок байтов
GGUF v2 little-endian или v3 little/big-endian.

Таблица типов соответствует llama.cpp
[`cd959bae99ff4bed22bef2604f45241c09bbb8c8`](https://github.com/ggml-org/llama.cpp/blob/cd959bae99ff4bed22bef2604f45241c09bbb8c8/gguf-py/gguf/constants.py).
Удалённые/неизвестные типы отклоняются. Делимость на блок проверяется для `ne[0]`.
`tensor_bytes` — сумма полезных байтов; `data_padding` — оставшаяся часть секции
данных. `standard_layout=0` предупреждает о необычных промежутках или хвосте;
перекрытия и выход за конец файла всегда ошибка. Таким образом большие
расхождения с размером файла видны, а допустимые нестандартные раскладки читаются.

## Раскладка

Одна строка на узел; значения памяти — целые байты, диапазон слоёв `[first,end)`:

```text
# name first end engine_bytes overhead_bytes limit_bytes nonblock_tensor...
node1 0 24 268435456 134217728 17179869184 token_embd.weight
node2 24 48 268435456 134217728 17179869184 output.weight output_norm.weight
```

Имена с пробелами заключаются в двойные кавычки. Список внеблочных тензоров
должен точно соответствовать файлу. Каждый слой размещается ровно один раз,
каждый внеблочный тензор — хотя бы раз. Явное повторение тензора на разных
узлах учитывает копии; повторение внутри узла — ошибка.

Таблица содержит веса слоёв, внеблочные веса, KV, буферы движка, обвязку,
итог, лимит и `fits`. По умолчанию вывод в MiB; `--bytes` даёт точные числа.
Буферы движка и обвязка задаются пользователем, а не выводятся из GGUF.

KV F16 на слой: `context × head_count_kv × (key_length + value_length) × 2`.
Отсутствующий `head_count_kv` заменяется на `head_count`; отсутствующие длины —
на `embedding_length / head_count`. Поддерживаются скаляры и массивы по слоям.
Разрешены стандартные attention-архитектуры `llama`, `mistral`, `qwen2`,
`qwen2moe`, `qwen3`, `qwen3moe`; MLA, гибридные модели и split-модели
для RAM-профиля отклоняются. Другие типы KV, padding движка, несколько
контекстов, runtime repacking и дополнительные копии весов требуют отдельных
параметров или другой модели расчёта. Это оценка компонентов, не измерение RSS.

Для [Qwen3-30B-A3B](https://huggingface.co/Qwen/Qwen3-30B-A3B/blob/main/config.json)
используются **явные** `key_length=value_length=128`, 4 KV-головы и 48 слоёв:
16 MiB на слой, 768 MiB всего; два узла по 24 слоя получают по 384 MiB KV.
Здесь `embedding_length / head_count = 64`, поэтому явные длины критичны.

## Проверки

```bash
cmake --preset ci
cmake --build --preset ci --parallel 2
ctest --preset ci
git submodule update --init external/llama
python -m pip install ./external/llama/gguf-py
python tools/compare_gguf.py ./build/bin/swarm model.gguf
python tests/gguf_oracle_test.py ./build/bin/swarm
```

C++ тесты проверяют Qwen3, массивы метаданных, fallback, ограничения и раскладку.
Python-тест CLI создаёт маленькие GGUF: Q4_K `[256,3]` — 432 байта,
Q8_0 `[32,2]` — 68, F32 `[3]` — 12. Сумма 512, padding 64.
Проверяются endian, v2/v3, повреждения, неизвестные типы, переполнения,
пересечения, дубликаты и пропуск массивов строк.

`compare_gguf.py` сопоставляет каждое имя, форму в порядке GGML, ID типа,
размер, абсолютное смещение, сумму и начало секции данных с `GGUFReader`.
Только Python-эталон использует mmap. Устанавливайте пакет из закреплённого
подмодуля: опубликованные версии могут отличаться (например, Q8_1).

[Протокол локальной проверки](model-profile-validation.md): среда, команды,
результаты сверки и границы проверки.

## Сравнение с #19160

На 2026-11-17 [страница #19160](https://ai.nsu.ru/issues/19160) недоступна
из среды разработки. Пользователь подтвердил, что логов пока нет; реальная
модель не предоставлена. Числовое
сравнение со стендом пока **не выполнено**; расхождения не выдумываются.

После получения одного полного startup-лога:

```bash
python tools/compare_buffers.py report.tsv startup.log --map CPU=node1 --map CPU_Mapped=node1 --map RPC0=node2 > comparison.md
```

Используйте реальные имена устройств из лога. Скрипт сравнивает `model buffer`
с суммой весов слоя и внеблочных тензоров, `KV buffer` с KV, `compute buffer`
с заданным параметром engine. Печатает signed-разницу в байтах и учитывает
точность округления лога. Несколько буферов разных устройств одного узла
суммируются; повторные записи одной пары device/component отклоняются,
чтобы не смешивать загрузки. Отсутствующие компоненты отмечаются `missing`.

Причины возможных расхождений: выравнивание аллокатора, repacking квантованных
весов, разделение CPU/RPC, дублирование embeddings/output, KV padding,
тип KV, фактический `n_ctx`, число параллельных последовательностей.
Обвязка/RSS не сравниваются с compute buffer: это разные компоненты.
