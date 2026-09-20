/**
 * @file types.hpp
 * @brief Core data types for the kernel.
 */
#pragma once
#include <cstdint>
#include "acRtosConfig.hpp"

namespace acrtos {

/** @brief Kernel time: both timestamps and delay lengths in ticks. */
using TickType = std::uint32_t;
inline constexpr TickType kWaitForever = 0xFFFFFFFF;

/** @brief Kernel base canary code */
inline constexpr std::uint32_t kStackCanary = 0xDEADBEEF;

/**
 * Convert a millisecond delay to tick count.
 * delay_ms(0) stays 0; any positive delay is at least one tick.
 */
[[nodiscard]] constexpr TickType ms_to_ticks(TickType ms) noexcept {
    if (ms == 0) return 0;
    return ms / config::kTickRateMs + (ms % config::kTickRateMs != 0 ? 1u : 0u);
}

enum class TaskState : std::uint8_t {
    Ready,
    Running,
    Blocked,
    Suspended
};

} // namespace acrtos