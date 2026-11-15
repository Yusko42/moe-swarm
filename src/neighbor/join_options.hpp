#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace swarm::neighbor {
struct JoinArguments {
    std::string_view address;
    std::string_view memory;
};

struct Ipv4Interface {
    std::uint32_t address;
    std::uint32_t netmask;
};

struct JoinOptions {
    std::string ip;
    std::uint16_t port;
    unsigned int memory_gib;
};

using JoinResult = std::variant<JoinOptions, std::string>;

JoinResult validate_join_options(const JoinArguments& args, std::span<const Ipv4Interface> interfaces,
                                 std::uint64_t available_ram_bytes);
}  // namespace swarm::neighbor
