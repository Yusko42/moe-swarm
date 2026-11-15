#include "join_command.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <cstring>
#include <fstream>
#include <sstream>
#endif

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace swarm::neighbor {
namespace {
#ifdef _WIN32

std::vector<Ipv4Interface> local_ipv4_interfaces() {
    MIB_UNICASTIPADDRESS_TABLE* raw_table = nullptr;
    const auto status = GetUnicastIpAddressTable(AF_INET, &raw_table);
    if (status == ERROR_NOT_FOUND) {
        return {};
    }
    if (status != NO_ERROR) {
        throw std::runtime_error("Не удалось получить список сетевых интерфейсов");
    }
    const std::unique_ptr<MIB_UNICASTIPADDRESS_TABLE, decltype(&FreeMibTable)> table{raw_table, FreeMibTable};

    constexpr auto kIpv4Bits = std::numeric_limits<std::uint32_t>::digits;
    constexpr auto kFullMask = std::numeric_limits<std::uint32_t>::max();
    std::vector<Ipv4Interface> interfaces;
    for (ULONG index = 0; index < table->NumEntries; ++index) {
        const auto& row = table->Table[index];
        const auto prefix = row.OnLinkPrefixLength;
        if (prefix == 0 || prefix > kIpv4Bits ||
            (row.DadState != IpDadStatePreferred && row.DadState != IpDadStateDeprecated)) {
            continue;
        }
        const auto mask = kFullMask << (kIpv4Bits - prefix);
        interfaces.push_back({ntohl(row.Address.Ipv4.sin_addr.s_addr), mask});
    }
    return interfaces;
}

std::uint64_t available_ram_bytes() {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) == 0) {
        throw std::runtime_error("Не удалось определить объём доступной оперативной памяти");
    }
    return memory.ullAvailPhys;
}

#else

std::uint32_t ipv4_value(const sockaddr& address) {
    sockaddr_in ipv4{};
    std::memcpy(&ipv4, &address, sizeof(ipv4));
    return ntohl(ipv4.sin_addr.s_addr);
}

std::vector<Ipv4Interface> local_ipv4_interfaces() {
    ifaddrs* raw_interfaces = nullptr;
    if (getifaddrs(&raw_interfaces) != 0) {
        throw std::runtime_error("Не удалось получить список сетевых интерфейсов");
    }
    const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> owner{raw_interfaces, freeifaddrs};

    std::vector<Ipv4Interface> interfaces;
    for (const auto* entry = owner.get(); entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_netmask == nullptr || entry->ifa_addr->sa_family != AF_INET ||
            entry->ifa_netmask->sa_family != AF_INET || (entry->ifa_flags & static_cast<unsigned int>(IFF_UP)) == 0) {
            continue;
        }
        const auto netmask = ipv4_value(*entry->ifa_netmask);
        if (netmask != 0) {
            interfaces.push_back({ipv4_value(*entry->ifa_addr), netmask});
        }
    }
    return interfaces;
}

std::uint64_t available_ram_bytes() {
    std::ifstream meminfo{"/proc/meminfo"};
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.starts_with("MemAvailable:")) {
            std::istringstream field{line};
            std::string name;
            std::uint64_t kib = 0;
            if (field >> name >> kib) {
                return kib * 1024;
            }
            break;
        }
    }
    throw std::runtime_error("Не удалось определить объём доступной оперативной памяти");
}

#endif
}  // namespace

int run_join(const JoinArguments& args) {
    const auto interfaces = local_ipv4_interfaces();
    const auto result = validate_join_options(args, interfaces, available_ram_bytes());
    if (const auto* error = std::get_if<std::string>(&result); error != nullptr) {
        std::cerr << *error << '\n';
        return EXIT_FAILURE;
    }

    const auto& options = std::get<JoinOptions>(result);
    std::cout << "Адрес роя: " << options.ip << ':' << options.port << '\n';
    std::cout << "Память: " << options.memory_gib << " ГБ\n";
    return EXIT_SUCCESS;
}
}  // namespace swarm::neighbor
