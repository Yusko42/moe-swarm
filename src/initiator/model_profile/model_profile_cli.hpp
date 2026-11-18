// Keep guards independent of checkout path (LLVM otherwise embeds absolute paths).
#ifndef SRC_INITIATOR_MODEL_PROFILE_MODEL_PROFILE_CLI_HPP  // NOLINT(llvm-header-guard)
#define SRC_INITIATOR_MODEL_PROFILE_MODEL_PROFILE_CLI_HPP
#include <cstdint>
#include <string>

namespace swarm {
struct ProfileOptions {
    std::string file;
    std::string placement;
    std::uint64_t context = 8192;
    bool tensors = false;
    bool header_only = false;
    bool bytes = false;
};
void print_model_profile(const ProfileOptions& options);
}  // namespace swarm
#endif
