/**
 * @file acRtosConfig.hpp
 * @brief Global configuration parameters and kernel limits.
* @details This file defines statically allocated boundaries for the RTOS,
 *          such as maximum task count and default stack sizes. Modifying 
 *          these values directly impacts the kernel's RAM footprint.
 */

#pragma once
#include <cstddef>
#include <cstdint>

namespace acrtos {

/** @brief Maximum number of active tasks, including the idle task. */
inline constexpr std::size_t kMaxTasks = 4;

/** @brief Default task stack size in 32-bit words (e.g., 256 = 1024 bytes). */
inline constexpr std::size_t kStackSize = 256; // 1024 bytes

/** @brief Number of priority levels (0 to kMaxPriorities - 1). */
inline constexpr std::size_t kMaxPriorities = 32;

/** @brief System tick interval in milliseconds. */
inline constexpr std::uint32_t kTickRateMs = 1;

} // namespace acrtos

#ifndef ACRTOS_USE_ASSERT
#define ACRTOS_USE_ASSERT 1
#endif

extern "C" void acrtos_assert_failed(const char* file, int line);

#if ACRTOS_USE_ASSERT
#define ACRTOS_ASSERT(expr) \
    do { if (!(expr)) { acrtos_assert_failed(__FILE__, __LINE__); } } while (0)
#else
#define ACRTOS_ASSERT(expr) do {} while (0)
#endif