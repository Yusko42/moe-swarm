# Модуль swarm::types

## Ответственность

Общие идентификаторы, скаляры и перечисления по §7.2–7.3 архитектуры
MoE Swarm v0.5 от 20.11.2026; коды ошибок из §6.2 той же редакции.
Модуль находится в `src/types`, как указано в структуре репозитория (§2).
Это библиотека только из заголовков, без ввода-вывода и внешних зависимостей.
Пространство имён — `swarm::types`, цель CMake — `swarm::types`, стандарт — C++20.

## Заголовки и типы

### `types/identifiers.hpp`

| Поле | Тип C++ | Представление в данных |
|------|---------|------------------------|
| `swarm_id` | `SwarmId` = `Uuid` | UUID v4, строка из 36 символов |
| `node_id` | `NodeId` = `Uuid` | UUID v4, строка из 36 символов |
| `operation_id` | `OperationId` = `Uuid` | UUID v4, строка из 36 символов |
| `request_id` | `RequestId` = `Uuid` | UUID v4, строка из 36 символов |
| `node_hw_id` | `NodeHwId` | Хэш машины, 64 шестнадцатеричных символа |

`Uuid` — `std::array<std::uint8_t, 16>`, `NodeHwId` —
`std::array<std::uint8_t, 32>`. Имена идентификаторов — псевдонимы, без запрета
присваивания между UUID разных назначений. Генерация UUID v4, вычисление хэша
и их текстовое кодирование относятся к использующим модуль компонентам.

### `types/scalars.hpp`

| Поле / величина | Тип C++ | Единицы / ограничение |
|----------------|---------|----------------------|
| `deployment_id` | `DeploymentId` = `std::uint64_t` | Монотонный токен, < 2^53 |
| `seq` | `SequenceNumber` = `std::uint64_t` | Монотонный счётчик каждого узла |
| `attempt` | `Attempt` = `std::uint8_t` | 1…2 |
| Значения в байтах | `Bytes` = `std::uint64_t` | Байты |
| `rtt_us` | `RttUs` = `std::uint32_t` | Микросекунды; интерфейс показывает мс с дробью |
| `bandwidth_Bps`, `mem_bw_Bps` | `BandwidthBps` = `std::uint64_t` | Байт/с |
| `cpu_score` | `CpuScore` = `double` | Нормированная безразмерная оценка |
| `ts_mono_ns` | `MonoTimestampNs` = `std::uint64_t` | Наносекунды монотонных часов |
| Индекс слоя | `LayerIndex` = `std::uint16_t` | [0, block_count), для целевой модели [0, 48) |
| Контекст | `ContextTokens` = `std::uint32_t` | Предел 8192 токена |
| Версия протокола | `ProtocolVersion` = `std::uint16_t` | 1 |

Константы: `kGib = 2^30`, `kMaxDeploymentId = 2^53 − 1`,
`kMinAttempt = 1`, `kMaxAttempt = 2`, `kContextLimit = 8192`,
`kProtocolVersion = 1`. Число слоёв берётся из метаданных GGUF, а не из константы.
ГБ в командной строке и интерфейсе означает ГиБ.

Псевдонимы задают представление в памяти; проверка диапазонов, версии протокола
и монотонности выполняется компонентами на границах протокола. Целые,
потенциально превышающие безопасный диапазон JavaScript, сериализуются строкой.
Байты и счётчики в безопасном диапазоне передаются числами.
`timestamp` не получает отдельного числового типа: это строка даты и времени UTC
с миллисекундами, только для журналов и интерфейса (§6.5).

### `types/enums.hpp`

| Тип | Значения протокола |
|-----|--------------------|
| `SwarmState` | `idle`, `deploying`, `ready`, `disbanding`, `failed` |
| `ModelState` | `not_started`, `placing`, `ready`, `answering`, `rebuild_pending` |
| `DeployPhase` | `baseline`, `measure`, `plan`, `rpc_start`, `loading_weights`, `warmup`, `verify` |
| `NodeState` | `joining`, `measuring`, `idle`, `assigned`, `leaving`, `left`, `lost`, `rejected` |
| `RpcState` | `absent`, `starting`, `loading`, `running`, `stopping`, `stopped`, `failed` |
| `Action` | `noop`, `measure`, `rpc_running`, `rpc_stopped`, `leave_allowed`, `disband` |
| `QueueReason` | `busy`, `placing`, `rebuild` |
| `DoneReason` | `eos`, `context_limit`, `stopped` |
| `NoticeCode` | `rebuild_pending`, `rebuilding`, `weights_transfer`, `node_lost`, `swarm_rebuilt` |

Элементы `enum class` используют PascalCase, например `RpcState::Loading`.
Числовые значения перечислений не являются частью протокола.

### `types/error_code.hpp`

`ErrorCode` содержит все 19 кодов из §6.2:

| Элемент C++ | Код протокола |
|-------------|---------------|
| `TooLong` | `too_long` |
| `QueueFull` | `queue_full` |
| `ModelNotStarted` | `model_not_started` |
| `PageBusy` | `page_busy` |
| `InsufficientMemory` | `insufficient_memory` |
| `RecoveryFailed` | `recovery_failed` |
| `Disbanded` | `disbanded` |
| `PlacementMismatch` | `placement_mismatch` |
| `ProbeTimeout` | `probe_timeout` |
| `EngineOom` | `engine_oom` |
| `EngineCrashed` | `engine_crashed` |
| `WeightsTransferFailed` | `weights_transfer_failed` |
| `DeployTimeout` | `deploy_timeout` |
| `RpcVersionMismatch` | `rpc_version_mismatch` |
| `NodeIncompatible` | `node_incompatible` |
| `AuthFailed` | `auth_failed` |
| `CgroupUnavailable` | `cgroup_unavailable` |
| `Cancelled` | `cancelled` |
| `ProtocolError` | `protocol_error` |

## Преобразование перечислений

Для всех перечислений, включая `ErrorCode`, доступны `constexpr`-функции:

- `to_string(value)` → `std::string_view` с точным значением в snake_case;
- `parse_swarm_state`, `parse_model_state`, `parse_deploy_phase`, `parse_node_state`,
  `parse_rpc_state`, `parse_action`, `parse_queue_reason`, `parse_done_reason`,
  `parse_notice_code`, `parse_error_code` принимают `std::string_view` и возвращают
  `std::optional` соответствующего перечисления.

Разбор чувствителен к регистру, пробелы не удаляет. Неизвестная строка возвращает
`std::nullopt`; передача недопустимого числового значения в `to_string` возвращает
пустую строку. Преобразования не выделяют память и не подставляют состояние по умолчанию.

```cpp
#include "types/enums.hpp"
#include "types/identifiers.hpp"
#include "types/scalars.hpp"

swarm::types::NodeId node_id{};
swarm::types::Bytes memory = 8 * swarm::types::kGib;
auto state = swarm::types::parse_rpc_state("loading");
// state == swarm::types::RpcState::Loading
```

Потребитель CMake подключает `target_link_libraries(<target> PRIVATE swarm::types)`.

