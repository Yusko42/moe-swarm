#include <CLI/CLI.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>

#include "model_profile_cli.hpp"

int main(int argc, char** argv) {
    try {
        CLI::App app{"Launch of large language models on localy connected PCs"};

        app.set_version_flag("-v,--version", "v0.0.1");

        swarm::ProfileOptions options;
        auto* profile = app.add_subcommand("model-profile", "Inspect GGUF headers and estimate node RAM (F16 KV)");
        profile->add_option("file", options.file, "GGUF file")->required();
        profile->add_option("--placement", options.placement, "Node placement file; memory parameters in bytes");
        profile->add_option("--context", options.context, "KV context tokens")->check(CLI::PositiveNumber);
        profile->add_flag("--tensors", options.tensors, "Print tensor descriptors");
        profile->add_flag("--header-only", options.header_only, "Inspect without attention/RAM profiling");
        profile->add_flag("--bytes", options.bytes, "Print node table in exact bytes instead of MiB");
        profile->callback([&] { swarm::print_model_profile(options); });

        CLI11_PARSE(app, argc, argv);

        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
