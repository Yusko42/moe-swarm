#pragma once  // NOLINT(llvm-header-guard)

#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include "types/detail/enum_strings.hpp"

namespace swarm::types {

// Closed sets from architecture v0.5, section 7.3. Only the string names below
// are part of the wire contract; enum ordinals are not serialized.
enum class SwarmState { Idle, Deploying, Ready, Disbanding, Failed };
enum class ModelState { NotStarted, Placing, Ready, Answering, RebuildPending };
enum class DeployPhase { Baseline, Measure, Plan, RpcStart, LoadingWeights, Warmup, Verify };
enum class NodeState { Joining, Measuring, Idle, Assigned, Leaving, Left, Lost, Rejected };
enum class RpcState { Absent, Starting, Loading, Running, Stopping, Stopped, Failed };
enum class Action { Noop, Measure, RpcRunning, RpcStopped, LeaveAllowed, Disband };
enum class QueueReason { Busy, Placing, Rebuild };
enum class DoneReason { Eos, ContextLimit, Stopped };
enum class NoticeCode { RebuildPending, Rebuilding, WeightsTransfer, NodeLost, SwarmRebuilt };

namespace detail {

inline constexpr std::array<std::pair<SwarmState, std::string_view>, 5> kSwarmStates{{
    {SwarmState::Idle, "idle"},
    {SwarmState::Deploying, "deploying"},
    {SwarmState::Ready, "ready"},
    {SwarmState::Disbanding, "disbanding"},
    {SwarmState::Failed, "failed"},
}};

inline constexpr std::array<std::pair<ModelState, std::string_view>, 5> kModelStates{{
    {ModelState::NotStarted, "not_started"},
    {ModelState::Placing, "placing"},
    {ModelState::Ready, "ready"},
    {ModelState::Answering, "answering"},
    {ModelState::RebuildPending, "rebuild_pending"},
}};

inline constexpr std::array<std::pair<DeployPhase, std::string_view>, 7> kDeployPhases{{
    {DeployPhase::Baseline, "baseline"},
    {DeployPhase::Measure, "measure"},
    {DeployPhase::Plan, "plan"},
    {DeployPhase::RpcStart, "rpc_start"},
    {DeployPhase::LoadingWeights, "loading_weights"},
    {DeployPhase::Warmup, "warmup"},
    {DeployPhase::Verify, "verify"},
}};

inline constexpr std::array<std::pair<NodeState, std::string_view>, 8> kNodeStates{{
    {NodeState::Joining, "joining"},
    {NodeState::Measuring, "measuring"},
    {NodeState::Idle, "idle"},
    {NodeState::Assigned, "assigned"},
    {NodeState::Leaving, "leaving"},
    {NodeState::Left, "left"},
    {NodeState::Lost, "lost"},
    {NodeState::Rejected, "rejected"},
}};

inline constexpr std::array<std::pair<RpcState, std::string_view>, 7> kRpcStates{{
    {RpcState::Absent, "absent"},
    {RpcState::Starting, "starting"},
    {RpcState::Loading, "loading"},
    {RpcState::Running, "running"},
    {RpcState::Stopping, "stopping"},
    {RpcState::Stopped, "stopped"},
    {RpcState::Failed, "failed"},
}};

inline constexpr std::array<std::pair<Action, std::string_view>, 6> kActions{{
    {Action::Noop, "noop"},
    {Action::Measure, "measure"},
    {Action::RpcRunning, "rpc_running"},
    {Action::RpcStopped, "rpc_stopped"},
    {Action::LeaveAllowed, "leave_allowed"},
    {Action::Disband, "disband"},
}};

inline constexpr std::array<std::pair<QueueReason, std::string_view>, 3> kQueueReasons{{
    {QueueReason::Busy, "busy"},
    {QueueReason::Placing, "placing"},
    {QueueReason::Rebuild, "rebuild"},
}};

inline constexpr std::array<std::pair<DoneReason, std::string_view>, 3> kDoneReasons{{
    {DoneReason::Eos, "eos"},
    {DoneReason::ContextLimit, "context_limit"},
    {DoneReason::Stopped, "stopped"},
}};

inline constexpr std::array<std::pair<NoticeCode, std::string_view>, 5> kNoticeCodes{{
    {NoticeCode::RebuildPending, "rebuild_pending"},
    {NoticeCode::Rebuilding, "rebuilding"},
    {NoticeCode::WeightsTransfer, "weights_transfer"},
    {NoticeCode::NodeLost, "node_lost"},
    {NoticeCode::SwarmRebuilt, "swarm_rebuilt"},
}};

}  // namespace detail

[[nodiscard]] constexpr std::string_view to_string(SwarmState value) {
    return detail::enum_name(value, detail::kSwarmStates);
}
[[nodiscard]] constexpr std::string_view to_string(ModelState value) {
    return detail::enum_name(value, detail::kModelStates);
}
[[nodiscard]] constexpr std::string_view to_string(DeployPhase value) {
    return detail::enum_name(value, detail::kDeployPhases);
}
[[nodiscard]] constexpr std::string_view to_string(NodeState value) {
    return detail::enum_name(value, detail::kNodeStates);
}
[[nodiscard]] constexpr std::string_view to_string(RpcState value) {
    return detail::enum_name(value, detail::kRpcStates);
}
[[nodiscard]] constexpr std::string_view to_string(Action value) { return detail::enum_name(value, detail::kActions); }
[[nodiscard]] constexpr std::string_view to_string(QueueReason value) {
    return detail::enum_name(value, detail::kQueueReasons);
}
[[nodiscard]] constexpr std::string_view to_string(DoneReason value) {
    return detail::enum_name(value, detail::kDoneReasons);
}
[[nodiscard]] constexpr std::string_view to_string(NoticeCode value) {
    return detail::enum_name(value, detail::kNoticeCodes);
}

[[nodiscard]] constexpr std::optional<SwarmState> parse_swarm_state(std::string_view name) {
    return detail::enum_value(name, detail::kSwarmStates);
}
[[nodiscard]] constexpr std::optional<ModelState> parse_model_state(std::string_view name) {
    return detail::enum_value(name, detail::kModelStates);
}
[[nodiscard]] constexpr std::optional<DeployPhase> parse_deploy_phase(std::string_view name) {
    return detail::enum_value(name, detail::kDeployPhases);
}
[[nodiscard]] constexpr std::optional<NodeState> parse_node_state(std::string_view name) {
    return detail::enum_value(name, detail::kNodeStates);
}
[[nodiscard]] constexpr std::optional<RpcState> parse_rpc_state(std::string_view name) {
    return detail::enum_value(name, detail::kRpcStates);
}
[[nodiscard]] constexpr std::optional<Action> parse_action(std::string_view name) {
    return detail::enum_value(name, detail::kActions);
}
[[nodiscard]] constexpr std::optional<QueueReason> parse_queue_reason(std::string_view name) {
    return detail::enum_value(name, detail::kQueueReasons);
}
[[nodiscard]] constexpr std::optional<DoneReason> parse_done_reason(std::string_view name) {
    return detail::enum_value(name, detail::kDoneReasons);
}
[[nodiscard]] constexpr std::optional<NoticeCode> parse_notice_code(std::string_view name) {
    return detail::enum_value(name, detail::kNoticeCodes);
}

}  // namespace swarm::types
