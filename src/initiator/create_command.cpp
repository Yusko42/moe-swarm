#include "create_command.hpp"
#include "http/http_server.hpp"
#include "registry/node_registry.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <fstream>
#include <sstream>
#endif

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "create_options.hpp"

namespace swarm::initiator {
namespace {

std::uint64_t available_ram_bytes() {
#ifdef _WIN32
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) != 0) {
        return memory.ullAvailPhys;
    }
#else
    std::ifstream meminfo{"/proc/meminfo"};
    std::string line;
    while (std::getline(meminfo, line)) {
        if (!line.starts_with("MemAvailable:")) {
            continue;
        }

        std::istringstream field{line};
        std::string name;
        std::uint64_t kib = 0;
        if (field >> name >> kib) {
            return kib * 1024;
        }
        break;
    }
#endif
    throw std::runtime_error("Не удалось определить объём доступной оперативной памяти");
}

}  // namespace

CLI::App& add_create_command(CLI::App& app, CreateCliArguments& args) {
    auto& create = *app.add_subcommand("create", "Create a swarm");
    create.add_option("--model", args.model, "Path to the model file (required)")->expected(0, 1);
    create.add_option("--memory", args.memory, "RAM limit in GiB (required)")->expected(0, 1);
    create
        .add_option_function<std::string>(
            "--port", [&args](const std::string& value) { args.port = value; }, "TCP port (default: 1042)")
        ->expected(0, 1);
    return create;
}

int run_create(const CreateCliArguments& args) {
    const auto result = validate_create_options({args.model, args.memory, args.port}, available_ram_bytes());
    if (const auto* error = std::get_if<std::string>(&result); error != nullptr) {
        std::cerr << *error << '\n';
        return EXIT_FAILURE;
    }

    const auto& options = std::get<CreateOptions>(result);

#ifndef NDEBUG
    std::clog << "Файл модели: " << args.model << '\n';
    std::clog << "Память: " << options.memory_gib << " ГБ\n";
    std::clog << "Порт: " << options.port << '\n';
#endif

    swarm::initiator::NodeRegistry registry;
    swarm::initiator::HttpServer server{registry, options.port};
    if (!server.bind()) {
        std::cerr << "Порт " << options.port << " занят. Укажите другой параметром --port\n";
        return EXIT_FAILURE;
    }
    std::cout << "Рой создан. Адрес: " << server.address() << std::endl;
    server.run();

    return EXIT_SUCCESS;
}

}  // namespace swarm::initiator