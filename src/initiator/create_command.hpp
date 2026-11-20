#pragma once  // NOLINT(llvm-header-guard)

#include <optional>
#include <string>

namespace CLI {
class App;
}  // namespace CLI

namespace swarm::initiator {

struct CreateCliArguments {
    std::string model{};
    std::string memory{};
    std::optional<std::string> port{};
};

CLI::App& add_create_command(CLI::App& app, CreateCliArguments& args);
int run_create(const CreateCliArguments& args);

}  // namespace swarm::initiator