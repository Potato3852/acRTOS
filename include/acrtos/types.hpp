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

/** @brief Timeout value meaning "block without a time limit". */
inline constexpr TickType kWaitForever = 0xFFFFFFFF;

/** @brief Kernel base canary code */
inline constexpr std::uint32_t kStackCanary = 0xDEADBEEF;

/**
 * @brief Convert milliseconds to ticks, rounding up.
 * @param ms Duration in milliseconds.
 * @return 0 for @p ms == 0; otherwise at least one tick.
 */
[[nodiscard]] constexpr TickType ms_to_ticks(TickType ms) noexcept {
    if (ms == 0) return 0;
    return ms / config::kTickRateMs + (ms % config::kTickRateMs != 0 ? 1u : 0u);
}

/** @brief Lifecycle state of a task. */
enum class TaskState : std::uint8_t {
    Ready,      ///< In a ready list, waiting for the CPU.
    Running,    ///< Currently executing (never in a ready list).
    Blocked,    ///< Waiting on a delay and/or an IPC object.
    Suspended,  ///< Removed from scheduling until resume().
    Deleted     ///< Terminal state; the TCB slot and stack are never reused.
};

} // namespace acrtos