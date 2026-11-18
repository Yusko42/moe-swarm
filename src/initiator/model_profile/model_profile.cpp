#include "model_profile.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace swarm {
namespace {
using U64 = std::uint64_t;
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("GGUF: " + message); }
U64 add(U64 a, U64 b) {
    if (b > std::numeric_limits<U64>::max() - a) {
        fail("integer addition overflow");
    }
    return a + b;
}
U64 mul(U64 a, U64 b) {
    if (a != 0 && b > std::numeric_limits<U64>::max() / a) {
        fail("integer multiplication overflow");
    }
    return a * b;
}
U64 align(U64 value, U64 alignment) { return mul(add(value, alignment - 1) / alignment, alignment); }

class Reader {
   public:
    explicit Reader(const std::filesystem::path& path) : stream_(path, std::ios::binary) {
        if (!stream_) {
            fail("cannot open " + path.string());
        }
        size_ = std::filesystem::file_size(path);
    }
    [[nodiscard]] U64 size() const { return size_; }
    [[nodiscard]] U64 position() const { return position_; }
    [[nodiscard]] bool big_endian() const { return big_; }
    void set_big_endian() { big_ = true; }
    void require(U64 bytes) const {
        if (bytes > size_ - position_) {
            fail("truncated header or data range");
        }
    }
    void skip(U64 bytes) {
        require(bytes);
        position_ += bytes;
        stream_.seekg(static_cast<std::streamoff>(position_));
        if (!stream_) {
            fail("seek failed");
        }
    }
    U64 integer(unsigned bytes) {
        require(bytes);
        U64 result = 0;
        for (unsigned i = 0; i < bytes; ++i) {
            const auto ch = stream_.get();
            if (ch == std::char_traits<char>::eof()) {
                fail("read failed");
            }
            const unsigned shift = 8 * (big_ ? bytes - i - 1 : i);
            result |= static_cast<U64>(static_cast<unsigned char>(ch)) << shift;
        }
        position_ += bytes;
        return result;
    }
    std::string string(bool retain = true) {
        const U64 bytes = integer(8);
        require(bytes);
        if (!retain) {
            skip(bytes);
            return {};
        }
        // Names and retained scalar strings are bounded; tokenizer strings are skipped.
        if (bytes > 1024ULL * 1024) {
            fail("name or scalar string exceeds 1 MiB");
        }
        std::string result(static_cast<std::size_t>(bytes), '\0');
        stream_.read(result.data(), static_cast<std::streamsize>(bytes));
        if (!stream_) {
            fail("read failed");
        }
        position_ += bytes;
        return result;
    }

   private:
    std::ifstream stream_;
    U64 size_ = 0;
    U64 position_ = 0;
    bool big_ = false;
};
unsigned width(std::uint32_t type) {
    constexpr std::array<unsigned, 13> widths{1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8};
    if (type >= widths.size()) {
        fail("unknown metadata type " + std::to_string(type));
    }
    return widths.at(type);
}
bool unsigned_type(std::uint32_t type) { return type == 0 || type == 2 || type == 4 || type == 10; }
Metadata metadata(Reader& reader, bool retain) {
    Metadata value;
    value.type = static_cast<std::uint32_t>(reader.integer(4));
    if (value.type == 8) {
        value.text = reader.string(retain);
    } else if (value.type == 9) {
        value.element_type = static_cast<std::uint32_t>(reader.integer(4));
        value.count = reader.integer(8);
        if (value.element_type == 9) {
            fail("nested metadata array");
        }
        const unsigned bytes = width(value.element_type);
        if (value.element_type == 8) {
            reader.require(mul(value.count, 8));
            for (U64 i = 0; i < value.count; ++i) {
                reader.string(false);
            }
        } else if (retain && unsigned_type(value.element_type)) {
            reader.require(mul(value.count, bytes));
            if (value.count > 1000000) {
                fail("profile metadata array too large");
            }
            for (U64 i = 0; i < value.count; ++i) {
                value.integers.push_back(reader.integer(bytes));
            }
        } else {
            reader.skip(mul(value.count, bytes));
        }
    } else {
        const auto raw = reader.integer(width(value.type));
        if (unsigned_type(value.type)) {
            value.integers.push_back(raw);
        }
    }
    return value;
}
U64 tensor_size(const TensorInfo& tensor) {
    // ggml type IDs and block sizes, llama.cpp cd959bae99ff4bed22bef2604f45241c09bbb8c8.
    // Removed IDs have zero sizes. Q8_1 is 36 bytes in this pinned revision.
    constexpr std::array<std::pair<U64, U64>, 44> types{
        {{1, 4},    {1, 2},    {32, 18},   {32, 20},   {0, 0},     {0, 0},     {32, 22},   {32, 24},  {32, 34},
         {32, 36},  {256, 84}, {256, 110}, {256, 144}, {256, 176}, {256, 210}, {256, 292}, {256, 66}, {256, 74},
         {256, 98}, {256, 50}, {32, 18},   {256, 110}, {256, 82},  {256, 136}, {1, 1},     {1, 2},    {1, 4},
         {1, 8},    {1, 8},    {256, 56},  {1, 2},     {0, 0},     {0, 0},     {0, 0},     {256, 54}, {256, 66},
         {0, 0},    {0, 0},    {0, 0},     {32, 17},   {64, 36},   {128, 18},  {64, 18},   {1, 1}}};
    if (tensor.type >= types.size() || types.at(tensor.type).first == 0) {
        fail(tensor.name + ": unsupported tensor type " + std::to_string(tensor.type));
    }
    const auto [elements, bytes] = types.at(tensor.type);
    if (tensor.shape.front() % elements != 0) {
        fail(tensor.name + ": first dimension not divisible by block size");
    }
    U64 result = mul(tensor.shape.front() / elements, bytes);
    for (std::size_t i = 1; i < tensor.shape.size(); ++i) {
        result = mul(result, tensor.shape[i]);
    }
    return result;
}
U64 scalar(const GgufHeader& header, const std::string& key) {
    const auto it = header.metadata.find(key);
    if (it == header.metadata.end() || !unsigned_type(it->second.type) || it->second.integers.size() != 1) {
        fail("missing or invalid unsigned scalar " + key);
    }
    return it->second.integers.front();
}
struct LayerIndex {
    std::size_t index;
    std::size_t count;
};
U64 per_layer(const GgufHeader& header, const std::string& key, LayerIndex layer) {
    const auto it = header.metadata.find(key);
    if (it == header.metadata.end()) {
        fail("missing " + key);
    }
    const auto& value = it->second;
    if (value.type != 9) {
        return scalar(header, key);
    }
    if (!unsigned_type(value.element_type) || value.integers.size() != layer.count) {
        fail("invalid layer array " + key);
    }
    return value.integers[layer.index];
}

TensorInfo read_tensor(Reader& reader, U64 alignment) {
    TensorInfo tensor;
    tensor.name = reader.string();
    const U64 dimensions = reader.integer(4);
    if (dimensions == 0 || dimensions > 4) {
        fail(tensor.name + ": invalid dimension count");
    }
    for (U64 dim = 0; dim < dimensions; ++dim) {
        const U64 length = reader.integer(8);
        if (length == 0) {
            fail(tensor.name + ": zero dimension");
        }
        tensor.shape.push_back(length);
    }
    tensor.type = static_cast<std::uint32_t>(reader.integer(4));
    tensor.offset = reader.integer(8);
    if (tensor.offset % alignment != 0) {
        fail(tensor.name + ": unaligned offset");
    }
    tensor.size_bytes = tensor_size(tensor);
    return tensor;
}

void validate_ranges(GgufHeader& header) {
    if (header.data_start > header.file_size) {
        fail("data section beyond file");
    }
    std::vector<std::pair<U64, U64>> ranges;
    for (auto& tensor : header.tensors) {
        tensor.absolute_offset = add(header.data_start, tensor.offset);
        const U64 end = add(tensor.absolute_offset, tensor.size_bytes);
        if (end > header.file_size) {
            fail(tensor.name + ": tensor beyond file");
        }
        ranges.emplace_back(tensor.offset, add(tensor.offset, tensor.size_bytes));
    }
    std::ranges::sort(ranges);
    U64 end = 0;
    for (const auto& range : ranges) {
        if (range.first < end) {
            fail("overlapping tensors");
        }
        end = range.second;
    }
}

void assign_weights(const GgufHeader& header, ModelProfile& model) {
    std::vector<bool> present(model.layers.size(), false);
    for (const auto& tensor : header.tensors) {
        if (!tensor.name.starts_with("blk.")) {
            model.nonblock_tensors.emplace(tensor.name, tensor.size_bytes);
            continue;
        }
        const auto dot = tensor.name.find('.', 4);
        U64 layer = 0;
        if (dot == std::string::npos || dot + 1 == tensor.name.size()) {
            fail("invalid block name " + tensor.name);
        }
        const auto result = std::from_chars(tensor.name.data() + 4, tensor.name.data() + dot, layer);
        if (result.ec != std::errc{} || result.ptr != tensor.name.data() + dot || layer >= model.layers.size()) {
            fail("invalid block index " + tensor.name);
        }
        model.layers[layer].weight_bytes = add(model.layers[layer].weight_bytes, tensor.size_bytes);
        present[layer] = true;
    }
    if (std::ranges::find(present, false) != present.end()) {
        fail("model has missing block tensors");
    }
}
}  // namespace

GgufHeader read_gguf_header(const std::filesystem::path& file) {
    Reader reader(file);
    GgufHeader header;
    header.file_size = reader.size();
    if (reader.integer(4) != 0x46554747) {
        fail("bad magic");
    }
    U64 version = reader.integer(4);
    if (version == 0x03000000) {
        reader.set_big_endian();
        version = 3;
    }
    if (version != 2 && version != 3) {
        fail("unsupported version " + std::to_string(version));
    }
    header.version = static_cast<std::uint32_t>(version);
    header.big_endian = reader.big_endian();
    const U64 tensor_count = reader.integer(8);
    const U64 metadata_count = reader.integer(8);
    reader.require(add(mul(tensor_count, 32), mul(metadata_count, 13)));
    for (U64 i = 0; i < metadata_count; ++i) {
        const auto key = reader.string();
        const bool retain = key == "general.architecture" || key == "general.alignment" || key == "split.count" ||
                            key.ends_with(".block_count") || key.ends_with(".embedding_length") ||
                            // C++20 has no string::contains; no Abseil dependency.
                            key.find(".attention.") != std::string::npos;  // NOLINT(abseil-string-find-str-contains)
        if (!header.metadata.emplace(key, metadata(reader, retain)).second) {
            fail("duplicate metadata " + key);
        }
    }
    U64 alignment = 32;
    if (header.metadata.contains("general.alignment")) {
        if (header.metadata.at("general.alignment").type != 4) {
            fail("general.alignment must be UINT32");
        }
        alignment = scalar(header, "general.alignment");
    }
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        fail("alignment must be a power of two");
    }
    std::set<std::string> names;
    for (U64 i = 0; i < tensor_count; ++i) {
        auto tensor = read_tensor(reader, alignment);
        if (tensor.name.empty() || !names.insert(tensor.name).second) {
            fail("empty or duplicate tensor name");
        }
        header.tensor_bytes = add(header.tensor_bytes, tensor.size_bytes);
        header.tensors.push_back(std::move(tensor));
    }
    header.header_end = reader.position();
    header.data_start = tensor_count == 0 ? reader.position() : align(reader.position(), alignment);
    validate_ranges(header);
    return header;
}

ModelProfile build_profile(const GgufHeader& header, U64 context_tokens) {
    if (context_tokens == 0) {
        fail("context must be positive");
    }
    if (header.metadata.contains("split.count") && scalar(header, "split.count") > 1) {
        fail("RAM profile requires an unsplit model");
    }
    ModelProfile model;
    const auto arch = header.metadata.find("general.architecture");
    if (arch == header.metadata.end() || arch->second.type != 8) {
        fail("missing general.architecture");
    }
    model.architecture = arch->second.text;
    // Conservative allowlist: these architectures use standard MHA/GQA KV storage.
    const std::set<std::string> supported{"llama", "qwen2", "qwen2moe", "qwen3", "qwen3moe", "mistral"};
    if (!supported.contains(model.architecture)) {
        fail("unsupported attention architecture " + model.architecture);
    }
    const auto prefix = model.architecture + ".";
    const U64 count = scalar(header, prefix + "block_count");
    if (count == 0 || count > 1000000) {
        fail("invalid block_count");
    }
    model.layers.resize(static_cast<std::size_t>(count));
    model.context_tokens = context_tokens;
    for (std::size_t i = 0; i < model.layers.size(); ++i) {
        const auto field = [&](const std::string& name) {
            return per_layer(header, prefix + name, {.index = i, .count = model.layers.size()});
        };
        const U64 heads = field("attention.head_count");
        const U64 kv_heads =
            header.metadata.contains(prefix + "attention.head_count_kv") ? field("attention.head_count_kv") : heads;
        if (heads == 0 || kv_heads == 0 || kv_heads > heads || heads % kv_heads != 0) {
            fail("invalid attention heads");
        }
        const auto dimension = [&](const std::string& name) {
            if (header.metadata.contains(prefix + name)) {
                return field(name);
            }
            const U64 embedding = field("embedding_length");
            if (embedding == 0 || embedding % heads != 0) {
                fail("embedding_length not divisible by head_count");
            }
            return embedding / heads;
        };
        const U64 key = dimension("attention.key_length");
        const U64 value = dimension("attention.value_length");
        if (key == 0 || value == 0) {
            fail("zero attention dimension");
        }
        model.layers[i].kv_bytes = mul(mul(mul(context_tokens, kv_heads), add(key, value)), 2);
    }
    assign_weights(header, model);
    return model;
}

std::vector<NodeMemory> estimate_ram(const ModelProfile& model, const std::vector<NodePlacement>& nodes) {
    std::vector<bool> assigned(model.layers.size(), false);
    std::set<std::string> assigned_nonblock;
    std::set<std::string> names;
    std::vector<NodeMemory> result;
    for (const auto& node : nodes) {
        if (node.name.empty() || !names.insert(node.name).second) {
            fail("empty or duplicate node name");
        }
        if (node.first > node.end || node.end > model.layers.size()) {
            fail("invalid layer range for " + node.name);
        }
        NodeMemory memory;
        memory.name = node.name;
        for (U64 i = node.first; i < node.end; ++i) {
            if (assigned[i]) {
                fail("layer assigned more than once");
            }
            assigned[i] = true;
            memory.weight_bytes = add(memory.weight_bytes, model.layers[i].weight_bytes);
            memory.kv_bytes = add(memory.kv_bytes, model.layers[i].kv_bytes);
        }
        std::set<std::string> local;
        for (const auto& tensor : node.nonblock_tensors) {
            const auto it = model.nonblock_tensors.find(tensor);
            if (it == model.nonblock_tensors.end()) {
                fail("unknown nonblock tensor " + tensor);
            }
            if (!local.insert(tensor).second) {
                fail("duplicate nonblock tensor on node " + node.name);
            }
            assigned_nonblock.insert(tensor);
            memory.nonblock_bytes = add(memory.nonblock_bytes, it->second);
        }
        memory.engine_buffer_bytes = node.engine_buffer_bytes;
        memory.overhead_bytes = node.overhead_bytes;
        memory.limit_bytes = node.limit_bytes;
        memory.total_bytes =
            add(add(add(add(memory.weight_bytes, memory.nonblock_bytes), memory.kv_bytes), memory.engine_buffer_bytes),
                memory.overhead_bytes);
        memory.fits = memory.total_bytes <= memory.limit_bytes;
        result.push_back(std::move(memory));
    }
    if (std::ranges::find(assigned, false) != assigned.end()) {
        fail("unassigned layer");
    }
    if (assigned_nonblock.size() != model.nonblock_tensors.size()) {
        fail("unassigned nonblock tensor");
    }
    return result;
}
}  // namespace swarm
