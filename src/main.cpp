#include <CLI/CLI.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>

#include "create_command.hpp"

int main(int argc, char** argv) {
    try {
        swarm::initiator::CreateCliArguments args;
        CLI::App app{"Launch large language models on locally connected PCs", "swarm"};
        app.set_version_flag("-v,--version", "v0.0.1");
        app.require_subcommand(1);

        const auto& create = swarm::initiator::add_create_command(app, args);
        CLI11_PARSE(app, argc, app.ensure_utf8(argv));

        if (create.parsed()) {
            return swarm::initiator::run_create(args);
        }
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}