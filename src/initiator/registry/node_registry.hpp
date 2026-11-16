#pragma once  // NOLINT(llvm-header-guard)

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace swarm::initiator {

struct NodeRecord {
    std::string node_id;
    std::string agent_instance_id;
    std::string name;
    std::string rpc_host;
    int rpc_port = 0;
    std::uint64_t memory_limit_bytes = 0;
    std::string rpc_state = "stopped";
    std::string last_deployment_id;
};

class NodeRegistry {
   public:
    NodeRegistry() = default;
    NodeRegistry(const NodeRegistry&) = delete;
    NodeRegistry& operator=(const NodeRegistry&) = delete;

    [[nodiscard]] const std::string& swarm_id() const { return swarm_id_; }

    NodeRecord join(const std::string& agent_instance_id, const std::string& name, std::uint64_t memory_limit_bytes,
                    const std::string& rpc_host);

    std::optional<NodeRecord> find(const std::string& node_id) const;

    bool update_rpc_state(const std::string& node_id, const std::string& state, const std::string& deployment_id,
                          int port);

   private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, NodeRecord> by_node_id_;
    std::unordered_map<std::string, std::string> agent_to_node_;
    std::string swarm_id_ = "s1";
    int next_id_ = 1;
};

}  // namespace swarm::initiator