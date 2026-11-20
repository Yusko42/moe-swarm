#include <CLI/CLI.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "join_command.hpp"

int main(int argc, char** argv) {
    try {
        std::string address;
        std::string memory;
        CLI::App app{"Launch large language models on locally connected PCs", "swarm"};
        app.set_version_flag("-v,--version", "v0.0.1");
        app.require_subcommand(1);

        auto* join = app.add_subcommand("join", "Join a swarm");
        join->add_option("address", address, "Swarm address (IPv4:port)");
        join->add_option("--memory", memory, "Available memory to share (GiB)");

        auto* create = app.add_subcommand("create", "Create a swarm");
        create->allow_extras();

        try {
            app.parse(argc, argv);
        } catch (const CLI::ArgumentMismatch& error) {
            if (join->parsed()) {
                std::cerr << "Параметр --memory: требуется одно значение\n";
                return EXIT_FAILURE;
            }
            return app.exit(error);
        } catch (const CLI::RequiredError& error) {
            if (argc > 1) {
                std::cerr << "Неизвестная команда: " << argv[1] << '\n';
                return EXIT_FAILURE;
            }
            return app.exit(error);
        } catch (const CLI::ParseError& error) {
            return app.exit(error);
        }

        if (join->parsed()) {
            return swarm::neighbor::run_join({address, memory});
        }

        std::cerr << "create не реализована\n";
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}