#include "model_profile.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
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
bool unsigned_type(std::uint32_t type) { return type == 0 || type == 2 || type == 4 || type == 10; }
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
