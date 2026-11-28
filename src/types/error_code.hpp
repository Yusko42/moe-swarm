#pragma once  // NOLINT(llvm-header-guard)

#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include "types/detail/enum_strings.hpp"

namespace swarm::types {

// Complete error vocabulary from architecture v0.5, section 6.2.
enum class ErrorCode {
    TooLong,
    QueueFull,
    ModelNotStarted,
    PageBusy,
    InsufficientMemory,
    RecoveryFailed,
    Disbanded,
    PlacementMismatch,
    ProbeTimeout,
    EngineOom,
    EngineCrashed,
    WeightsTransferFailed,
    DeployTimeout,
    RpcVersionMismatch,
    NodeIncompatible,
    AuthFailed,
    CgroupUnavailable,
    Cancelled,
    ProtocolError,
};

namespace detail {

inline constexpr std::array<std::pair<ErrorCode, std::string_view>, 19> kErrorCodes{{
    {ErrorCode::TooLong, "too_long"},
    {ErrorCode::QueueFull, "queue_full"},
    {ErrorCode::ModelNotStarted, "model_not_started"},
    {ErrorCode::PageBusy, "page_busy"},
    {ErrorCode::InsufficientMemory, "insufficient_memory"},
    {ErrorCode::RecoveryFailed, "recovery_failed"},
    {ErrorCode::Disbanded, "disbanded"},
    {ErrorCode::PlacementMismatch, "placement_mismatch"},
    {ErrorCode::ProbeTimeout, "probe_timeout"},
    {ErrorCode::EngineOom, "engine_oom"},
    {ErrorCode::EngineCrashed, "engine_crashed"},
    {ErrorCode::WeightsTransferFailed, "weights_transfer_failed"},
    {ErrorCode::DeployTimeout, "deploy_timeout"},
    {ErrorCode::RpcVersionMismatch, "rpc_version_mismatch"},
    {ErrorCode::NodeIncompatible, "node_incompatible"},
    {ErrorCode::AuthFailed, "auth_failed"},
    {ErrorCode::CgroupUnavailable, "cgroup_unavailable"},
    {ErrorCode::Cancelled, "cancelled"},
    {ErrorCode::ProtocolError, "protocol_error"},
}};

}  // namespace detail

[[nodiscard]] constexpr std::string_view to_string(ErrorCode value) {
    return detail::enum_name(value, detail::kErrorCodes);
}

[[nodiscard]] constexpr std::optional<ErrorCode> parse_error_code(std::string_view name) {
    return detail::enum_value(name, detail::kErrorCodes);
}

}  // namespace swarm::types
