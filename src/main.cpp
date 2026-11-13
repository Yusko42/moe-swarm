#include <CLI/CLI.hpp>
#include <cstdlib>

int main(int argc, char** argv) {
    try {
        CLI::App app{"Launch of large language models on localy connected PCs"};

        app.set_version_flag("-v,--version", "v0.0.1");

        CLI11_PARSE(app, argc, argv);

        return 0;
    } catch (...) {
        return EXIT_FAILURE;
    }
}
