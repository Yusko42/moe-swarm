#include "node_registry.hpp"

namespace swarm::initiator {

NodeRecord NodeRegistry::join(const std::string& agent_instance_id, const std::string& name,
                              std::uint64_t memory_limit_bytes, const std::string& rpc_host) {
    std::lock_guard lock{mu_};

    if (const auto it = agent_to_node_.find(agent_instance_id); it != agent_to_node_.end()) {
        return by_node_id_.at(it->second);
    }

    NodeRecord rec;
    rec.node_id = "n" + std::to_string(next_id_++);
    rec.agent_instance_id = agent_instance_id;
    rec.name = name;
    rec.rpc_host = rpc_host;
    rec.memory_limit_bytes = memory_limit_bytes;

    by_node_id_.emplace(rec.node_id, rec);
    agent_to_node_.emplace(agent_instance_id, rec.node_id);
    return rec;
}

std::optional<NodeRecord> NodeRegistry::find(const std::string& node_id) const {
    std::lock_guard lock{mu_};
    const auto it = by_node_id_.find(node_id);
    if (it == by_node_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool NodeRegistry::update_rpc_state(const std::string& node_id, RpcState state, const std::string& deployment_id,
                                    int port) {
    std::lock_guard lock{mu_};
    const auto it = by_node_id_.find(node_id);
    if (it == by_node_id_.end()) {
        return false;
    }

    NodeRecord& n = it->second;
    const bool was_running = n.rpc_state == RpcState::Running;
    const bool is_running = state == RpcState::Running;

    const bool meaningful_change =
        is_running && (!was_running || n.last_deployment_id != deployment_id || (port != 0 && n.rpc_port != port));

    n.rpc_state = state;
    n.last_deployment_id = deployment_id;
    if (port != 0) {
        n.rpc_port = port;
    }
    return meaningful_change;
}
}  // namespace swarm::initiator