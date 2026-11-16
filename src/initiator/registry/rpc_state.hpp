#pragma once  // NOLINT(llvm-header-guard)

#include <optional>
#include <string_view>

namespace swarm::initiator {

enum class RpcState {
    Stopped,
    Running,
    Failed,
};

[[nodiscard]] constexpr std::string_view to_string(RpcState state) {
    switch (state) {
        case RpcState::Stopped:
            return "stopped";
        case RpcState::Running:
            return "running";
        case RpcState::Failed:
            return "failed";
    }
    return "stopped";  // unreachable; silences -Wreturn-type
}

[[nodiscard]] constexpr std::optional<RpcState> parse_rpc_state(std::string_view s) {
    if (s == "stopped") {
        return RpcState::Stopped;
    }
    if (s == "running") {
        return RpcState::Running;
    }
    if (s == "failed") {
        return RpcState::Failed;
    }
    return std::nullopt;
}

}  // namespace swarm::initiator