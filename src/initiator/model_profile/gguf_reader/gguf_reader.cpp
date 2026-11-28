#include "gguf_reader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
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
    void retain(U64 bytes) {
        constexpr U64 budget = 64ULL * 1024 * 1024;
        if (bytes > budget - retained_) {
            fail("decoded metadata exceeds 64 MiB");
        }
        retained_ += bytes;
    }
    std::string string() {
        const U64 bytes = integer(8);
        require(bytes);
        if (bytes > 1024ULL * 1024) {
            fail("name or scalar string exceeds 1 MiB");
        }
        retain(bytes);
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
    U64 retained_ = 0;
};
unsigned width(std::uint32_t type) {
    constexpr std::array<unsigned, 13> widths{1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8};
    if (type >= widths.size()) {
        fail("unknown metadata type " + std::to_string(type));
    }
    return widths.at(type);
}
bool unsigned_type(std::uint32_t type) { return type == 0 || type == 2 || type == 4 || type == 10; }
void append_value(Reader& reader, Metadata& value, std::uint32_t type) {
    if (type == 8) {
        value.strings.push_back(reader.string());
        return;
    }
    const auto bytes = width(type);
    auto raw = reader.integer(bytes);
    if (unsigned_type(type)) {
        value.integers.push_back(raw);
    } else if (type == 1 || type == 3 || type == 5 || type == 11) {
        if (bytes < 8 && (raw & (U64{1} << (bytes * 8 - 1))) != 0) {
            raw |= std::numeric_limits<U64>::max() << (bytes * 8);
        }
        value.signed_integers.push_back(std::bit_cast<std::int64_t>(raw));
    } else if (type == 6) {
        value.reals.push_back(std::bit_cast<float>(static_cast<std::uint32_t>(raw)));
    } else if (type == 12) {
        value.reals.push_back(std::bit_cast<double>(raw));
    } else if (type == 7) {
        if (raw > 1) {
            fail("invalid boolean value");
        }
        value.booleans.push_back(raw != 0);
    }
}
Metadata metadata(Reader& reader) {
    Metadata value;
    value.type = static_cast<std::uint32_t>(reader.integer(4));
    if (value.type == 8) {
        value.text = reader.string();
    } else if (value.type == 9) {
        value.element_type = static_cast<std::uint32_t>(reader.integer(4));
        value.count = reader.integer(8);
        if (value.element_type == 9) {
            fail("nested metadata array");
        }
        const auto bytes = width(value.element_type);
        reader.require(mul(value.count, value.element_type == 8 ? 8 : bytes));
        if (value.count > 1000000) {
            fail("metadata array too large");
        }
        // Account for decoded storage before growing vectors, including string objects.
        reader.retain(mul(value.count, value.element_type == 8 ? sizeof(std::string) : sizeof(U64)));
        for (U64 i = 0; i < value.count; ++i) {
            append_value(reader, value, value.element_type);
        }
    } else {
        reader.retain(sizeof(U64));
        append_value(reader, value, value.type);
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
        if (!header.metadata.emplace(key, metadata(reader)).second) {
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
    header.alignment = static_cast<std::uint32_t>(alignment);
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

}  // namespace swarm
