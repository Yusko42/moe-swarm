# Протокол замера MoE Swarm

> Скопировать этот файл для каждого испытания в каталог результатов, например `results/YYYY-MM-DD/run-group-ID.md`. Пустые поля заполнять данными, неудачные прогоны не удалять. Статус: `DRAFT / MEASURED / PASS / FAIL / BLOCKED / NOT INSTRUMENTED`.

## 1. Метаданные и связь с требованиями

| Поле | Значение |
|---|---|
| `run_group_id` | TODO |
| Дата/время и часовой пояс | TODO |
| QA / исполнитель | TODO |
| Методика (версия/commit) | `docs/qa/methodology.md`, TODO |
| НФТ / тест-кейс / чек-лист | TODO |
| Цель проверки и принятый порог | TODO (не выдумывать, если TBD) |
| `stand_id` и режим | TODO (3 узла / 1 ПК / пам. увеличена) |
| Исход `PASS / FAIL / MEASURED / BLOCKED` | TODO |

## 2. Стенд

| Узел | Роль | CPU / ядра | RAM (тип, MHz, каналы; установ./отдано) | SSD | ОС | Сеть (линк, RTT) |
|---|---|---|---|---|---|---|
| PC-1 | Инициатор | TODO | TODO | TODO | TODO | TODO |
| PC-2 | Сосед | TODO | TODO | TODO | TODO | TODO |
| PC-3 | Сосед | TODO | TODO | TODO | TODO | TODO |

Дополнительно: switch/cables; фоновые процессы и CPU/RAM до замера; VM/железо; swap; ограничение и замер cgroup; сетевые параметры: TODO.

## 3. ПО, модель, корпус, генерация

| Поле | Значение |
|---|---|
| MoE Swarm commit/build | TODO |
| llama.cpp / llama-server version/commit | TODO |
| Файл модели, сборка, квантовка | TODO |
| Размер и SHA-256 GGUF | TODO |
| Tokenizer/chat template | TODO |
| `corpus_id` / SHA-256 / фактические prompt tokens | TODO |
| Сценарий (`wiki-probe`, short, 1k, 4k, near8k, multitur) | TODO |
| `n_ctx`, KV type, allocation `--memory` | TODO |
| temperature / seed / top-p / top-k | TODO |
| thinking отключён: как проверено | TODO |
| `max_new_tokens`, `ignore_eos`, stop policy | TODO |
| `cache_prompt` / cache profile / фактические cached tokens | TODO |
| threads / batch / parallel slots / GPU off | TODO |
| warm-up и политика рестарта/сброса кэша | TODO |
| источник таймингов / версия схемы полей | TODO |

## 4. Исходные измерения (10 учитываемых прогонов)

Для каждой конкретной метрики хранить исходный ряд из 10 значений и отдельный файл машинных данных (CSV/JSONL). Таблица ниже — пример структуры, значения не заданы.

| № | `request_id` | Вход всего / обработано / cache | Выход токенов / decode steps | TTFT client, с | Prompt, т/с | Decode, т/с | Queue, с | RAM peak PC1/2/3, МиБ | Валидность / примечание |
|---:|---|---|---|---:|---:|---:|---:|---|---|
| 0 | warm-up | TODO | TODO | — | — | — | — | — | EXCLUDED (warm-up) |
| 1 | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |
| 2 | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |
| 3 | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |
| 4 | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |
| 5 | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |
| 6 | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |
| 7 | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |
| 8 | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |
| 9 | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |
| 10 | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |

Повторные/ошибочные строки: `run_id`, причина `INVALID`, исходные данные, ссылка на лог: TODO.

## 5. Итоговые показатели

| Метрика | Единица | n | Среднее | Медиана | Минимум/максимум | Источник / формула |
|---|---|---:|---:|---:|---|---|
| `server_startup_s` | с | TODO | TODO | TODO | TODO | process start → `/health` 200 |
| `swarm_startup_s` | с | TODO | TODO | TODO | TODO | `/start` → `model_ready` |
| `ttft_client_s` | с | TODO | TODO | TODO | TODO | client stream |
| `prompt_tps` | т/с | TODO | TODO | TODO | TODO | processed token count / prompt ms |
| `generation_tps` | т/с | TODO | TODO | TODO | TODO | backend timings, semantics documented |
| `queue_wait_s` | с | TODO | TODO | TODO | TODO | received → dequeued |
| `request_to_answer_s` | с | TODO | TODO | TODO | TODO | received → response started |
| memory.peak PC1 / PC2 / PC3 | МиБ | TODO | TODO | TODO | TODO | cgroup v2 per-run или аналог |

**Среднее:** сумма значений / n. **Медиана:** середина отсортированного ряда (для n=10 — среднее между 5-м и 6-м). `INVALID` не скрывать; исходный ряд и количество повторов обязательны. Показатели из несовместимых профилей кэширования **не объединять**.

## 6. Результат, отклонения и вложения

- Вывод относительно *актуального* НФТ: TODO.
- Отклонения от методики / нестабильность / важные замечания: TODO.
- Ссылки на исходные JSONL/CSV: TODO.
- Технические логи с timestamp и IDs (без текста диалогов): TODO.
- Снимки конфигурации ОС/cgroup/сети: TODO.
- Баги / связанные задачи: TODO.
- Вопросы аналитикам и разработчикам: TODO.

**Исполнитель:** TODO. **Дата:** TODO. **Рецензент:** TODO.
