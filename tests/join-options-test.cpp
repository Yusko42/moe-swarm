#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "join_options.hpp"

namespace swarm::neighbor {
namespace {

constexpr std::uint64_t kGib = 1024ULL * 1024 * 1024;
constexpr std::array<Ipv4Interface, 1> kInterfaces{{
    {0xC0A80114U, 0xFFFFFF80U},
}};
constexpr std::string_view kAddress = "192.168.1.10:1042";

TEST(JoinOptionsTest, AcceptsValidOptionsAtRamLimit) {
    const auto result = validate_join_options({kAddress, "4"}, kInterfaces, 4 * kGib);

    ASSERT_TRUE(std::holds_alternative<JoinOptions>(result));
    const auto& options = std::get<JoinOptions>(result);
    EXPECT_EQ(options.ip, "192.168.1.10");
    EXPECT_EQ(options.port, 1042);
    EXPECT_EQ(options.memory_gib, 4U);
}

TEST(JoinOptionsTest, RejectsInvalidAddresses) {
    constexpr std::array<std::string_view, 8> addresses{
        "",
        "192.168.1.10",
        "192.168.1.10:0",
        "192.168.1.10:70000",
        "192.168.1.256:1042",
        "localhost:1042",
        "192.168.1.10:1042x",
        "192.168.1.10:0000001042",
    };

    for (const auto address : addresses) {
        SCOPED_TRACE(address);
        const auto result = validate_join_options({address, "4"}, kInterfaces, 8 * kGib);

        ASSERT_TRUE(std::holds_alternative<std::string>(result));
        EXPECT_TRUE(std::get<std::string>(result).starts_with("Параметр адрес роя:"));
    }
}

TEST(JoinOptionsTest, RejectsInvalidMemoryValues) {
    constexpr std::array<std::string_view, 5> values{"", "0", "-1", "abc", "1000"};

    for (const auto memory : values) {
        SCOPED_TRACE(memory);
        const auto result = validate_join_options({kAddress, memory}, kInterfaces, 8 * kGib);

        ASSERT_TRUE(std::holds_alternative<std::string>(result));
        EXPECT_EQ(std::get<std::string>(result), "Параметр --memory: требуется целое число от 1 до 999");
    }
}

TEST(JoinOptionsTest, RejectsAddressOutsideLocalSubnet) {
    const auto result = validate_join_options({"192.168.1.130:1042", "4"}, kInterfaces, 8 * kGib);

    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    EXPECT_EQ(std::get<std::string>(result), "Параметр адрес роя: адрес должен находиться в той же IP-подсети");
}

TEST(JoinOptionsTest, ReportsInsufficientRam) {
    const auto result = validate_join_options({kAddress, "5"}, kInterfaces, 4 * kGib);

    ASSERT_TRUE(std::holds_alternative<std::string>(result));
    EXPECT_EQ(std::get<std::string>(result), "Свободно 4 ГБ, указано 5 ГБ. Укажите не больше 4");
}

}  // namespace
}  // namespace swarm::neighbor