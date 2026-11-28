#pragma once  // NOLINT(llvm-header-guard)

#include <cstdint>

namespace swarm::types {

// Architecture v0.5, section 7.2. Range checks belong at protocol boundaries.
using DeploymentId = std::uint64_t;    // Monotonic fencing token, strictly below 2^53.
using SequenceNumber = std::uint64_t;  // seq: monotonic per node.
using Attempt = std::uint8_t;          // 1: initial request, 2: the only retry.
using Bytes = std::uint64_t;
using RttUs = std::uint32_t;            // Microseconds; convert to fractional ms for the UI.
using BandwidthBps = std::uint64_t;     // bandwidth_Bps and mem_bw_Bps: bytes per second.
using CpuScore = double;                // Normalized, dimensionless.
using MonoTimestampNs = std::uint64_t;  // ts_mono_ns: monotonic clock, nanoseconds.
using LayerIndex = std::uint16_t;       // [0, block_count); block_count comes from GGUF.
using ContextTokens = std::uint32_t;
using ProtocolVersion = std::uint16_t;

inline constexpr Bytes kGib = Bytes{1} << 30U;  // CLI/UI GB means GiB, not 10^9 bytes.
inline constexpr DeploymentId kMaxDeploymentId = (DeploymentId{1} << 53U) - 1U;
inline constexpr Attempt kMinAttempt = 1;
inline constexpr Attempt kMaxAttempt = 2;
inline constexpr ContextTokens kContextLimit = 8192;
inline constexpr ProtocolVersion kProtocolVersion = 1;

// Values that may exceed the JavaScript safe integer range must be serialized as
// strings. Bytes and counters are JSON numbers within their safe operating range.
// Wall-clock timestamp is a formatted string for logs/UI, not a monotonic scalar.

}  // namespace swarm::types
