#pragma once  // NOLINT(llvm-header-guard)

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace swarm::initiator {

struct CreateArguments {
    std::string_view model;
    std::string_view memory;
    std::optional<std::string_view> port;
};

struct CreateOptions {
    std::filesystem::path model_path;
    unsigned int memory_gib;
    std::uint16_t port;
};

using CreateResult = std::variant<CreateOptions, std::string>;

CreateResult validate_create_options(const CreateArguments& args, std::uint64_t available_ram_bytes);

}  // namespace swarm::initiator