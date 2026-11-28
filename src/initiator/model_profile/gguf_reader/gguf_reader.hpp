#ifndef SRC_INITIATOR_MODEL_PROFILE_GGUF_READER_GGUF_READER_HPP  // NOLINT(llvm-header-guard)
#define SRC_INITIATOR_MODEL_PROFILE_GGUF_READER_GGUF_READER_HPP

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace swarm {
// GGUF type IDs are preserved. Scalars use text or a single typed vector entry;
// arrays use count, element_type and the corresponding typed vector.
struct Metadata {
    std::uint32_t type = 0;
    std::uint32_t element_type = 0;
    std::uint64_t count = 0;
    std::string text;
    std::vector<std::uint64_t> integers;
    std::vector<std::int64_t> signed_integers;
    std::vector<double> reals;
    std::vector<bool> booleans;
    std::vector<std::string> strings;
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
    std::uint32_t alignment = 32;
    bool big_endian = false;
    std::uint64_t file_size = 0;
    std::uint64_t header_end = 0;
    std::uint64_t data_start = 0;
    std::uint64_t tensor_bytes = 0;
    std::map<std::string, Metadata> metadata;
    std::vector<TensorInfo> tensors;
};
// Reads metadata and tensor descriptors only; tensor payloads are never loaded.
GgufHeader read_gguf_header(const std::filesystem::path& file);
}  // namespace swarm
#endif
