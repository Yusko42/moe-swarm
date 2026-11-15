#include "join_options.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <system_error>

namespace swarm::neighbor {
namespace {
constexpr std::uint64_t kBytesPerGib = 1024ULL * 1024 * 1024;

std::optional<unsigned int> parse_decimal(std::string_view input) {
    if (input.empty()) {
        return std::nullopt;
    }

    unsigned int value = 0;
    const auto* end = input.data() + input.size();
    const auto result = std::from_chars(input.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }

    return value;
}

std::optional<std::uint32_t> parse_ipv4(std::string_view input) {
    std::uint32_t address = 0;
    for (int index = 0; index < 4; ++index) {
        const auto dot = input.find('.');
        if ((dot == std::string_view::npos) != (index == 3)) {
            return std::nullopt;
        }

        const auto part = input.substr(0, dot);
        if (part.size() > 3 || (part.size() > 1 && part.front() == '0')) {
            return std::nullopt;
        }

        const auto octet = parse_decimal(part);
        if (!octet.has_value() || *octet > 255) {
            return std::nullopt;
        }

        address = (address << 8) | *octet;
        if (dot != std::string_view::npos) {
            input.remove_prefix(dot + 1);
        }
    }

    return address;
}
}  // namespace

JoinResult validate_join_options(const JoinArguments& args, std::span<const Ipv4Interface> interfaces,
                                 std::uint64_t available_ram_bytes) {
    const auto separator = args.address.find(':');
    if (args.address.size() > 21 || separator == std::string_view::npos) {
        return std::string{"Параметр адрес роя: ожидается IPv4:порт длиной не более 21 символа"};
    }

    const auto ip_text = args.address.substr(0, separator);
    const auto port_text = args.address.substr(separator + 1);
    const auto ip = parse_ipv4(ip_text);
    const auto port = parse_decimal(port_text);
    if (!ip.has_value() || !port.has_value() || *port == 0 || *port > 65535) {
        return std::string{"Параметр адрес роя: ожидается корректный IPv4 и порт от 1 до 65535"};
    }

    const auto memory = parse_decimal(args.memory);
    if (!memory.has_value() || args.memory.size() > 3 || *memory == 0) {
        return std::string{"Параметр --memory: требуется целое число от 1 до 999"};
    }

    const bool same_subnet = std::ranges::any_of(interfaces, [address = *ip](const Ipv4Interface& interface) {
        return (address & interface.netmask) == (interface.address & interface.netmask);
    });
    if (!same_subnet) {
        return std::string{"Параметр адрес роя: адрес должен находиться в той же IP-подсети"};
    }

    const auto available_gib = available_ram_bytes / kBytesPerGib;
    if (*memory > available_gib) {
        const auto available = std::to_string(available_gib);
        return "Свободно " + available + " ГБ, указано " + std::to_string(*memory) + " ГБ. Укажите не больше " +
               available;
    }

    return JoinOptions{std::string{ip_text}, static_cast<std::uint16_t>(*port), *memory};
}
}  // namespace swarm::neighbor
