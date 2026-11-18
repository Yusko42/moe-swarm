#include <CLI/CLI.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "join_command.hpp"
#include "model_profile_cli.hpp"

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

        swarm::ProfileOptions options;
        auto* profile = app.add_subcommand("model-profile", "Inspect GGUF headers and estimate node RAM (F16 KV)");
        profile->add_option("file", options.file, "GGUF file")->required();
        profile->add_option("--placement", options.placement, "Node placement file; memory parameters in bytes");
        profile->add_option("--context", options.context, "KV context tokens")->check(CLI::PositiveNumber);
        profile->add_flag("--tensors", options.tensors, "Print tensor descriptors");
        profile->add_flag("--header-only", options.header_only, "Inspect without attention/RAM profiling");
        profile->add_flag("--bytes", options.bytes, "Print node table in exact bytes instead of MiB");
        profile->callback([&] { swarm::print_model_profile(options); });

        try {
            app.parse(argc, argv);
        } catch (const CLI::ArgumentMismatch& error) {
            if (join->parsed()) {
                std::cerr << "Параметр --memory: требуется одно значение\n";
                return EXIT_FAILURE;
            }
            return app.exit(error);
        } catch (const CLI::RequiredError& error) {
            if (profile->parsed()) {
                return app.exit(error);
            }
            if (argc > 1) {
                std::cerr << "Неизвестная команда: " << argv[1] << '\n';
                return EXIT_FAILURE;
            }
            return app.exit(error);
        } catch (const CLI::ParseError& error) {
            return app.exit(error);
        }

        if (profile->parsed()) {
            return EXIT_SUCCESS;
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
