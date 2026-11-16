# 2.1. Проверка API llama-server

- Стенд: [1.2-docker-stand](../1.2-docker-stand), инициатор и 2 соседа, `n_parallel=1`, `n_ctx=8192`, `-rea off`.
  Запуск: `WORKER_MEMORY=8g INITIATOR_MEMORY=8g MODEL=Qwen3-30B-A3B-Q3_K_M.gguf WORKERS=2 docker compose up`
- Модель: Qwen3-30B-A3B-Q3_K_M, llama.cpp `b12110-cd959ba`

Мощная модель не нужна, так что можно любому запустить стенд на малой модели.

Во всех командах ниже `B=http://127.0.0.1:8080`, а `$MSGS` — сообщения тестового диалога:

```bash
B=http://127.0.0.1:8080
MSGS='[{"role":"system","content":"Ты — краткий помощник. Отвечай по-русски."},{"role":"user","content":"Сколько будет 2+2? Ответь одним словом."}]'
```

## Таблица

| № | Проверка | Как | Результат |
|---|----------|-----|-----------|
| 1 | Готовность | `GET /health` во время загрузки и после | Сначала отказ соединения, затем ~14 с `503 Loading model`, потом `200 {"status":"ok"}`. Во время загрузки 503 отдают все эндпоинты |
| 2 | Точная длина входа | `POST /v1/chat/completions/input_tokens` и `/apply-template` + `/tokenize` | Совпадает: `input_tokens` = `tokenize` = `usage.prompt_tokens` = 50 |
| 3 | Поток | `POST /v1/chat/completions`, `"stream": true` | SSE: роль → токены → `finish_reason` → `usage` + `timings` → `[DONE]` |
| 4 | Отмена | закрыть соединение посреди ответа | Генерация остановилась меньше чем за 1 с, CPU 0 %, следующий запрос принят |
| 5 | Подсчёт во время генерации | п. 2 параллельно с идущим ответом | Отвечает сразу, ≤ 2 мс. Второй чат ждёт в очереди |
| — | Граница контекста | вход > `n_ctx`, = `n_ctx`, `n_ctx`−30 | Вход должен быть строго меньше `n_ctx`, иначе 400. Ответ обрезается остатком контекста |
| — | `/props` | `GET /props`, `GET /v1/models` | `n_ctx`, слоты, сборка, шаблон, возможности шаблона |
| — | `/metrics` | `GET /metrics` | 501: включается флагом `--metrics` |
| — | Режим размышлений | обычный запрос и `enable_thinking:true` | `-rea off` работает, но запрос может его переопределить |
| — | Прогресс загрузки | `/health` при загрузке, `return_progress` | Прогресса загрузки модели нет. Прогресс промпта идёт по батчам в 2048 токенов |

## 1. Готовность

1. ~14 c. все эндпоинты отдают `503`
2. Когда загрузилось, `200`

```bash
# раз в 0.5 с во время загрузки
curl -s -w ' %{http_code}\n' $B/health

# после загрузки
curl -sS $B/health; curl -sSI $B/health
```

Сырые ответы:

```text
12:42:59.743 000
12:43:00.250 503 {"error":{"message":"Loading model","type":"unavailable_error","code":503}}
12:43:14.450 200 {"status":"ok"}

# HEAD /health
HTTP/1.1 200 OK
Server: llama.cpp
Content-Type: application/json; charset=utf-8
Content-Length: 15
```

## 2. Точная длина входа

- `/v1/chat/completions/input_tokens`
- `/apply-template` + `/tokenize`
- `usage.prompt_tokens`

дали одинаковое использование токенов. Флаг `add_special` на результат не влияет, потому что у Qwen3 нет BOS.

```bash
curl -sS $B/v1/chat/completions/input_tokens -H 'Content-Type: application/json' -d "{\"messages\":$MSGS}"

PROMPT=$(curl -sS $B/apply-template -H 'Content-Type: application/json' -d "{\"messages\":$MSGS}" | jq -c .prompt)

curl -sS $B/tokenize -H 'Content-Type: application/json' -d "{\"content\":$PROMPT,\"add_special\":false}"
curl -sS $B/tokenize -H 'Content-Type: application/json' -d "{\"content\":$PROMPT,\"add_special\":true}"
curl -sS $B/v1/chat/completions -H 'Content-Type: application/json' -d "{\"messages\":$MSGS,\"max_tokens\":1}"
```

Сырые ответы:

```json
// input_tokens
{"input_tokens":50,"object":"response.input_tokens"}

// apply-template
{"prompt":"<|im_start|>system\nТы — краткий помощник. Отвечай по-русски.<|im_end|>\n<|im_start|>user\nСколько будет 2+2? Ответь одним словом.<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n"}

// tokenize, add_special=false (с add_special=true — тот же массив)
{"tokens":[151644,8948,198,33995,4552,1959,7665,80559,130366,125978,69859,13,128876,132599,18530,17686,12,129089,79012,13,151645,198,151644,872,198,19311,4793,53470,68823,220,17,10,17,30,128876,46974,4824,138502,91107,12228,13,151645,198,151644,77091,198,151667,271,151668,271]}

// настоящий запрос, max_tokens=1
{"choices":[{"finish_reason":"length","index":0,"message":{"role":"assistant","content":"Ч"}}],"created":1794807796,"model":"/models/Qwen3-30B-A3B-Q3_K_M.gguf","system_fingerprint":"b12110-cd959ba","object":"chat.completion","usage":{"completion_tokens":1,"prompt_tokens":50,"total_tokens":51,"prompt_tokens_details":{"cached_tokens":0}},"id":"chatcmpl-EXC5LMo2zP2ZcNCYfFJkrbIBBsAtVjOs","timings":{"cache_n":0,"prompt_n":50,"prompt_ms":905.956,"prompt_per_token_ms":18.11912,"prompt_per_second":55.19031829360366,"predicted_n":1,"predicted_ms":0.001,"predicted_per_token_ms":0.0,"predicted_per_second":0.0}}
```

## 3. Поток

Ответ приходит как SSE (`text/event-stream`).

- Первый фрагмент содержит только роль: `delta:{role:"assistant",content:null}`.
- Дальше идёт по фрагменту на токен с `delta.content`.
- Затем приходит фрагмент с пустой `delta` и `finish_reason`: `stop`, если модель закончила сама, или
  `length` при упоре в `max_tokens`.
- Последний фрагмент содержит пустой `choices`, `usage` и `timings`;
  он есть только при `stream_options.include_usage`. Поток заканчивается строкой `data: [DONE]`.

```bash
M3='[{"role":"system","content":"Ты — краткий помощник. Отвечай по-русски."},{"role":"user","content":"Перечисли пять цветов радуги через запятую."}]'
curl -sSN $B/v1/chat/completions -H 'Content-Type: application/json' \
  -d "{\"messages\":$M3,\"max_tokens\":200,\"stream\":true,\"stream_options\":{\"include_usage\":true}}"
# то же с "max_tokens":5 — для finish_reason "length"
```

Сырой ответ, `max_tokens=200` (26 фрагментов, середина пропущена):

```text
data: {"choices":[{"finish_reason":null,"index":0,"delta":{"role":"assistant","content":null}}],"created":1794807797,"id":"chatcmpl-5ebGcSkhWJhK05dxmzGaqbg8oMxkMIhN","model":"/models/Qwen3-30B-A3B-Q3_K_M.gguf","system_fingerprint":"b12110-cd959ba","object":"chat.completion.chunk"}

data: {"choices":[{"finish_reason":null,"index":0,"delta":{"content":"К"}}],"created":1794807797,"id":"chatcmpl-5ebGcSkhWJhK05dxmzGaqbg8oMxkMIhN","model":"/models/Qwen3-30B-A3B-Q3_K_M.gguf","system_fingerprint":"b12110-cd959ba","object":"chat.completion.chunk"}

…

data: {"choices":[{"finish_reason":null,"index":0,"delta":{"content":"."}}],"created":1794807798,"id":"chatcmpl-5ebGcSkhWJhK05dxmzGaqbg8oMxkMIhN","model":"/models/Qwen3-30B-A3B-Q3_K_M.gguf","system_fingerprint":"b12110-cd959ba","object":"chat.completion.chunk"}

data: {"choices":[{"finish_reason":"stop","index":0,"delta":{}}],"created":1794807799,"id":"chatcmpl-5ebGcSkhWJhK05dxmzGaqbg8oMxkMIhN","model":"/models/Qwen3-30B-A3B-Q3_K_M.gguf","system_fingerprint":"b12110-cd959ba","object":"chat.completion.chunk"}

data: {"choices":[],"created":1794807799,"id":"chatcmpl-5ebGcSkhWJhK05dxmzGaqbg8oMxkMIhN","model":"/models/Qwen3-30B-A3B-Q3_K_M.gguf","system_fingerprint":"b12110-cd959ba","object":"chat.completion.chunk","usage":{"completion_tokens":24,"prompt_tokens":50,"total_tokens":74,"prompt_tokens_details":{"cached_tokens":0}},"timings":{"cache_n":0,"prompt_n":50,"prompt_ms":877.081,"prompt_per_token_ms":17.54162,"prompt_per_second":57.00727754905191,"predicted_n":24,"predicted_ms":1167.243,"predicted_per_token_ms":50.74969565217391,"predicted_per_second":19.70455166576283}}

data: [DONE]
```

При `max_tokens=5` всё так же, только в предпоследнем фрагменте `"finish_reason":"length"` и
`completion_tokens:5`.

## 4. Отмена

Перед закрытием соединения слот был занят (`is_processing:true`, `n_decoded:88`), CPU всех трёх
контейнеров был загружен на 64–81 %. Через секунду после закрытия слот свободен
(`is_processing:false`), CPU 0 %. В логе сервера `stop: cancel task`, а через 52 мс после него
`release ... n_tokens = 189`. Следующий запрос отработал нормально: 200 за 1.1 с.

```bash
ML='[{"role":"system","content":"Ты — краткий помощник. Отвечай по-русски."},{"role":"user","content":"Напиши очень длинный подробный рассказ о путешествии на Марс, не меньше 3000 слов."}]'
curl -sSN $B/v1/chat/completions -H 'Content-Type: application/json' \
  -d "{\"messages\":$ML,\"max_tokens\":2000,\"stream\":true}" > /dev/null & PID=$!
sleep 8; curl -sS $B/slots; docker stats --no-stream
kill $PID
sleep 1; curl -sS $B/slots; sleep 3; curl -sS $B/slots; docker stats --no-stream
curl -sS $B/v1/chat/completions -H 'Content-Type: application/json' -d "{\"messages\":$MSGS,\"max_tokens\":8}"
docker logs --since <время начала> <контейнер инициатора>
```

Сырые ответы (`/slots` — без поля `params`):

```
# /slots до закрытия
[{"id":0,"n_ctx":8192,"speculative":false,"is_processing":true,"id_task":212,"n_prompt_tokens":149,"n_prompt_tokens_processed":61,"n_prompt_tokens_cache":0,…,"next_token":[{"has_next_token":true,"has_new_line":true,"n_remain":1912,"n_decoded":88}]}]

# docker stats до закрытия
moe-swarm-stand-initiator-1 72.59%; moe-swarm-stand-worker-2 79.69%; moe-swarm-stand-worker-1 64.48%

# лог инициатора
2.00.356.454 W srv          stop: cancel task, id_task = 212
2.00.408.940 I slot      release: id  0 | task 212 | stop processing: n_tokens = 189, truncated = 0

# /slots через 1 с (через 4 с — то же)
[{"id":0,"n_ctx":8192,"speculative":false,"is_processing":false,"id_task":212,"n_prompt_tokens":189,"n_prompt_tokens_processed":0,"n_prompt_tokens_cache":0,…,"next_token":[{"has_next_token":true,"has_new_line":false,"n_remain":-1,"n_decoded":0}]}]

# docker stats через 4 с
moe-swarm-stand-initiator-1 0.00%; moe-swarm-stand-worker-2 0.00%; moe-swarm-stand-worker-1 0.00%

# следующий запрос, 200 за 1.15 с
{"choices":[{"finish_reason":"stop","index":0,"message":{"role":"assistant","content":"Четыре."}}],…,"usage":{"completion_tokens":6,"prompt_tokens":50,"total_tokens":56,"prompt_tokens_details":{"cached_tokens":0}},…}
```

## 5. Подсчёт во время генерации

Пока идёт генерация, `/tokenize`, `/apply-template`, `input_tokens`, `/health`, `/props` и `/slots`
отвечают сразу, не дольше 2 мс, как и в простое. Второй чат за 10 с не получил ни байта: он стоит
в очереди.

```bash
curl -sSN $B/v1/chat/completions -H 'Content-Type: application/json' \
  -d "{\"messages\":$ML,\"max_tokens\":2000,\"stream\":true}" > /dev/null & PID=$!
sleep 5
curl -sS -w ' %{http_code} %{time_total}\n' $B/tokenize -H 'Content-Type: application/json' -d '{"content":"Сколько будет 2+2?"}'
curl -sS -w ' %{http_code} %{time_total}\n' $B/v1/chat/completions/input_tokens -H 'Content-Type: application/json' -d "{\"messages\":$MSGS}"
# аналогично /apply-template, /health, /props, /slots
curl -sS --max-time 10 $B/v1/chat/completions -H 'Content-Type: application/json' -d "{\"messages\":$MSGS,\"max_tokens\":4}"
kill $PID
```

Сырые ответы (код и время в секундах; в скобках — тот же запрос в простое):

```text
tokenize        200 0.000809  (0.000539)
apply-template  200 0.001740
input_tokens    200 0.001529  (0.001068)  {"input_tokens":50,"object":"response.input_tokens"}
health          200 0.000532
props           200 0.000628
slots           200 0.000587
второй чат      curl: (28) Operation timed out after 10002 milliseconds with 0 bytes received
```

## Граница контекста

Вход в 8392 токена и вход ровно в 8192 токена сразу получают `400 exceed_context_size_error` с
полями `n_prompt_tokens` и `n_ctx`. Вход в 8170 токенов при `max_tokens=200` отработал с кодом 200,
но ответ оборвался на 22 токенах: `finish_reason:"length"`, `total_tokens:8192`.

```bash
# сообщение из N слов "hello"; N подбирается через /apply-template + /tokenize под нужную длину входа
words() { yes hello | head -n "$1" | tr '\n' ' '; }
mk() { jq -nc --arg u "$(words "$1") $2" --argjson mt "$3" \
  '{messages:[{role:"system",content:"Ты — краткий помощник. Отвечай по-русски."},{role:"user",content:$u}],max_tokens:$mt}'; }
mk <N на 8392 токена> "" 16 | curl -sS $B/v1/chat/completions -H 'Content-Type: application/json' -d @-
mk <N на 8192 токена> "" 16 | curl -sS $B/v1/chat/completions -H 'Content-Type: application/json' -d @-
mk <N на 8162 токена> "Напиши длинный рассказ." 200 | curl -sS $B/v1/chat/completions -H 'Content-Type: application/json' -d @-
```

Сырые ответы:

```json
// 8392 токена, 400 за 0.009 с
{"error":{"code":400,"message":"request (8392 tokens) exceeds the available context size (8192 tokens), try increasing it","type":"exceed_context_size_error","n_prompt_tokens":8392,"n_ctx":8192}}

// 8192 токена, 400 за 0.009 с
{"error":{"code":400,"message":"request (8192 tokens) exceeds the available context size (8192 tokens), try increasing it","type":"exceed_context_size_error","n_prompt_tokens":8192,"n_ctx":8192}}

// 8170 токенов, max_tokens=200: 200 за 112 с (choices и id опущены)
{"finish_reason":"length","usage":{"completion_tokens":22,"prompt_tokens":8170,"total_tokens":8192,"prompt_tokens_details":{"cached_tokens":0}},"timings":{"prompt_n":8170,"prompt_ms":107710.856,"predicted_n":22,"predicted_per_second":5.03756463975056}}
```

## Цена длинного входа

8170 токенов входа обрабатывались 108 с (76 т/с). Генерация на почти полном контексте шла
5.0 т/с, а на коротком, около 50–200 токенов, — 19.1–19.7 т/с. Данные — из `timings` ответов
выше и из п. 3.

## `/props`

Отвечает 200: `n_ctx=8192`, `total_slots=1`, `build_info`, `model_path`, `chat_template`,
`chat_template_caps`, флаги `endpoint_metrics`, `endpoint_slots`, `endpoint_props`, `is_sleeping`.
`/v1/models` дополнительно сообщает `n_ctx_train=40960`, `n_params` и `size`.

```bash
curl -sS $B/props | jq '{n_ctx:.default_generation_settings.n_ctx,total_slots,build_info,model_path,endpoint_metrics,endpoint_slots,endpoint_props,is_sleeping,chat_template_caps}'
curl -sS $B/v1/models
```

Сырые ответы (из `/props` — выборка полей; полный ответ ~7 КБ, в основном шаблон):

```json
{"n_ctx":8192,"total_slots":1,"build_info":"b12110-cd959ba","model_path":"/models/Qwen3-30B-A3B-Q3_K_M.gguf","endpoint_metrics":false,"endpoint_slots":true,"endpoint_props":false,"is_sleeping":false,"chat_template_caps":{"supports_object_arguments":true,"supports_parallel_tool_calls":true,"supports_preserve_reasoning":false,"supports_reasoning_effort":false,"supports_string_content":true,"supports_system_role":true,"supports_tool_calls":true,"supports_tools":true,"supports_typed_content":false,"reasoning_effort_levels":[]}}

// /v1/models, data[0]
{"id":"/models/Qwen3-30B-A3B-Q3_K_M.gguf","meta":{"vocab_type":2,"n_vocab":151936,"n_ctx":8192,"n_ctx_train":40960,"n_embd":2048,"n_params":30532122624,"size":14705876992,"ftype":"Q3_K - Medium"}}
```

## `/metrics`

Отвечает `501 not_supported_error` с подсказкой «Start it with `--metrics`». `/slots` при этом
включён по умолчанию.

```bash
curl -sS $B/metrics
```

```json
{"error":{"code":501,"message":"This server does not support metrics endpoint. Start it with `--metrics`","type":"not_supported_error"}}
```

## Режим размышлений

При `-rea off` шаблон добавляет пустой блок `<think></think>`; ответ `"391"` занимает 4 токена,
`reasoning_content` в ответе нет. Если в запросе передать
`chat_template_kwargs:{enable_thinking:true}`, размышления включаются (152 токена во втором прогоне,
233 в первом) и приходят отдельно в `reasoning_content`, а не в `content`.

```bash
MT='[{"role":"system","content":"Ты — краткий помощник. Отвечай по-русски."},{"role":"user","content":"Сколько будет 17*23? Ответь только числом."}]'
curl -sS $B/v1/chat/completions -H 'Content-Type: application/json' -d "{\"messages\":$MT,\"max_tokens\":256}"
curl -sS $B/v1/chat/completions -H 'Content-Type: application/json' \
  -d "{\"messages\":$MT,\"max_tokens\":256,\"chat_template_kwargs\":{\"enable_thinking\":true}}"
curl -sS $B/apply-template -H 'Content-Type: application/json' -d "{\"messages\":$MT}"
```

Сырые ответы:

```json
// по умолчанию
{"choices":[{"finish_reason":"stop","index":0,"message":{"role":"assistant","content":"391"}}],"created":1794807801,"model":"/models/Qwen3-30B-A3B-Q3_K_M.gguf","system_fingerprint":"b12110-cd959ba","object":"chat.completion","usage":{"completion_tokens":4,"prompt_tokens":52,"total_tokens":56,"prompt_tokens_details":{"cached_tokens":0}},"id":"chatcmpl-03vTSZumDceFAtPma7M8PAYztWJo0zs9","timings":{…,"predicted_n":4,"predicted_per_second":19.09113471341025}}

// enable_thinking=true (reasoning_content обрезан)
{"message":{"content":"391","reasoning_content":"Хорошо, пользователь спрашивает 17 умножить на 23. Нужно дать только число в ответе. Сначала я должен посчитать это. Дав…"},"usage":{"prompt_tokens":48,"completion_tokens":152}}

// хвост prompt из apply-template
"…числом.<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n"
```

## Прогресс загрузки

В `/health` прогресса загрузки модели нет, только сообщение «Loading model» (см. п. 1). Прогресс
есть лишь у обработки промпта: при `return_progress:true` в потоке приходит
`prompt_progress {total, cache, processed, time_ms}`, по событию на каждый батч в 2048 токенов. На
1544 токенах было два события: 0 и 1544 (12.3 с). На 6044 токенах — четыре: 0, 2048 (14.5 с),
4096 (35.8 с) и 6044 (68.8 с).

```bash
jq -nc --arg u "$(words 6000) Сколько раз повторено слово?" \
  '{messages:[{role:"system",content:"Ты — краткий помощник. Отвечай по-русски."},{role:"user",content:$u}],max_tokens:8,stream:true,return_progress:true}' \
  | curl -sSN $B/v1/chat/completions -H 'Content-Type: application/json' -d @- \
  | while IFS= read -r l; do [ -n "$l" ] && echo "$(date +%T.%3N) $l"; done
```

Сырые ответы (время прихода и поле `prompt_progress` из фрагментов, 6044 токена):

```text
12:43:42.814 {"total":6044,"cache":0,"processed":0,"time_ms":0}
12:43:57.348 {"total":6044,"cache":0,"processed":2048,"time_ms":14532}
12:44:18.632 {"total":6044,"cache":0,"processed":4096,"time_ms":35817}
12:44:51.606 {"total":6044,"cache":0,"processed":6044,"time_ms":68791}

# 1544 токена
{"total":1544,"cache":0,"processed":0,"time_ms":0}
{"total":1544,"cache":0,"processed":1544,"time_ms":12301}
```

