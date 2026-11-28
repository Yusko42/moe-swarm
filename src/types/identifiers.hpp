#pragma once  // NOLINT(llvm-header-guard)

#include <array>
#include <cstdint>

namespace swarm::types {

// Architecture v0.5, section 7.2. On the wire: UUID v4, 36 characters.
using Uuid = std::array<std::uint8_t, 16>;
using SwarmId = Uuid;
using NodeId = Uuid;
using OperationId = Uuid;
using RequestId = Uuid;

// Machine hash; on the wire: 64 hexadecimal characters.
using NodeHwId = std::array<std::uint8_t, 32>;

}  // namespace swarm::types
