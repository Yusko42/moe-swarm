# Запуск нейронки

```sh
WORKER_MEMORY=8g INITIATOR_MEMORY=8g \
MODEL=Qwen3-30B-A3B-Q3_K_M.gguf WORKERS=2 docker compose up --build
```

- Панель сервера висит на 127.0.0.1:8080, поднимается 20 секунд
- Детальный конфиг прописан в compose, но он собирался по ФТ/НФТ и Task #19160
- Скорость генерации ответа на 341 токен: 16.51 т/с
- Скорость колеблется между 17 и 15.5

# Раскладка слоёв: -ngl 32 -ts 1,1

Ожидание: 32 последних слоя уходят на двух соседей поровну, первые 16 остаются у инициатора.

Итог: почти подтверждается. `-ngl` считает 49 частей (48 слоёв и выходной слой) и
отдаёт их с конца, начиная с выходного. Поэтому соседям уходят выходной слой и 31 слой,
а у инициатора остаётся 17 слоёв.

| Устройство       | Слои             | Веса, МиБ   | KV, МиБ | Буферы, МиБ |
|------------------|------------------|-------------|---------|-------------|
| Инициатор        | 0–16, эмбеддинги | 3413 + 1597 | 272     | 96          |
| Сосед 172.18.0.3 | 17–32            | 4527        | 256     | 100         |
| Сосед 172.18.0.4 | 33–47, выходной  | 4488        | 240     | 85          |

- Для раскладки «16 у инициатора, по 16 у соседей» нужен `-ngl 33`.
- В строках `load_tensors`, `llama_kv_cache` и `sched_reserve` оба соседа подписаны `RPC0`.
  Это ошибка подписи в llama.cpp, соседей различаем по адресу.

# Память контейнеров: memory.peak, memory.events

Собрано после загрузки модели и нескольких запросов в чат:

```bash
for c in $(docker compose ps -q); do
  echo "== $(docker inspect -f '{{.Name}}' $c)"
  docker exec $c cat /sys/fs/cgroup/memory.peak /sys/fs/cgroup/memory.events
done
```

| Контейнер   | memory.peak | Ожидание по логу llama.cpp | max | oom_kill | sock_throttled |
|-------------|-------------|----------------------------|-----|----------|----------------|
| initiator-1 | 7.31 ГиБ    | 3780 + 1596 = 5.25 ГиБ     | 0   | 0        | 0              |
| worker-1    | 4.82 ГиБ    | 4.77 ГиБ                   | 0   | 0        | 0              |
| worker-2    | 4.73 ГиБ    | 4.70 ГиБ                   | 0   | 0        | 0              |

- Ни один контейнер не упирался в лимит: `max`, `oom_kill` и `sock_throttled` равны 0.
- Инициатор: пик на ~2 ГиБ выше расчёта. В `memory.peak` входит page cache: без mmap
  модель (13.7 ГиБ) читается с диска через него целиком, включая слои соседей.
- С лимитом 6 ГиБ инициатор упирался в лимит (`max` 47 004, `sock_throttled` 201) без OOM:
  ядро вытесняло page cache.

# Чтение с диска во время генерации

Счётчики сняты до и после запроса в чат:

```bash
snap() { for c in $(docker compose ps -q); do
  echo "$(docker inspect -f '{{.Name}}' $c) \
$(docker exec $c sh -c "grep -h pgmajfault /sys/fs/cgroup/memory.stat; awk '{for(i=1;i<=NF;i++) if(\$i ~ /^rbytes=/) print}' /sys/fs/cgroup/io.stat")"
done; }
snap > before.txt
# ... запрос в чат ...
snap > after.txt
diff before.txt after.txt
```

Итог: `diff` пустой - с диска никто не читал

То, что в файлах было записано ниже:

| Контейнер   | pgmajfault | rbytes          | rios    |
|-------------|------------|-----------------|---------|
| initiator-1 | 19         | 12.9 ГиБ        | 106 817 |
| worker-1    | 1          | 32 768          | 1       |
| worker-2    | 0          | диск не читался | —       |

Всё чтение у инициатора пришлось на загрузку модели. Прочитано 12.9 ГиБ при файле
13.7 ГиБ: часть файла, видимо, уже была в page cache хоста с прошлого запуска.

# Падение соседа

## Случай 1: процесс соседа умер

```bash
docker kill moe-swarm-stand-worker-2
```

Итог: падает весь кластер. Инициатор не возвращает ошибку на запрос, а аварийно
завершает llama-server целиком (`GGML_ABORT`).

```
initiator-1  | /src/ggml/src/ggml-rpc/ggml-rpc.cpp:685: Remote RPC server crashed or returned malformed response
initiator-1  | 44.55.043.439 E recv failed (bytes_recv=0, size_to_recv=8)
initiator-1  | llama-server(+0xbce0e6) [0x56261d5360e6]
initiator-1  | ...
initiator-1  | /usr/lib/x86_64-linux-gnu/libc.so.6(__clone+0x44) [0x7f38556af3d4]
```

## Случай 2: сосед пропал молча

```bash
docker pause moe-swarm-stand-worker-2
docker unpause moe-swarm-stand-worker-2
```

Итог: генерация зависает без ошибки. В логе инициатора ничего не появляется,
вывод ответа останавливается. После `unpause` работа сразу продолжается с того же места.

## Случай 3: выдернутый кабель

```bash
docker network disconnect moe-swarm-stand_default moe-swarm-stand-worker-2
docker network connect moe-swarm-stand_default moe-swarm-stand-worker-2
```

Итог: как в случае 2, генерация зависает без ошибки, но после подключения
продолжается не сразу, а с задержкой. Чем дольше сосед был отключён, тем больше
задержка. отключение на 2 минуты, генерация продолжилась примерно через 27 с после
подключения.

# Сырые логи

```
initiator-1  | Исполнители: 172.18.0.3:50052,172.18.0.4:50052
initiator-1  | 0.00.001.594 I srv  llama_server: initializing ...
initiator-1  | 0.00.001.599 I cmn  common_param: common_params_print_info: build 12110 (cd959ba) with GNU 15.2.0 for Linux x86_64
initiator-1  | 0.00.001.600 I cmn  common_param: common_params_print_info: verbosity = 4 (adjust with the `-lv N` CLI arg)
initiator-1  | 0.00.001.600 I cmn  common_param: device_info:
initiator-1  | 0.00.001.603 I cmn  common_param:   - CPU     : AMD Ryzen 9 9950X3D 16-Core Processor (30972 MiB, 30972 MiB free)
initiator-1  | 0.00.001.757 I cmn  common_param:   - RPC0    : 172.18.0.3:50052 (30972 MiB, 30972 MiB free)
initiator-1  | 0.00.001.836 I cmn  common_param:   - RPC1    : 172.18.0.4:50052 (30972 MiB, 30972 MiB free)
initiator-1  | 0.00.001.864 I cmn  common_param: system_info: n_threads = 2 (n_threads_batch = 2) / 32 | CPU : SSE3 = 1 | SSSE3 = 1 | AVX = 1 | AVX_VNNI = 1 | AVX2 = 1 | F16C = 1 | FMA = 1 | BMI2 = 1 | AVX512 = 1 | AVX512_VBMI = 1 | AVX512_VNNI = 1 | AVX512_BF16 = 1 | LLAMAFILE = 1 | OPENMP = 1 | REPACK = 1 |
initiator-1  | 0.00.002.006 I srv          init: using 31 threads for HTTP server
initiator-1  | 0.00.002.026 W srv  server_setup: security: no API key is set and CORS allows all origins (see https://github.com/ggml-org/llama.cpp/pull/25655)
initiator-1  | 0.00.003.485 I srv    load_model: loading model '/models/Qwen3-30B-A3B-Q3_K_M.gguf'
initiator-1  | 0.00.003.486 I srv    load_model: local path '/models/Qwen3-30B-A3B-Q3_K_M.gguf'
initiator-1  | 0.00.003.490 I cmn  common_fit_a: fitting params to device memory ...
initiator-1  | 0.00.003.490 I cmn  common_fit_a: (for bugs during this step try to reproduce them with -fit off, or provide --verbose logs if the bug only occurs with -fit on)
initiator-1  | 0.00.003.510 I common_params_fit_impl: getting device memory data for initial parameters:
initiator-1  | 0.00.069.349 I common_memory_breakdown_print: | memory breakdown [MiB]      | total    free    self   model   context   compute    unaccounted |
initiator-1  | 0.00.069.350 I common_memory_breakdown_print: |   - RPC0 (172.18.0.3:50052) | 30972 = 30972 + (4883 =  4527 +     256 +     100) +       -4883 |
initiator-1  | 0.00.069.350 I common_memory_breakdown_print: |   - RPC1 (172.18.0.4:50052) | 30972 = 30972 + (4812 =  4487 +     240 +      84) +       -4812 |
initiator-1  | 0.00.069.350 I common_memory_breakdown_print: |   - Host                    |                  3780 =  3412 +     272 +      96                |
initiator-1  | 0.00.069.350 I common_memory_breakdown_print: |   - CPU_REPACK              |                  1596 =  1596 +       0 +       0                |
initiator-1  | 0.00.076.773 I common_params_fit_impl: projected memory use with initial parameters [MiB]:
initiator-1  | 0.00.076.776 I common_params_fit_impl:   - RPC0 (172.18.0.3:50052):  30972 total,   4883 used,  26088 free vs. target of   1024
initiator-1  | 0.00.076.776 I common_params_fit_impl:   - RPC1 (172.18.0.4:50052):  30972 total,   4812 used,  26159 free vs. target of   1024
initiator-1  | 0.00.076.776 I common_params_fit_impl: projected to use 9695 MiB of device memory vs. 61944 MiB of free device memory
initiator-1  | 0.00.076.777 I common_params_fit_impl: targets for free memory can be met on all devices, no changes needed
initiator-1  | 0.00.076.777 I common_fit_params: successfully fit params to free device memory
initiator-1  | 0.00.076.780 I common_fit_params: fitting params to free memory took 0.07 seconds
initiator-1  | 0.00.091.428 I llama_model_loader: loaded meta data with 35 key-value pairs and 579 tensors from /models/Qwen3-30B-A3B-Q3_K_M.gguf (version GGUF V3 (latest))
initiator-1  | 0.00.091.435 I llama_model_loader: Dumping metadata keys/values. Note: KV overrides do not apply in this output.
initiator-1  | 0.00.091.437 I llama_model_loader: - kv   0:                       general.architecture str              = qwen3moe
initiator-1  | 0.00.091.438 I llama_model_loader: - kv   1:                               general.type str              = model
initiator-1  | 0.00.091.438 I llama_model_loader: - kv   2:                               general.name str              = Qwen3-30B-A3B
initiator-1  | 0.00.091.438 I llama_model_loader: - kv   3:                           general.basename str              = Qwen3-30B-A3B
initiator-1  | 0.00.091.438 I llama_model_loader: - kv   4:                       general.quantized_by str              = Unsloth
initiator-1  | 0.00.091.438 I llama_model_loader: - kv   5:                         general.size_label str              = 30B-A3B
initiator-1  | 0.00.091.439 I llama_model_loader: - kv   6:                           general.repo_url str              = https://huggingface.co/unsloth
initiator-1  | 0.00.091.440 I llama_model_loader: - kv   7:                       qwen3moe.block_count u32              = 48
initiator-1  | 0.00.091.440 I llama_model_loader: - kv   8:                    qwen3moe.context_length u32              = 40960
initiator-1  | 0.00.091.440 I llama_model_loader: - kv   9:                  qwen3moe.embedding_length u32              = 2048
initiator-1  | 0.00.091.440 I llama_model_loader: - kv  10:               qwen3moe.feed_forward_length u32              = 6144
initiator-1  | 0.00.091.441 I llama_model_loader: - kv  11:              qwen3moe.attention.head_count u32              = 32
initiator-1  | 0.00.091.441 I llama_model_loader: - kv  12:           qwen3moe.attention.head_count_kv u32              = 4
initiator-1  | 0.00.091.444 I llama_model_loader: - kv  13:                    qwen3moe.rope.freq_base f32              = 1000000.000000
initiator-1  | 0.00.091.445 I llama_model_loader: - kv  14:  qwen3moe.attention.layer_norm_rms_epsilon f32              = 0.000001
initiator-1  | 0.00.091.445 I llama_model_loader: - kv  15:                 qwen3moe.expert_used_count u32              = 8
initiator-1  | 0.00.091.446 I llama_model_loader: - kv  16:              qwen3moe.attention.key_length u32              = 128
initiator-1  | 0.00.091.446 I llama_model_loader: - kv  17:            qwen3moe.attention.value_length u32              = 128
initiator-1  | 0.00.091.446 I llama_model_loader: - kv  18:                      qwen3moe.expert_count u32              = 128
initiator-1  | 0.00.091.446 I llama_model_loader: - kv  19:        qwen3moe.expert_feed_forward_length u32              = 768
initiator-1  | 0.00.091.446 I llama_model_loader: - kv  20:                       tokenizer.ggml.model str              = gpt2
initiator-1  | 0.00.091.447 I llama_model_loader: - kv  21:                         tokenizer.ggml.pre str              = qwen2
initiator-1  | 0.00.091.448 I llama_model_loader: - kv  22:                      tokenizer.ggml.tokens arr[str,151936]  = ["!", "\"", "#", "$", "%", "&", "'", ...
initiator-1  | 0.00.091.449 I llama_model_loader: - kv  23:                  tokenizer.ggml.token_type arr[i32,151936]  = [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, ...
initiator-1  | 0.00.091.450 I llama_model_loader: - kv  24:                      tokenizer.ggml.merges arr[str,151387]  = ["Ġ Ġ", "ĠĠ ĠĠ", "i n", "Ġ t",...
initiator-1  | 0.00.091.450 I llama_model_loader: - kv  25:                tokenizer.ggml.eos_token_id u32              = 151645
initiator-1  | 0.00.091.451 I llama_model_loader: - kv  26:            tokenizer.ggml.padding_token_id u32              = 151654
initiator-1  | 0.00.091.451 I llama_model_loader: - kv  27:               tokenizer.ggml.add_bos_token bool             = false
initiator-1  | 0.00.091.452 I llama_model_loader: - kv  28:                    tokenizer.chat_template str              = {%- if tools %}\n    {{- '<|im_start|>...
initiator-1  | 0.00.091.452 I llama_model_loader: - kv  29:               general.quantization_version u32              = 2
initiator-1  | 0.00.091.452 I llama_model_loader: - kv  30:                          general.file_type u32              = 12
initiator-1  | 0.00.091.452 I llama_model_loader: - kv  31:                      quantize.imatrix.file str              = Qwen3-30B-A3B-GGUF/imatrix_unsloth.dat
initiator-1  | 0.00.091.453 I llama_model_loader: - kv  32:                   quantize.imatrix.dataset str              = unsloth_calibration_Qwen3-30B-A3B.txt
initiator-1  | 0.00.091.453 I llama_model_loader: - kv  33:             quantize.imatrix.entries_count i32              = 384
initiator-1  | 0.00.091.453 I llama_model_loader: - kv  34:              quantize.imatrix.chunks_count i32              = 685
initiator-1  | 0.00.091.453 I llama_model_loader: - type  f32:  241 tensors
initiator-1  | 0.00.091.454 I llama_model_loader: - type q3_K:  193 tensors
initiator-1  | 0.00.091.454 I llama_model_loader: - type q4_K:  139 tensors
initiator-1  | 0.00.091.454 I llama_model_loader: - type q5_K:    5 tensors
initiator-1  | 0.00.091.454 I llama_model_loader: - type q6_K:    1 tensors
initiator-1  | 0.00.091.455 I print_info: file format = GGUF V3 (latest)
initiator-1  | 0.00.091.456 I print_info: file type   = Q3_K - Medium
initiator-1  | 0.00.091.456 I print_info: file size   = 13.70 GiB (3.85 BPW)
initiator-1  | 0.00.091.756 I llama_prepare_model_devices: using device RPC0 (172.18.0.3:50052) (unknown id) - 30972 MiB free
initiator-1  | 0.00.091.968 I llama_prepare_model_devices: using device RPC1 (172.18.0.4:50052) (unknown id) - 30972 MiB free
initiator-1  | 0.00.114.290 I load: 0 unused tokens
initiator-1  | 0.00.114.724 W load: control-looking token: 128247 '</s>' was not control-type; this is probably a bug in the model. its type will be overridden
initiator-1  | 0.00.121.620 I load: printing all EOG tokens:
initiator-1  | 0.00.121.621 I load:   - 128247 ('</s>')
initiator-1  | 0.00.121.621 I load:   - 151643 ('<|endoftext|>')
initiator-1  | 0.00.121.621 I load:   - 151645 ('<|im_end|>')
initiator-1  | 0.00.121.622 I load:   - 151662 ('<|fim_pad|>')
initiator-1  | 0.00.121.622 I load:   - 151663 ('<|repo_name|>')
initiator-1  | 0.00.121.622 I load:   - 151664 ('<|file_sep|>')
initiator-1  | 0.00.121.696 I load: special tokens cache size = 27
initiator-1  | 0.00.130.130 I load: token to piece cache size = 0.9311 MB
initiator-1  | 0.00.130.139 I print_info: arch                  = qwen3moe
initiator-1  | 0.00.130.139 I print_info: vocab_only            = 0
initiator-1  | 0.00.130.140 I print_info: no_alloc              = 0
initiator-1  | 0.00.130.140 I print_info: n_ctx_train           = 40960
initiator-1  | 0.00.130.140 I print_info: n_embd_inp            = 2048
initiator-1  | 0.00.130.140 I print_info: n_embd                = 2048
initiator-1  | 0.00.130.140 I print_info: n_embd_out            = 2048
initiator-1  | 0.00.130.140 I print_info: n_layer               = 48
initiator-1  | 0.00.130.141 I print_info: n_layer_all           = 48
initiator-1  | 0.00.130.148 I print_info: n_head                = 32
initiator-1  | 0.00.130.149 I print_info: n_head_kv             = 4
initiator-1  | 0.00.130.150 I print_info: n_rot                 = 128
initiator-1  | 0.00.130.150 I print_info: n_swa                 = 0
initiator-1  | 0.00.130.150 I print_info: is_swa_any            = 0
initiator-1  | 0.00.130.150 I print_info: non_causal_type       = 0
initiator-1  | 0.00.130.150 I print_info: n_embd_head_k         = 128
initiator-1  | 0.00.130.150 I print_info: n_embd_head_v         = 128
initiator-1  | 0.00.130.151 I print_info: n_gqa                 = 8
initiator-1  | 0.00.130.151 I print_info: n_embd_k_gqa          = 512
initiator-1  | 0.00.130.152 I print_info: n_embd_v_gqa          = 512
initiator-1  | 0.00.130.152 I print_info: f_norm_eps            = 0.0e+00
initiator-1  | 0.00.130.153 I print_info: f_norm_rms_eps        = 1.0e-06
initiator-1  | 0.00.130.153 I print_info: f_clamp_kqv           = 0.0e+00
initiator-1  | 0.00.130.154 I print_info: f_max_alibi_bias      = 0.0e+00
initiator-1  | 0.00.130.154 I print_info: f_logit_scale         = 0.0e+00
initiator-1  | 0.00.130.154 I print_info: f_attn_scale          = 0.0e+00
initiator-1  | 0.00.130.154 I print_info: f_attn_value_scale    = 0.0000
initiator-1  | 0.00.130.155 I print_info: n_ff                  = 6144
initiator-1  | 0.00.130.155 I print_info: n_expert              = 128
initiator-1  | 0.00.130.155 I print_info: n_expert_used         = 8
initiator-1  | 0.00.130.155 I print_info: n_expert_groups       = 0
initiator-1  | 0.00.130.155 I print_info: n_group_used          = 0
initiator-1  | 0.00.130.155 I print_info: causal attn           = 1
initiator-1  | 0.00.130.155 I print_info: pooling type          = -1
initiator-1  | 0.00.130.155 I print_info: rope type             = 2
initiator-1  | 0.00.130.156 I print_info: rope scaling          = linear
initiator-1  | 0.00.130.156 I print_info: freq_base_train       = 1000000.0
initiator-1  | 0.00.130.156 I print_info: freq_scale_train      = 1
initiator-1  | 0.00.130.157 I print_info: n_ctx_orig_yarn       = 40960
initiator-1  | 0.00.130.157 I print_info: rope_yarn_log_mul     = 0.0000
initiator-1  | 0.00.130.157 I print_info: rope_finetuned        = unknown
initiator-1  | 0.00.130.159 I print_info: model type            = 30B.A3B
initiator-1  | 0.00.130.159 I print_info: model params          = 30.53 B
initiator-1  | 0.00.130.159 I print_info: general.name          = Qwen3-30B-A3B
initiator-1  | 0.00.130.159 I print_info: n_ff_exp              = 768
initiator-1  | 0.00.130.160 I print_info: vocab type            = BPE
initiator-1  | 0.00.130.160 I print_info: n_vocab               = 151936
initiator-1  | 0.00.130.160 I print_info: n_merges              = 151387
initiator-1  | 0.00.130.161 I print_info: BOS token             = 11 ','
initiator-1  | 0.00.130.161 I print_info: EOS token             = 151645 '<|im_end|>'
initiator-1  | 0.00.130.161 I print_info: EOT token             = 151645 '<|im_end|>'
initiator-1  | 0.00.130.161 I print_info: PAD token             = 151654 '<|vision_pad|>'
initiator-1  | 0.00.130.161 I print_info: LF token              = 198 'Ċ'
initiator-1  | 0.00.130.161 I print_info: FIM PRE token         = 151659 '<|fim_prefix|>'
initiator-1  | 0.00.130.162 I print_info: FIM SUF token         = 151661 '<|fim_suffix|>'
initiator-1  | 0.00.130.162 I print_info: FIM MID token         = 151660 '<|fim_middle|>'
initiator-1  | 0.00.130.162 I print_info: FIM PAD token         = 151662 '<|fim_pad|>'
initiator-1  | 0.00.130.162 I print_info: FIM REP token         = 151663 '<|repo_name|>'
initiator-1  | 0.00.130.162 I print_info: FIM SEP token         = 151664 '<|file_sep|>'
initiator-1  | 0.00.130.162 I print_info: EOG token             = 128247 '</s>'
initiator-1  | 0.00.130.163 I print_info: EOG token             = 151643 '<|endoftext|>'
initiator-1  | 0.00.130.163 I print_info: EOG token             = 151645 '<|im_end|>'
initiator-1  | 0.00.130.163 I print_info: EOG token             = 151662 '<|fim_pad|>'
initiator-1  | 0.00.130.163 I print_info: EOG token             = 151663 '<|repo_name|>'
initiator-1  | 0.00.130.163 I print_info: EOG token             = 151664 '<|file_sep|>'
initiator-1  | 0.00.130.163 I print_info: max token length      = 256
initiator-1  | 0.00.130.649 I load_tensors: loading model tensors, this can take a while... (load_mode = none)
initiator-1  | 0.00.134.200 I load_tensors: offloading output layer to GPU
initiator-1  | 0.00.134.202 I load_tensors: offloading 31 repeating layers to GPU
initiator-1  | 0.00.134.202 I load_tensors: offloaded 32/49 layers to GPU
initiator-1  | 0.00.134.207 I load_tensors:          CPU model buffer size =  3412.91 MiB
initiator-1  | 0.00.134.209 I load_tensors:   CPU_REPACK model buffer size =  1596.94 MiB
initiator-1  | 0.00.134.210 I load_tensors: RPC0[172.18.0.3:50052] model buffer size =  4527.14 MiB
initiator-1  | 0.00.134.211 I load_tensors: RPC0[172.18.0.4:50052] model buffer size =  4487.63 MiB
initiator-1  | 0.14.425.699 I cmn  common_init_: added </s> logit bias = -inf
initiator-1  | 0.14.425.756 I cmn  common_init_: added <|endoftext|> logit bias = -inf
initiator-1  | 0.14.425.756 I cmn  common_init_: added <|im_end|> logit bias = -inf
initiator-1  | 0.14.425.756 I cmn  common_init_: added <|fim_pad|> logit bias = -inf
initiator-1  | 0.14.425.756 I cmn  common_init_: added <|repo_name|> logit bias = -inf
initiator-1  | 0.14.425.757 I cmn  common_init_: added <|file_sep|> logit bias = -inf
initiator-1  | 0.14.425.798 I llama_context: constructing llama_context
initiator-1  | 0.14.425.800 I llama_context: n_seq_max             = 1
initiator-1  | 0.14.425.800 I llama_context: n_ctx                 = 8192
initiator-1  | 0.14.425.800 I llama_context: n_ctx_seq             = 8192
initiator-1  | 0.14.425.800 I llama_context: n_batch               = 2048
initiator-1  | 0.14.425.800 I llama_context: n_ubatch              = 512
initiator-1  | 0.14.425.801 I llama_context: causal_attn           = 1
initiator-1  | 0.14.425.801 I llama_context: flash_attn            = auto
initiator-1  | 0.14.425.801 I llama_context: kv_unified            = false
initiator-1  | 0.14.425.805 I llama_context: freq_base             = 1000000.0
initiator-1  | 0.14.425.805 I llama_context: freq_scale            = 1
initiator-1  | 0.14.425.805 I llama_context: n_rs_seq              = 0
initiator-1  | 0.14.425.805 I llama_context: n_outputs_max         = 1
initiator-1  | 0.14.425.805 I llama_context: n_outputs_max_per_seq = 1
initiator-1  | 0.14.425.805 I llama_context: n_ctx_seq (8192) < n_ctx_train (40960) -- the full capacity of the model will not be utilized
initiator-1  | 0.14.425.837 I llama_context:        CPU  output buffer size =     0.58 MiB
initiator-1  | 0.14.461.957 I llama_kv_cache:        CPU KV buffer size =   272.00 MiB
initiator-1  | 0.14.462.332 I llama_kv_cache: RPC0[172.18.0.3:50052] KV buffer size =   256.00 MiB
initiator-1  | 0.14.462.479 I llama_kv_cache: RPC0[172.18.0.4:50052] KV buffer size =   240.00 MiB
initiator-1  | 0.14.462.487 I llama_kv_cache: size =  768.00 MiB (  8192 cells,  48 layers,  1/1 seqs), K (f16):  384.00 MiB, V (f16):  384.00 MiB
initiator-1  | 0.14.462.491 I llama_kv_cache: n_rot_k = 0, n_embd_head_k_all = 128
initiator-1  | 0.14.462.492 I llama_kv_cache: n_rot_v = 0, n_embd_head_k_all = 128
initiator-1  | 0.14.462.507 I sched_reserve: reserving ...
initiator-1  | 0.14.463.794 I resolve_fused_ops: Flash Attention enabled
initiator-1  | 0.14.463.795 I resolve_fused_ops: resolving fused DeepSeek V4 HC support:
initiator-1  | 0.14.464.341 I resolve_fused_ops: fused DeepSeek V4 HC pre enabled
initiator-1  | 0.14.464.735 I resolve_fused_ops: fused DeepSeek V4 HC comb enabled
initiator-1  | 0.14.465.105 I resolve_fused_ops: fused DeepSeek V4 HC post enabled
initiator-1  | 0.14.502.291 I sched_reserve: RPC0[172.18.0.3:50052] compute buffer size =   100.03 MiB
initiator-1  | 0.14.502.292 I sched_reserve: RPC0[172.18.0.4:50052] compute buffer size =    84.51 MiB
initiator-1  | 0.14.502.293 I sched_reserve:        CPU compute buffer size =    96.02 MiB
initiator-1  | 0.14.502.294 I sched_reserve: graph: nodes = 3033, splits = 3, input objects = 4, input tensors = 9
initiator-1  | 0.14.502.295 I sched_reserve: reserve took 39.79 ms, sched copies = 1
initiator-1  | 0.14.502.328 I cmn          init: llama threadpool init, n_threads = 2
initiator-1  | 0.14.502.344 I cmn  common_init_: warming up the model with an empty run - please wait ... (--no-warmup to disable)
initiator-1  | 0.14.744.359 I srv    load_model: initializing, n_slots = 1, n_ctx_slot = 8192, kv_unified = 'false'
initiator-1  | 0.14.744.371 I spec common_specu: no implementations specified for speculative decoding
initiator-1  | 0.14.744.373 I slot   load_model: id  0 | task -1 | new slot, n_ctx = 8192
initiator-1  | 0.14.744.384 I srv    load_model: prompt cache is disabled - use `--cache-ram N` to enable it
initiator-1  | 0.14.744.385 I srv    load_model: for more info see https://github.com/ggml-org/llama.cpp/pull/16391
initiator-1  | 0.14.744.385 I srv    load_model: context checkpoints enabled, max = 32, min spacing = 8192
initiator-1  | 0.14.744.414 W srv          init: --cache-idle-slots requires --cache-ram, disabling
initiator-1  | 0.14.751.652 I srv          init: init: chat template, example_format: '<|im_start|>system
initiator-1  | You are a helpful assistant<|im_end|>
initiator-1  | <|im_start|>user
initiator-1  | Hello<|im_end|>
initiator-1  | <|im_start|>assistant
initiator-1  | Hi there<|im_end|>
initiator-1  | <|im_start|>user
initiator-1  | How are you?<|im_end|>
initiator-1  | <|im_start|>assistant
initiator-1  | <think>
initiator-1  |
initiator-1  | </think>
initiator-1  |
initiator-1  | '
initiator-1  | 0.14.751.910 I srv          init: init: chat template, thinking = 0
initiator-1  | 0.14.751.922 I srv          init: preserve_reasoning kwarg: not supported by template
initiator-1  | 0.14.751.950 I srv  server_run_i: model loaded
initiator-1  | 0.14.751.955 I srv  server_on_li: listening on http://0.0.0.0:8080
initiator-1  | 0.14.752.093 I srv  update_slots: all slots are idle
initiator-1  | 3.43.829.920 I srv  server_strea: conv_id=065d6b4b-e015-4058-9fdd-daadf209493f (empty=0)
initiator-1  | 3.43.830.468 I slot get_availabl: id  0 | task -1 |  - skipping, slot is empty
initiator-1  | 3.43.830.472 I slot get_availabl: id  0 | task -1 | selected slot by LRU, n_tokens = 0, t_last = -1
initiator-1  | 3.43.830.626 I cmn  common_reaso: activated, budget=2147483647 tokens
initiator-1  | 3.43.830.627 I cmn  common_reaso: deactivated (natural end)
initiator-1  | 3.43.830.653 I slot launch_slot_: id  0 | task -1 | sampler chain: logits -> ?penalties -> ?dry -> ?top-n-sigma -> top-k -> ?typical -> top-p -> min-p -> ?xtc -> temp-ext -> dist
initiator-1  | 3.43.830.662 I slot launch_slot_: id  0 | task -1 | sampler params:
initiator-1  |     repeat_last_n = 64, repeat_penalty = 1.000, frequency_penalty = 0.000, presence_penalty = 0.000
initiator-1  |     dry_multiplier = 0.000, dry_base = 1.750, dry_allowed_length = 2, dry_penalty_last_n = 64
initiator-1  |     top_k = 40, top_p = 0.950, min_p = 0.050, xtc_probability = 0.000, xtc_threshold = 0.100, typical_p = 1.000, top_n_sigma = -1.000, temp = 0.800
initiator-1  |     mirostat = 0, mirostat_lr = 0.100, mirostat_ent = 5.000, adaptive_target = -1.000, adaptive_decay = 0.900
initiator-1  | 3.43.830.664 I slot launch_slot_: id  0 | task 0 | processing task, is_child = 0
initiator-1  | 3.43.830.670 I slot   operator(): id  0 | task 0 | new prompt, n_ctx_slot = 8192, n_keep = 0, task.n_tokens = 218
initiator-1  | 3.43.830.676 I slot   operator(): id  0 | task 0 | cached n_tokens = 0, memory_seq_rm [0, end)
initiator-1  | 3.43.830.734 I slot init_sampler: id  0 | task 0 | init sampler, took 0.03 ms, tokens: text = 218, total = 218
initiator-1  | 3.48.455.137 I sched_graph_realloc: graph reallocation: nodes = 3059 (changed = 0), splits = 3, n_same = 1, n_diff = 0
initiator-1  | 3.52.162.258 I slot print_timing: id  0 | task 0 | n_gen =    100, tg =  16.84 t/s, tg_3s =  17.01 t/s
initiator-1  | 3.55.192.718 I slot print_timing: id  0 | task 0 | n_gen =    150, tg =  16.72 t/s, tg_3s =  16.50 t/s
initiator-1  | 3.58.206.141 I slot print_timing: id  0 | task 0 | n_gen =    200, tg =  16.69 t/s, tg_3s =  16.59 t/s
initiator-1  | 4.01.210.592 I slot print_timing: id  0 | task 0 | n_gen =    250, tg =  16.68 t/s, tg_3s =  16.64 t/s
initiator-1  | 4.03.918.478 I sched_graph_realloc: graph reallocation: nodes = 3059 (changed = 0), splits = 3, n_same = 2, n_diff = 0
initiator-1  | 4.04.242.679 I slot print_timing: id  0 | task 0 | n_gen =    300, tg =  16.65 t/s, tg_3s =  16.49 t/s
initiator-1  | 4.06.882.196 I slot print_timing: id  0 | task 0 | prompt eval time =    2453.13 ms /   218 tokens (   11.25 ms per token,    88.87 tokens per second)
initiator-1  | 4.06.882.199 I slot print_timing: id  0 | task 0 |        eval time =   20598.38 ms /   341 tokens (   60.58 ms per token,    16.51 tokens per second)
initiator-1  | 4.06.882.200 I slot print_timing: id  0 | task 0 |       total time =   23051.51 ms /   559 tokens
initiator-1  | 4.06.882.200 I slot print_timing: id  0 | task 0 |    graphs reused =        338
initiator-1  | 4.06.882.201 I slot print_timing: id  0 | task 0 |   graph reallocs =          2    /     0        (same size) / (changed size)
initiator-1  | 4.06.882.219 I slot      release: id  0 | task 0 | stop processing: n_tokens = 558, truncated = 0
initiator-1  | 4.06.882.227 I srv  update_slots: all slots are idle
initiator-1  | 4.25.795.249 I srv  server_strea: conv_id=065d6b4b-e015-4058-9fdd-daadf209493f (empty=0)
initiator-1  | 4.25.796.407 I slot get_availabl: id  0 | task -1 |  - checking sim = 0.374 (214/572) > 0.100
initiator-1  | 4.25.796.409 I slot get_availabl: id  0 | task -1 | selected slot by LCP similarity, f_sim_best = 0.374 (> 0.100 thold), f_keep = 0.384
initiator-1  | 4.25.796.521 I cmn  common_reaso: activated, budget=2147483647 tokens
initiator-1  | 4.25.796.523 I cmn  common_reaso: deactivated (natural end)
initiator-1  | 4.25.796.551 I slot launch_slot_: id  0 | task -1 | sampler chain: logits -> ?penalties -> ?dry -> ?top-n-sigma -> top-k -> ?typical -> top-p -> min-p -> ?xtc -> temp-ext -> dist
initiator-1  | 4.25.796.556 I slot launch_slot_: id  0 | task -1 | sampler params:
initiator-1  |     repeat_last_n = 64, repeat_penalty = 1.000, frequency_penalty = 0.000, presence_penalty = 0.000
initiator-1  |     dry_multiplier = 0.000, dry_base = 1.750, dry_allowed_length = 2, dry_penalty_last_n = 64
initiator-1  |     top_k = 40, top_p = 0.950, min_p = 0.050, xtc_probability = 0.000, xtc_threshold = 0.100, typical_p = 1.000, top_n_sigma = -1.000, temp = 0.800
initiator-1  |     mirostat = 0, mirostat_lr = 0.100, mirostat_ent = 5.000, adaptive_target = -1.000, adaptive_decay = 0.900
initiator-1  | 4.25.796.557 I slot launch_slot_: id  0 | task 342 | processing task, is_child = 0
initiator-1  | 4.25.796.562 I slot   operator(): id  0 | task 342 | new prompt, n_ctx_slot = 8192, n_keep = 0, task.n_tokens = 572
initiator-1  | 4.25.796.566 I slot   operator(): id  0 | task 342 | cached n_tokens = 0, memory_seq_rm [0, end)
initiator-1  | 4.25.796.693 I slot init_sampler: id  0 | task 342 | init sampler, took 0.04 ms, tokens: text = 572, total = 572
initiator-1  | 4.29.331.364 I sched_graph_realloc: graph reallocation: nodes = 3061 (changed = 0), splits = 3, n_same = 3, n_diff = 0
initiator-1  | 4.32.318.961 I slot print_timing: id  0 | task 342 | prompt eval time =    6258.15 ms /   572 tokens (   10.94 ms per token,    91.40 tokens per second)
initiator-1  | 4.32.318.964 I slot print_timing: id  0 | task 342 |        eval time =     264.24 ms /     5 tokens (   66.06 ms per token,    15.14 tokens per second)
initiator-1  | 4.32.318.965 I slot print_timing: id  0 | task 342 |       total time =    6522.38 ms /   577 tokens
initiator-1  | 4.32.318.966 I slot print_timing: id  0 | task 342 |    graphs reused =        341
initiator-1  | 4.32.318.966 I slot print_timing: id  0 | task 342 |   graph reallocs =          3    /     0        (same size) / (changed size)
initiator-1  | 4.32.319.006 I slot      release: id  0 | task 342 | stop processing: n_tokens = 576, truncated = 0
initiator-1  | 4.32.319.015 I srv  update_slots: all slots are idle
initiator-1  | 4.51.795.508 I srv  server_strea: conv_id=065d6b4b-e015-4058-9fdd-daadf209493f (empty=0)
initiator-1  | 4.51.796.702 I slot get_availabl: id  0 | task -1 |  - checking sim = 0.961 (568/591) > 0.100
initiator-1  | 4.51.796.706 I slot get_availabl: id  0 | task -1 | selected slot by LCP similarity, f_sim_best = 0.961 (> 0.100 thold), f_keep = 0.986
initiator-1  | 4.51.796.846 I cmn  common_reaso: activated, budget=2147483647 tokens
initiator-1  | 4.51.796.848 I cmn  common_reaso: deactivated (natural end)
initiator-1  | 4.51.796.885 I slot launch_slot_: id  0 | task -1 | sampler chain: logits -> ?penalties -> ?dry -> ?top-n-sigma -> top-k -> ?typical -> top-p -> min-p -> ?xtc -> temp-ext -> dist
initiator-1  | 4.51.796.892 I slot launch_slot_: id  0 | task -1 | sampler params:
initiator-1  |     repeat_last_n = 64, repeat_penalty = 1.000, frequency_penalty = 0.000, presence_penalty = 0.000
initiator-1  |     dry_multiplier = 0.000, dry_base = 1.750, dry_allowed_length = 2, dry_penalty_last_n = 64
initiator-1  |     top_k = 40, top_p = 0.950, min_p = 0.050, xtc_probability = 0.000, xtc_threshold = 0.100, typical_p = 1.000, top_n_sigma = -1.000, temp = 0.800
initiator-1  |     mirostat = 0, mirostat_lr = 0.100, mirostat_ent = 5.000, adaptive_target = -1.000, adaptive_decay = 0.900
initiator-1  | 4.51.796.893 I slot launch_slot_: id  0 | task 348 | processing task, is_child = 0
initiator-1  | 4.51.796.900 I slot   operator(): id  0 | task 348 | new prompt, n_ctx_slot = 8192, n_keep = 0, task.n_tokens = 591
initiator-1  | 4.51.796.904 I slot   operator(): id  0 | task 348 | cached n_tokens = 0, memory_seq_rm [0, end)
initiator-1  | 4.51.797.081 I slot init_sampler: id  0 | task 348 | init sampler, took 0.09 ms, tokens: text = 591, total = 591
initiator-1  | 4.55.317.838 I sched_graph_realloc: graph reallocation: nodes = 3061 (changed = 0), splits = 3, n_same = 4, n_diff = 0
initiator-1  | 4.59.825.793 I slot print_timing: id  0 | task 348 | prompt eval time =    6219.76 ms /   591 tokens (   10.52 ms per token,    95.02 tokens per second)
initiator-1  | 4.59.825.796 I slot print_timing: id  0 | task 348 |        eval time =    1809.11 ms /    29 tokens (   64.61 ms per token,    15.48 tokens per second)
initiator-1  | 4.59.825.796 I slot print_timing: id  0 | task 348 |       total time =    8028.87 ms /   620 tokens
initiator-1  | 4.59.825.796 I slot print_timing: id  0 | task 348 |    graphs reused =        368
initiator-1  | 4.59.825.797 I slot print_timing: id  0 | task 348 |   graph reallocs =          4    /     0        (same size) / (changed size)
initiator-1  | 4.59.825.829 I slot      release: id  0 | task 348 | stop processing: n_tokens = 619, truncated = 0
initiator-1  | 4.59.825.835 I srv  update_slots: all slots are idle
```
