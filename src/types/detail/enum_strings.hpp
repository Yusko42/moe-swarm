#pragma once  // NOLINT(llvm-header-guard)

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

namespace swarm::types::detail {

template <typename Enum, std::size_t Size>
[[nodiscard]] constexpr std::string_view enum_name(Enum value,
                                                   const std::array<std::pair<Enum, std::string_view>, Size>& entries) {
    for (const auto& [candidate, name] : entries) {
        if (candidate == value) {
            return name;
        }
    }
    return {};
}

template <typename Enum, std::size_t Size>
[[nodiscard]] constexpr std::optional<Enum> enum_value(
    std::string_view name, const std::array<std::pair<Enum, std::string_view>, Size>& entries) {
    for (const auto& [value, candidate] : entries) {
        if (candidate == name) {
            return value;
        }
    }
    return std::nullopt;
}

}  // namespace swarm::types::detail
