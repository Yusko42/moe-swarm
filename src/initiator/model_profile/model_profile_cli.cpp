#include "model_profile_cli.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "model_profile.hpp"

namespace swarm {
namespace {
std::vector<NodePlacement> read_placement(const std::string& file) {
    std::ifstream stream(file);
    if (!stream) {
        throw std::runtime_error("Cannot open placement: " + file);
    }
    std::vector<NodePlacement> nodes;
    std::string line;
    while (std::getline(stream, line)) {
        const auto start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line[start] == '#') {
            continue;
        }
        std::istringstream row(line);
        NodePlacement node;
        std::array<std::string, 5> numbers;
        if (!(row >> std::quoted(node.name) >> numbers[0] >> numbers[1] >> numbers[2] >> numbers[3] >> numbers[4])) {
            throw std::runtime_error(
                "Placement expects: name first end engine_bytes overhead_bytes limit_bytes [tensors...]");
        }
        std::array<std::uint64_t*, 5> fields{&node.first, &node.end, &node.engine_buffer_bytes, &node.overhead_bytes,
                                             &node.limit_bytes};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto& number = numbers.at(i);
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), *fields.at(i));
            if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) {
                throw std::runtime_error("Invalid placement integer: " + number);
            }
        }
        std::string name;
        while (row >> std::quoted(name)) {
            node.nonblock_tensors.push_back(name);
        }
        if (!row.eof()) {
            throw std::runtime_error("Invalid placement tensor list");
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}
void print_tensors(const std::vector<TensorInfo>& tensors) {
    std::cout << "tensor\tname\ttype\tshape\tbytes\tabsolute_offset\n";
    for (const auto& tensor : tensors) {
        std::cout << "tensor\t" << tensor.name << '\t' << tensor.type << '\t';
        for (std::size_t i = 0; i < tensor.shape.size(); ++i) {
            std::cout << (i == 0 ? "" : ",") << tensor.shape[i];
        }
        std::cout << '\t' << tensor.size_bytes << '\t' << tensor.absolute_offset << '\n';
    }
}
}  // namespace

void print_model_profile(const ProfileOptions& options) {
    const auto& [file, placement, context, tensors, header_only, bytes] = options;
    const auto header = read_gguf_header(file);
    std::cout << "version\t" << header.version << "\nendian\t" << (header.big_endian ? "big" : "little")
              << "\nfile_bytes\t" << header.file_size << "\nheader_end\t" << header.header_end << "\ndata_start\t"
              << header.data_start << "\ntensor_bytes\t" << header.tensor_bytes << '\n';
    if (tensors) {
        print_tensors(header.tensors);
    }
    if (header_only) {
        if (!placement.empty()) {
            throw std::runtime_error("--header-only cannot be combined with --placement");
        }
        return;
    }
    const auto model = build_profile(header, context);
    std::cout << "architecture\t" << model.architecture << "\ncontext_tokens\t" << context << "\nlayer_count\t"
              << model.layers.size() << "\nlayer\tindex\tweight_bytes\tkv_bytes\n";
    for (std::size_t i = 0; i < model.layers.size(); ++i) {
        std::cout << "layer\t" << i << '\t' << model.layers[i].weight_bytes << '\t' << model.layers[i].kv_bytes << '\n';
    }
    for (const auto& [name, size] : model.nonblock_tensors) {
        std::cout << "nonblock\t" << name << '\t' << size << '\n';
    }
    if (placement.empty()) {
        return;
    }
    const auto nodes = estimate_ram(model, read_placement(placement));
    std::cout << "units\t" << (bytes ? "bytes" : "MiB")
              << "\nnode\tname\tweights\tnonblock\tkv\tengine\toverhead\ttotal\tlimit\tfits\n";
    const auto print = [bytes = options.bytes](std::uint64_t value) {
        if (bytes) {
            std::cout << value;
        } else {
            std::cout << std::fixed << std::setprecision(3) << static_cast<double>(value) / (1024 * 1024);
        }
        std::cout << '\t';
    };
    for (const auto& node : nodes) {
        std::cout << "node\t" << node.name << '\t';
        for (const auto value : {node.weight_bytes, node.nonblock_bytes, node.kv_bytes, node.engine_buffer_bytes,
                                 node.overhead_bytes, node.total_bytes, node.limit_bytes}) {
            print(value);
        }
        std::cout << (node.fits ? "yes" : "no") << '\n';
    }
}
}  // namespace swarm
