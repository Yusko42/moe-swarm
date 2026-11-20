#!/usr/bin/env bash
# Эксперимент 2.1: как ведёт себя HTTP API llama-server (стенд 1.2-docker-stand).
#
# Использование:
#   ./run.sh              # полный прогон: прошлые результаты -> archive/, ждёт загрузки сервера
#                         # (запускать ДО `docker compose up`), затем все проверки
#   ./run.sh 2 3 ctx      # только выбранные проверки, дописываются к текущим результатам
#
# Проверки: watch (опрос /health при загрузке), 1 (health), 2 (длина входа), 3 (поток), 4 (отмена),
#           5 (подсчёт во время генерации), props, metrics, think (режим размышлений),
#           progress (прогресс промпта, PROGRESS_SIZES="1500 6000"), queue (отключение из очереди),
#           ctx (граница контекста)
#
# Переменные окружения:
#   BASE=http://127.0.0.1:8080   адрес сервера
#   WATCH_TIMEOUT=600            сколько секунд ждать загрузки в режиме watch
#   CTX_TIMEOUT=1800             max-time для запросов на границе контекста (на 30B на CPU это долго)
#   SKIP_CTX=1                   пропустить границу контекста
#
# Результат: results.md (таблица), commands.md (команды), raw/ (сырые ответы).
set -uo pipefail

BASE="${BASE:-http://127.0.0.1:8080}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAW="$DIR/raw"
CMDS="$DIR/commands.md"
SUMMARY="$RAW/summary.tsv"
mkdir -p "$RAW"

command -v curl >/dev/null || { echo "нужен curl" >&2; exit 1; }
command -v jq   >/dev/null || { echo "нужен jq" >&2; exit 1; }

SYS_PROMPT="Ты — краткий помощник. Отвечай по-русски."
USER_PROMPT="Сколько будет 2+2? Ответь одним словом."

ts()  { date +%H:%M:%S.%3N; }
now() { date +%s.%N; }
say() { printf '\n=== %s\n' "$*"; }
# row "№" "Проверка" "Наблюдение"
row() {
    local v=${3//$'\n'/ }; v=${v//$'\t'/ }
    printf '%s\t%s\t%s\n' "$1" "$2" "$v" >> "$SUMMARY"; printf '  -> %s: %s\n' "$2" "$v"
}
jqf() { jq -r "$1" "$2" 2>/dev/null || echo "?"; }

docker_ok() { command -v docker >/dev/null && docker info >/dev/null 2>&1; }
initiator_id() { docker ps -q --filter name=initiator | head -n1; }
cpu_sample() {   # загрузка CPU контейнеров стенда (если доступен docker)
    if docker_ok; then
        docker stats --no-stream --format '{{.Name}} {{.CPUPerc}}' 2>/dev/null | grep -E 'initiator|worker' | tr '\n' ';'
    else
        echo "docker недоступен"
    fi
}

# req NAME METHOD PATH [JSON] [MAXTIME] -> печатает "код время"; сохраняет raw/NAME.{req.json,headers,body}
req() {
    local name=$1 method=$2 path=$3 data=${4-} maxt=${5:-300}
    local args=(-sS -X "$method" -D "$RAW/$name.headers" -o "$RAW/$name.body"
                -w '%{http_code} %{time_total}' --max-time "$maxt")
    local shown="curl -sS -X $method '$BASE$path'"
    if [[ -n $data ]]; then
        printf '%s' "$data" > "$RAW/$name.req.json"
        args+=(-H 'Content-Type: application/json' --data-binary "@$RAW/$name.req.json")
        shown+=" -H 'Content-Type: application/json' -d @raw/$name.req.json"
    fi
    printf -- '- `%s`: `%s`\n' "$name" "$shown" >> "$CMDS"
    local res
    res=$(curl "${args[@]}" "$BASE$path" 2>"$RAW/$name.err") || true
    [[ -s "$RAW/$name.err" ]] || rm -f "$RAW/$name.err"
    echo "$res" > "$RAW/$name.status"
    echo "$res"
}

# stream_bg NAME JSON -> запускает потоковый запрос в фоне, печатает PID; SSE пишется в raw/NAME.sse
stream_bg() {
    local name=$1 data=$2
    printf '%s' "$data" > "$RAW/$name.req.json"
    printf -- '- `%s`: `curl -sSN %s/v1/chat/completions -H "Content-Type: application/json" -d @raw/%s.req.json` (в фоне)\n' \
        "$name" "$BASE" "$name" >> "$CMDS"
    : > "$RAW/$name.sse"
    curl -sSN --max-time 900 -H 'Content-Type: application/json' \
        --data-binary "@$RAW/$name.req.json" "$BASE/v1/chat/completions" \
        > "$RAW/$name.sse" 2>"$RAW/$name.err" &
    echo $!
}
wait_first_chunk() {  # ждёт первого фрагмента в .sse, до 300 c
    local f=$1 i
    for ((i = 0; i < 600; i++)); do
        grep -q '"content"' "$f" 2>/dev/null && return 0
        sleep 0.5
    done
    return 1
}
sse_json() {
    local out
    out=$(sed -n 's/^data: //p' "$1" | { grep -v '^\[DONE\]' || true; } | jq -cs '.' 2>/dev/null)
    [[ -n $out ]] && echo "$out" || echo '[]'
}

msgs() {  # msgs SYS USER -> JSON-массив сообщений
    jq -nc --arg s "$1" --arg u "$2" '[{role:"system",content:$s},{role:"user",content:$u}]'
}
MSGS="$(msgs "$SYS_PROMPT" "$USER_PROMPT")"

# ---------------------------------------------------------------------------
t_watch() {
    say "1 (watch). Опрос /health во время загрузки. Теперь запустите docker compose up"
    local log="$RAW/1-health-watch.log" deadline probed=0 code body
    deadline=$(( $(date +%s) + ${WATCH_TIMEOUT:-600} ))
    : > "$log"
    echo "# время  код  тело" >> "$log"
    printf -- '- `1-health-watch`: `curl -s -w %%{http_code} %s/health` каждые 0.5 c\n' "$BASE" >> "$CMDS"
    local ok_count=0
    while (( $(date +%s) < deadline )); do
        body=$(curl -s --max-time 2 -o - -w '\n%{http_code}' "$BASE/health" 2>/dev/null)
        code=${body##*$'\n'}; body=${body%$'\n'*}
        printf '%s %s %s\n' "$(ts)" "$code" "$body" >> "$log"
        if [[ $code == 503 && $probed == 0 ]]; then
            probed=1
            echo "  сервер отвечает 503, пробую другие эндпоинты во время загрузки"
            req 1-loading-props    GET /props   "" 5 >/dev/null
            req 1-loading-models   GET /v1/models "" 5 >/dev/null
            req 1-loading-slots    GET /slots   "" 5 >/dev/null
            req 1-loading-metrics  GET /metrics "" 5 >/dev/null
            req 1-loading-tokenize POST /tokenize '{"content":"hello"}' 5 >/dev/null
            req 1-loading-chat     POST /v1/chat/completions "{\"messages\":$MSGS,\"max_tokens\":4}" 5 >/dev/null
        fi
        if [[ $code == 200 ]]; then ok_count=$((ok_count + 1)); (( ok_count >= 4 )) && break; fi
        sleep 0.5
    done
    if (( ok_count == 0 )); then
        echo "Сервер не ответил 200 за ${WATCH_TIMEOUT:-600} c — прогон остановлен" >&2
        row 1 "Готовность" "сервер не поднялся за ${WATCH_TIMEOUT:-600} c"
        write_results; exit 1
    fi
    # Сжатая история: смены состояния
    awk 'NR>1 { k=$2" "substr($0, index($0,$3)); if (k!=prev) { print; prev=k } }' "$log" > "$RAW/1-health-transitions.log"
    cat "$RAW/1-health-transitions.log"
    local states; states=$(awk '{print $2}' "$RAW/1-health-transitions.log" | uniq | tr '\n' '>' | sed 's/>$//')
    local loading_bodies; loading_bodies=$(awk '$2=="503"{print substr($0, index($0,$3))}' "$log" | sort -u | head -n3 | tr '\n' ' ')
    row 1 "Готовность при загрузке" "смены кодов: $states; тело 503: ${loading_bodies:-нет}"
    if [[ $probed == 1 ]]; then
        local s=""
        for e in props models slots metrics tokenize chat; do
            s+="$e=$(cut -d' ' -f1 "$RAW/1-loading-$e.status") "
        done
        row 1 "Другие эндпоинты при загрузке" "$s"
    fi
    if grep -qiE 'progress|percent|%' "$log"; then
        row extra "Прогресс загрузки" "в теле /health есть поле прогресса (см. raw/1-health-watch.log)"
    else
        row extra "Прогресс загрузки" "в /health прогресса нет, только код/сообщение"
    fi
}

t1() {
    say "1. Готовность после загрузки"
    local r; r=$(req 1-health GET /health)
    row 1 "Готовность после загрузки" "код ${r%% *}, тело $(tr -d '\n' < "$RAW/1-health.body")"
    printf -- '- `1-health-HEAD`: `curl -sSI %s/health`\n' "$BASE" >> "$CMDS"
    r=$(curl -sSI -o "$RAW/1-health-HEAD.headers" -w '%{http_code} %{time_total}' "$BASE/health" 2>"$RAW/1-health-HEAD.err"); echo "$r" > "$RAW/1-health-HEAD.status"
    row 1 "HEAD /health" "код ${r%% *}"
}

t2() {
    say "2. Точная длина входа"
    local r body
    r=$(req 2-input-tokens POST /v1/chat/completions/input_tokens "{\"messages\":$MSGS}")
    row 2 "input_tokens" "код ${r%% *}, тело $(head -c 300 "$RAW/2-input-tokens.body" | tr -d '\n')"

    r=$(req 2-apply-template POST /apply-template "{\"messages\":$MSGS}")
    row 2 "apply-template" "код ${r%% *}, prompt: $(jqf '.prompt|tojson' "$RAW/2-apply-template.body" | head -c 400)"

    local prompt n_false n_true real
    prompt=$(jq -c '.prompt // ""' "$RAW/2-apply-template.body" 2>/dev/null || echo '""')
    req 2-tokenize-nospecial POST /tokenize "{\"content\":$prompt,\"add_special\":false}" >/dev/null
    req 2-tokenize-special   POST /tokenize "{\"content\":$prompt,\"add_special\":true}"  >/dev/null
    req 2-tokenize-pieces    POST /tokenize "{\"content\":$prompt,\"with_pieces\":true}"  >/dev/null
    n_false=$(jqf '.tokens|length' "$RAW/2-tokenize-nospecial.body")
    n_true=$(jqf '.tokens|length'  "$RAW/2-tokenize-special.body")

    r=$(req 2-real POST /v1/chat/completions "{\"messages\":$MSGS,\"max_tokens\":1}")
    real=$(jqf '.usage.prompt_tokens' "$RAW/2-real.body")
    local it; it=$(jqf '.. | numbers? ' "$RAW/2-input-tokens.body" | head -n1)
    row 2 "Сравнение" "usage.prompt_tokens=$real; tokenize(add_special=false)=$n_false; tokenize(add_special=true)=$n_true; input_tokens=${it:-?}"
    row 2 "Совпадение" "$( [[ $real == "$n_false" ]] && echo 'add_special=false совпадает' || echo 'add_special=false НЕ совпадает' ); $( [[ $real == "$n_true" ]] && echo 'add_special=true совпадает' || echo 'add_special=true НЕ совпадает' ); $( [[ $real == "$it" ]] && echo 'input_tokens совпадает' || echo 'input_tokens не совпадает или нет' )"
    # Время подсчёта в простое: база для проверки 5
    echo "${r#* }" > "$RAW/2-real.time"
}

t3() {
    say "3. Поток"
    local data f arr
    for variant in stop length; do
        local mt=200; [[ $variant == length ]] && mt=5
        data=$(jq -nc --argjson m "$(msgs "$SYS_PROMPT" "Перечисли пять цветов радуги через запятую.")" --argjson mt $mt \
            '{messages:$m, max_tokens:$mt, stream:true, stream_options:{include_usage:true}}')
        printf '%s' "$data" > "$RAW/3-stream-$variant.req.json"
        printf -- '- `3-stream-%s`: `curl -sSN %s/v1/chat/completions -H "Content-Type: application/json" -d @raw/3-stream-%s.req.json`\n' \
            "$variant" "$BASE" "$variant" >> "$CMDS"
        curl -sSN --max-time 300 -D "$RAW/3-stream-$variant.headers" -H 'Content-Type: application/json' \
            --data-binary "@$RAW/3-stream-$variant.req.json" "$BASE/v1/chat/completions" > "$RAW/3-stream-$variant.sse" 2>/dev/null
        f="$RAW/3-stream-$variant.sse"
        arr=$(sse_json "$f"); printf '%s' "$arr" > "$RAW/3-stream-$variant.chunks.json"
        local n done_mark finish last_keys timings usage ctype first
        n=$(jq 'length' <<<"$arr")
        done_mark=$(grep -c '^data: \[DONE\]' "$f")
        finish=$(jq -r '[.[].choices[]?.finish_reason | select(.)] | join(",")' <<<"$arr")
        first=$(jq -c '.[0]' <<<"$arr" | head -c 300)
        last_keys=$(jq -c '.[-1] | keys' <<<"$arr")
        timings=$(jq -c '[.[] | select(.timings)] | last | .timings // "нет"' <<<"$arr")
        usage=$(jq -c '[.[] | select(.usage)] | last | .usage // "нет"' <<<"$arr")
        ctype=$(grep -i '^content-type' "$RAW/3-stream-$variant.headers" | tr -d '\r')
        row 3 "Поток ($variant, max_tokens=$mt)" "фрагментов: $n, [DONE]: $done_mark, $ctype, finish_reason: $finish"
        row 3 "Первый фрагмент ($variant)" "$first"
        row 3 "Последний фрагмент ($variant)" "ключи: $last_keys; usage: $usage"
        row 3 "timings ($variant)" "$timings"
    done
    # Пустые/служебные фрагменты: сколько без content
    local empty; empty=$(jq '[.[] | select((.choices|length)==0 or (.choices[0].delta.content // "") == "")] | length' "$RAW/3-stream-stop.chunks.json")
    row 3 "Фрагменты без текста" "$empty (роль, finish, usage)"
}

LONG_MSGS="$(msgs "$SYS_PROMPT" "Напиши очень длинный подробный рассказ о путешествии на Марс, не меньше 3000 слов.")"
LONG_REQ="$(jq -nc --argjson m "$LONG_MSGS" '{messages:$m, max_tokens:2000, stream:true}')"

slot_info() {  # slot_info FILE -> краткая сводка /slots
    jq -r '[.[] | {id, is_processing, n_decoded: (.next_token[0].n_decoded? // .n_decoded? // null), n_remain: (.next_token[0].n_remain? // null)}] | tostring' "$1" 2>/dev/null || echo "?"
}

t4() {
    say "4. Отмена"
    local pid t0 metrics_before
    req 4-metrics-before GET /metrics >/dev/null
    t0=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    pid=$(stream_bg 4-stream "$LONG_REQ")
    if ! wait_first_chunk "$RAW/4-stream.sse"; then row 4 "Отмена" "не дождались первого фрагмента"; kill "$pid" 2>/dev/null; return; fi
    sleep 4
    req 4-slots-before GET /slots >/dev/null
    local cpu_before; cpu_before=$(cpu_sample)
    local chunks_before; chunks_before=$(grep -c '^data:' "$RAW/4-stream.sse")
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
    local t_kill; t_kill=$(ts)
    sleep 1;  req 4-slots-after-1s GET /slots >/dev/null
    sleep 3;  req 4-slots-after-4s GET /slots >/dev/null
    local cpu_after; cpu_after=$(cpu_sample)
    req 4-metrics-after GET /metrics >/dev/null
    local r; r=$(req 4-next POST /v1/chat/completions "{\"messages\":$MSGS,\"max_tokens\":8}" 120)
    row 4 "Перед отменой (получено фрагментов: $chunks_before)" "slots: $(slot_info "$RAW/4-slots-before.body"); CPU: $cpu_before"
    row 4 "Через 1 c после закрытия ($t_kill)" "slots: $(slot_info "$RAW/4-slots-after-1s.body")"
    row 4 "Через 4 c" "slots: $(slot_info "$RAW/4-slots-after-4s.body"); CPU: $cpu_after"
    row 4 "Следующий запрос" "код ${r%% *}, время ${r#* } c, ответ: $(jqf '.choices[0].message.content' "$RAW/4-next.body" | head -c 80 | tr '\n' ' ')"
    if docker_ok && [[ -n $(initiator_id) ]]; then
        docker logs --since "$t0" "$(initiator_id)" > "$RAW/4-server.log" 2>&1
        local cancel_lines; cancel_lines=$(grep -iE 'cancel|disconnect|connection closed|stop' "$RAW/4-server.log" | grep -viE 'stop_|stopping word' | head -n5 | tr '\n' ' ' | head -c 500)
        row 4 "Лог сервера" "${cancel_lines:-строк про отмену не найдено (raw/4-server.log)}"
    fi
}

t5() {
    say "5. Подсчёт во время генерации"
    local pid r
    # База в простое
    r=$(req 5-idle-tokenize POST /tokenize '{"content":"Сколько будет 2+2?"}'); local idle_tok=${r#* }
    r=$(req 5-idle-input-tokens POST /v1/chat/completions/input_tokens "{\"messages\":$MSGS}"); local idle_it=${r#* }

    pid=$(stream_bg 5-stream "$LONG_REQ")
    if ! wait_first_chunk "$RAW/5-stream.sse"; then row 5 "Подсчёт" "не дождались первого фрагмента"; kill "$pid" 2>/dev/null; return; fi
    sleep 2
    local s=""
    r=$(req 5-busy-tokenize       POST /tokenize '{"content":"Сколько будет 2+2?"}' 60);              s+="tokenize: ${r} (простой $idle_tok); "
    r=$(req 5-busy-apply-template POST /apply-template "{\"messages\":$MSGS}" 60);                    s+="apply-template: ${r}; "
    r=$(req 5-busy-input-tokens   POST /v1/chat/completions/input_tokens "{\"messages\":$MSGS}" 60);  s+="input_tokens: ${r} (простой $idle_it); "
    r=$(req 5-busy-health         GET /health "" 60);                                                 s+="health: ${r}; "
    r=$(req 5-busy-props          GET /props "" 60);                                                  s+="props: ${r}; "
    r=$(req 5-busy-slots          GET /slots "" 60);                                                  s+="slots: ${r}; "
    row 5 "Служебные запросы во время генерации (код время_с)" "$s"
    # Второй чат при n_parallel=1 должен ждать; ограничим 10 c
    r=$(req 5-busy-chat POST /v1/chat/completions "{\"messages\":$MSGS,\"max_tokens\":4}" 10)
    row 5 "Второй чат во время генерации (max-time 10 c)" "код ${r%% *}, время ${r#* } c (000 = не дождались, т.е. в очереди)"
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
}

t_props() {
    say "Доп. /props"
    local r; r=$(req props GET /props)
    row props "/props" "код ${r%% *}; n_ctx=$(jqf '.default_generation_settings.n_ctx' "$RAW/props.body"); total_slots=$(jqf '.total_slots' "$RAW/props.body"); build=$(jqf '.build_info' "$RAW/props.body"); model=$(jqf '.model_path' "$RAW/props.body")"
    row props "Ключи /props" "$(jqf 'keys|join(",")' "$RAW/props.body")"
    row props "Шаблон: упоминание enable_thinking" "$(jqf '.chat_template | test("enable_thinking")' "$RAW/props.body")"
    req models GET /v1/models >/dev/null
    row props "/v1/models" "$(jq -c '.data[0] | {id, meta}' "$RAW/models.body" 2>/dev/null | head -c 400)"
}

t_metrics() {
    say "Доп. /metrics"
    local r; r=$(req metrics GET /metrics)
    if [[ ${r%% *} == 200 ]]; then
        row metrics "/metrics" "включён; метрики: $(grep -v '^#' "$RAW/metrics.body" | awk '{print $1}' | tr '\n' ' ' | head -c 600)"
    else
        row metrics "/metrics" "код ${r%% *}: $(head -c 200 "$RAW/metrics.body" | tr -d '\n'). Включается LLAMA_ARG_ENDPOINT_METRICS=1 (--metrics)"
    fi
    r=$(req slots GET /slots)
    row metrics "/slots" "код ${r%% *}; $(slot_info "$RAW/slots.body")"
}

t_think() {
    say "Доп. режим размышлений"
    local q; q=$(msgs "$SYS_PROMPT" "Сколько будет 17*23? Ответь только числом.")
    local r
    r=$(req think-default POST /v1/chat/completions "$(jq -nc --argjson m "$q" '{messages:$m, max_tokens:256}')")
    row think "По умолчанию (-rea off)" "код ${r%% *}; reasoning_content: $(jqf '.choices[0].message.reasoning_content // "нет"|tostring|.[0:80]' "$RAW/think-default.body"); <think> в content: $(jqf '.choices[0].message.content|test("<think>")' "$RAW/think-default.body"); content: $(jqf '.choices[0].message.content|.[0:120]|tojson' "$RAW/think-default.body"); completion_tokens=$(jqf '.usage.completion_tokens' "$RAW/think-default.body")"
    r=$(req think-kwargs-on POST /v1/chat/completions "$(jq -nc --argjson m "$q" '{messages:$m, max_tokens:256, chat_template_kwargs:{enable_thinking:true}}')")
    row think "chat_template_kwargs.enable_thinking=true" "код ${r%% *}; reasoning_content: $(jqf '.choices[0].message.reasoning_content // "нет"|tostring|.[0:80]|tojson' "$RAW/think-kwargs-on.body"); <think> в content: $(jqf '.choices[0].message.content|test("<think>")' "$RAW/think-kwargs-on.body"); completion_tokens=$(jqf '.usage.completion_tokens' "$RAW/think-kwargs-on.body")"
    req think-template POST /apply-template "{\"messages\":$q}" >/dev/null
    row think "Хвост шаблона (apply-template)" "$(jqf '.prompt|.[-60:]|tojson' "$RAW/think-template.body")"
}

# Строит JSON-запрос с user-сообщением из N повторов слова
words() { yes "hello" | head -n "$1" | tr '\n' ' '; }
prompt_len() {  # prompt_len N -> число токенов входа после шаблона
    local m; m=$(jq -nc --arg s "$SYS_PROMPT" --arg u "$(words "$1")" '[{role:"system",content:$s},{role:"user",content:$u}]')
    curl -sS -H 'Content-Type: application/json' -d "{\"messages\":$m}" "$BASE/apply-template" \
        | jq -c '{content:.prompt, add_special:false}' \
        | curl -sS -H 'Content-Type: application/json' --data-binary @- "$BASE/tokenize" | jq '.tokens|length'
}
fit_words() {  # fit_words TARGET -> число слов, дающее ~TARGET токенов
    local target=$1 n=1000 c ratio
    c=$(prompt_len $n)
    n=$(awk -v t="$target" -v c="$c" -v n="$n" 'BEGIN{ printf "%d", n * (t - 20) / (c - 20) }')
    c=$(prompt_len "$n")
    n=$(( n + target - c ))   # для "hello " ≈ 1 токен на слово; подправляем точно
    echo "$n"
}

t_progress() {
    say "Доп. прогресс обработки промпта (return_progress)"
    local m data size name arr
    for size in ${PROGRESS_SIZES:-1500 6000}; do
        name="progress-$size"
        m=$(jq -nc --arg s "$SYS_PROMPT" --arg u "$(words "$size") Сколько раз повторено слово?" '[{role:"system",content:$s},{role:"user",content:$u}]')
        data=$(jq -nc --argjson m "$m" '{messages:$m, max_tokens:8, stream:true, return_progress:true}')
        printf '%s' "$data" > "$RAW/$name.req.json"
        printf -- '- `%s`: `curl -sSN %s/v1/chat/completions -d @raw/%s.req.json`\n' "$name" "$BASE" "$name" >> "$CMDS"
        # Каждой строке SSE ставим метку времени прихода, чтобы видеть реальный шаг прогресса
        curl -sSN --max-time "${CTX_TIMEOUT:-1800}" -H 'Content-Type: application/json' --data-binary "@$RAW/$name.req.json" \
            "$BASE/v1/chat/completions" 2>/dev/null \
            | while IFS= read -r line; do [[ -n $line ]] && printf '%s %s\n' "$(ts)" "$line"; done > "$RAW/$name.timed.sse"
        sed 's/^[^ ]* //' "$RAW/$name.timed.sse" > "$RAW/$name.sse"
        arr=$(sse_json "$RAW/$name.sse")
        row progress "return_progress=true, ~$size слов" "событий prompt_progress: $(jq '[.[]|select(.prompt_progress)]|length' <<<"$arr"); processed по шагам: $(jq -c '[.[]|select(.prompt_progress)|.prompt_progress|[.processed,.total,.time_ms]]' <<<"$arr" | head -c 500)"
    done
}

t_queue() {
    say "Доп. клиент отключился, пока его запрос ждал в очереди"
    # A — длинная генерация, занимает единственный слот.
    # B — длинный запрос (max_tokens=400), встаёт в очередь и отключается через 5 c, не дождавшись слота.
    # C — короткий запрос (max_tokens=8), встаёт в очередь после B и ждёт.
    # Затем A отменяется. Если B выкинут из очереди, C отработает сразу (~1–2 c);
    # если B всё равно выполняется, C будет ждать, пока сгенерируются 400 токенов B.
    local t0 pid_a pid_b pid_c c_start t_kill
    t0=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    pid_a=$(stream_bg queue-A "$LONG_REQ")
    if ! wait_first_chunk "$RAW/queue-A.sse"; then row queue "Очередь" "не дождались первого фрагмента A"; kill "$pid_a" 2>/dev/null; return; fi

    local data_b; data_b=$(jq -nc --argjson m "$(msgs "$SYS_PROMPT" "Расскажи подробно историю Древнего Рима.")" '{messages:$m, max_tokens:400, stream:true}')
    printf '%s' "$data_b" > "$RAW/queue-B.req.json"
    printf -- '- `queue-B`: `curl -sSN --max-time 5 %s/v1/chat/completions -d @raw/queue-B.req.json` (в фоне)\n' "$BASE" >> "$CMDS"
    curl -sSN --max-time 5 -H 'Content-Type: application/json' --data-binary "@$RAW/queue-B.req.json" \
        -w '\n#curl %{http_code} %{time_total}\n' "$BASE/v1/chat/completions" > "$RAW/queue-B.sse" 2>"$RAW/queue-B.err" &
    pid_b=$!
    sleep 1

    local data_c; data_c="{\"messages\":$MSGS,\"max_tokens\":8}"
    printf '%s' "$data_c" > "$RAW/queue-C.req.json"
    printf -- '- `queue-C`: `curl -sS %s/v1/chat/completions -d @raw/queue-C.req.json` (в фоне)\n' "$BASE" >> "$CMDS"
    c_start=$(now)
    curl -sS --max-time 600 -H 'Content-Type: application/json' --data-binary "@$RAW/queue-C.req.json" \
        -o "$RAW/queue-C.body" -w '%{http_code} %{time_total}' "$BASE/v1/chat/completions" > "$RAW/queue-C.status" 2>"$RAW/queue-C.err" &
    pid_c=$!

    wait "$pid_b" 2>/dev/null
    sleep 1
    kill "$pid_a" 2>/dev/null; wait "$pid_a" 2>/dev/null
    t_kill=$(now)
    local s="" i
    for i in 0.5 2 5; do
        sleep "$i"
        req "queue-slots-$i" GET /slots >/dev/null
        s+="+${i}c: $(slot_info "$RAW/queue-slots-$i.body"); "
    done
    wait "$pid_c" 2>/dev/null
    local c_res c_after
    c_res=$(cat "$RAW/queue-C.status")
    c_after=$(awk -v s="$c_start" -v k="$t_kill" -v t="${c_res#* }" 'BEGIN{ printf "%.1f", s + t - k }')
    row queue "B (в очереди, max-time 5 c)" "получено data-строк: $(grep -c '^data:' "$RAW/queue-B.sse"); $(grep '^#curl' "$RAW/queue-B.sse")"
    row queue "/slots после отмены A" "$s"
    row queue "C после отмены A" "код ${c_res%% *}, всего ${c_res#* } c, из них после отмены A: ${c_after} c; ответ: $(jqf '.choices[0].message.content' "$RAW/queue-C.body" | head -c 60)"
    local b_data; b_data=$(grep -c '^data:' "$RAW/queue-B.sse")
    local verdict
    if (( b_data > 0 )); then
        verdict="B получил данные, т.е. не стоял в очереди — проверка недостоверна"
    else
        verdict=$(awk -v a="$c_after" 'BEGIN{
            if (a < 0)      print "C завершился до отмены A, т.е. не стоял в очереди — проверка недостоверна";
            else if (a < 8) print "C отработал сразу после отмены A: B, похоже, выкинут из очереди";
            else            print "C ждал после отмены A: B, похоже, выполнялся без клиента" }')
    fi
    row queue "Вывод (авто)" "$verdict"
    if docker_ok && [[ -n $(initiator_id) ]]; then
        docker logs --since "$t0" "$(initiator_id)" > "$RAW/queue-server.log" 2>&1
        row queue "Лог сервера" "$(grep -iE 'cancel|launch|release|defer|queue' "$RAW/queue-server.log" | head -n 12 | tr '\n' ' ' | head -c 900)"
    fi
}

t_ctx() {
    [[ ${SKIP_CTX:-0} == 1 ]] && { echo "пропуск ctx"; return; }
    say "Доп. граница контекста (долго на большой модели)"
    local n_ctx; n_ctx=$(curl -sS "$BASE/props" | jq '.default_generation_settings.n_ctx')
    [[ $n_ctx =~ ^[0-9]+$ ]] || { row ctx "Граница контекста" "не удалось узнать n_ctx"; return; }
    local maxt=${CTX_TIMEOUT:-1800} n m r

    # a) вход больше контекста
    n=$(fit_words $(( n_ctx + 200 )))
    m=$(jq -nc --arg s "$SYS_PROMPT" --arg u "$(words "$n")" '[{role:"system",content:$s},{role:"user",content:$u}]')
    r=$(req ctx-over POST /v1/chat/completions "$(jq -nc --argjson m "$m" '{messages:$m, max_tokens:16}')" "$maxt")
    row ctx "Вход > n_ctx ($(prompt_len "$n") > $n_ctx)" "код ${r%% *}, время ${r#* } c, тело: $(head -c 300 "$RAW/ctx-over.body" | tr -d '\n')"

    # b) вход ровно n_ctx
    n=$(fit_words "$n_ctx")
    m=$(jq -nc --arg s "$SYS_PROMPT" --arg u "$(words "$n")" '[{role:"system",content:$s},{role:"user",content:$u}]')
    r=$(req ctx-equal POST /v1/chat/completions "$(jq -nc --argjson m "$m" '{messages:$m, max_tokens:16}')" "$maxt")
    row ctx "Вход = n_ctx ($(prompt_len "$n"))" "код ${r%% *}; $(jq -c '{finish: .choices[0].finish_reason, usage, error}' "$RAW/ctx-equal.body" 2>/dev/null | head -c 300)"

    # c) вход n_ctx-30, max_tokens 200: упрётся ли генерация в контекст
    n=$(fit_words $(( n_ctx - 30 )))
    m=$(jq -nc --arg s "$SYS_PROMPT" --arg u "$(words "$n") Напиши длинный рассказ." '[{role:"system",content:$s},{role:"user",content:$u}]')
    r=$(req ctx-near POST /v1/chat/completions "$(jq -nc --argjson m "$m" '{messages:$m, max_tokens:200}')" "$maxt")
    row ctx "Вход n_ctx-30, max_tokens=200" "код ${r%% *}; $(jq -c '{finish: .choices[0].finish_reason, usage, error}' "$RAW/ctx-near.body" 2>/dev/null | head -c 300)"
}

# ---------------------------------------------------------------------------
write_results() {
    {
        echo "# 2.1. Проверка API llama-server"
        echo
        echo "Сервер: \`$BASE\`, запуск: $(date '+%Y-%m-%d %H:%M')"
        echo
        echo "| № | Проверка | Наблюдение |"
        echo "|---|----------|------------|"
        sed 's/|/\\|/g' "$SUMMARY" | awk -F'\t' '{printf "| %s | %s | %s |\n", $1, $2, $3}'
        echo
        echo "Команды: [commands.md](commands.md), сырые ответы: [raw/](raw/)."
    } > "$DIR/results.md"
}

archive_previous() {
    [[ -d $RAW && -n $(ls -A "$RAW" 2>/dev/null) ]] || return 0
    local dst; dst="$DIR/archive/$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$dst"
    mv "$RAW" "$dst/raw"
    [[ -f $DIR/results.md ]] && mv "$DIR/results.md" "$dst/"
    [[ -f $CMDS ]] && mv "$CMDS" "$dst/"
    mkdir -p "$RAW"
    echo "Прошлые результаты перенесены в $dst"
}

main() {
    local tests=("$@")
    if [[ ${#tests[@]} -eq 0 ]]; then
        # Полный прогон: начисто, с ожиданием загрузки сервера
        archive_previous
        tests=(watch props 1 2 3 think metrics progress 4 5 queue ctx)
        echo "Полный прогон. Если стенд ещё не запущен, поднимите его в другом терминале:"
        echo "  cd ../1.2-docker-stand && MODEL=... docker compose up --build"
    fi
    [[ -f $CMDS ]] || printf '# Команды эксперимента 2.1\n\nСырые ответы лежат в raw/<имя>.{req.json,headers,body,status,sse}.\n\n' > "$CMDS"
    for t in "${tests[@]}"; do
        case $t in
            watch) t_watch ;;
            1) t1 ;; 2) t2 ;; 3) t3 ;; 4) t4 ;; 5) t5 ;;
            props) t_props ;; metrics) t_metrics ;; think) t_think ;; progress) t_progress ;; ctx) t_ctx ;;
            queue) t_queue ;;
            *) echo "неизвестная проверка: $t" >&2 ;;
        esac
    done
    write_results
    say "Готово: $DIR/results.md"
    cat "$DIR/results.md"
    echo
    echo "Архив для отправки: tar czf 2.1-results.tgz -C '$DIR' results.md commands.md raw"
}

main "$@"
