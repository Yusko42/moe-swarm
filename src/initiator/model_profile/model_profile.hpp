// Keep guards independent of checkout path (LLVM otherwise embeds absolute paths).
#ifndef SRC_INITIATOR_MODEL_PROFILE_MODEL_PROFILE_HPP  // NOLINT(llvm-header-guard)
#define SRC_INITIATOR_MODEL_PROFILE_MODEL_PROFILE_HPP

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace swarm {
struct Metadata {
    std::uint32_t type = 0;
    std::uint32_t element_type = 0;
    std::uint64_t count = 0;
    std::string text;
    std::vector<std::uint64_t> integers;
};
struct TensorInfo {
    std::string name;
    std::vector<std::uint64_t> shape;
    std::uint32_t type = 0;
    std::uint64_t offset = 0;
    std::uint64_t absolute_offset = 0;
    std::uint64_t size_bytes = 0;
};
struct GgufHeader {
    std::uint32_t version = 0;
    bool big_endian = false;
    std::uint64_t file_size = 0;
    std::uint64_t header_end = 0;
    std::uint64_t data_start = 0;
    std::uint64_t tensor_bytes = 0;
    std::map<std::string, Metadata> metadata;
    std::vector<TensorInfo> tensors;
};
struct LayerMemory {
    std::uint64_t weight_bytes = 0;
    std::uint64_t kv_bytes = 0;
};
struct ModelProfile {
    std::string architecture;
    std::uint64_t context_tokens = 8192;
    std::vector<LayerMemory> layers;
    std::map<std::string, std::uint64_t> nonblock_tensors;
};
struct NodePlacement {
    std::string name;
    std::uint64_t first = 0;
    std::uint64_t end = 0;
    std::vector<std::string> nonblock_tensors;
    std::uint64_t engine_buffer_bytes = 0;
    std::uint64_t overhead_bytes = 0;
    std::uint64_t limit_bytes = 0;
};
struct NodeMemory {
    std::string name;
    std::uint64_t weight_bytes = 0;
    std::uint64_t nonblock_bytes = 0;
    std::uint64_t kv_bytes = 0;
    std::uint64_t engine_buffer_bytes = 0;
    std::uint64_t overhead_bytes = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t limit_bytes = 0;
    bool fits = false;
};
GgufHeader read_gguf_header(const std::filesystem::path& file);
ModelProfile build_profile(const GgufHeader& header, std::uint64_t context_tokens = 8192);
std::vector<NodeMemory> estimate_ram(const ModelProfile& model, const std::vector<NodePlacement>& nodes);
}  // namespace swarm
#endif
