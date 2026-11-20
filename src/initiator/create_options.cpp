#include "create_options.hpp"

#include <charconv>
#include <fstream>
#include <system_error>
#include <utility>

namespace swarm::initiator {
namespace {

constexpr std::uint16_t kDefaultPort = 1042;
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

}  // namespace

CreateResult validate_create_options(const CreateArguments& args, std::uint64_t available_ram_bytes) {
    if (args.model.empty()) {
        return std::string{"Параметр --model: не указан путь к файлу модели"};
    }

    std::filesystem::path model_path;
    try {
        model_path = std::filesystem::path{std::u8string{args.model.begin(), args.model.end()}};
        if (model_path.u32string().size() > 255) {
            return std::string{"Параметр --model: путь должен содержать не более 255 символов"};
        }
    } catch (const std::filesystem::filesystem_error&) {
        return std::string{"Параметр --model: некорректный путь к файлу модели"};
    }

    std::error_code file_error;
    if (!std::filesystem::is_regular_file(model_path, file_error) || !std::ifstream{model_path, std::ios::binary}) {
        return std::string{"Параметр --model: файл не найден или недоступен для чтения"};
    }

    const auto memory = parse_decimal(args.memory);
    if (!memory.has_value() || args.memory.size() > 3 || *memory < 2) {
        return std::string{"Параметр --memory: требуется целое число от 2 до 999"};
    }

    std::uint16_t port = kDefaultPort;
    if (args.port.has_value()) {
        const auto value = parse_decimal(*args.port);
        if (!value.has_value() || args.port->size() > 5 || *value < 1024 || *value > 65535) {
            return std::string{"Параметр --port: требуется целое число от 1024 до 65535"};
        }
        port = static_cast<std::uint16_t>(*value);
    }

    const auto available_gib = available_ram_bytes / kBytesPerGib;
    if (*memory > available_gib) {
        const auto available = std::to_string(available_gib);
        return "Свободно " + available + " ГБ, указано " + std::to_string(*memory) + " ГБ. Укажите не больше " +
               available;
    }

    return CreateOptions{std::move(model_path), *memory, port};
}

}  // namespace swarm::initiator