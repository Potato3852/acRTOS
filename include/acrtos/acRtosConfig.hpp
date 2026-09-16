/**
 * @file acRtosConfig.hpp
 * @brief Compile-time kernel limits and port defaults.
 *
 * Change these to size RAM (task count × stack) and to match the CPU clock
 * the application actually configured. The kernel does not talk to STM32 HAL.
 */
#pragma once
#include <cstddef>
#include <cstdint>

namespace acrtos::config {

/** @brief Maximum number of tasks, including the idle task created in start(). */
inline constexpr std::size_t kMaxTasks = 4;

/** @brief Default task stack size in 32-bit words (256 words = 1024 bytes). */
inline constexpr std::size_t kStackSize = 256;

/** @brief Priority levels 0 .. kMaxPriorities-1. Higher number = more urgent. */
inline constexpr std::size_t kMaxPriorities = 32;

/** @brief One kernel tick in milliseconds (SysTick period). */
inline constexpr std::uint32_t kTickRateMs = 1;

/**
 * @brief CPU frequency used to program SysTick.
 * @details Must match SYSCLK. Default is STM32 HSI 16 MHz. If you enable the
 *          PLL in the application, call acrtos::port::set_cpu_hz() before start().
 */
inline constexpr std::uint32_t kCpuHz = 16'000'000;

static_assert(kMaxPriorities <= 32, "ReadyManager uses a 32-bit bitmask; kMaxPriorities cannot exceed 32.");
static_assert(kMaxPriorities > 0, "Need at least one priority level.");
static_assert(kMaxTasks > 0, "Need at least one TCB slot (idle task).");
static_assert((kStackSize * sizeof(std::uint32_t)) % 8 == 0, "Stack size in bytes must be 8-byte aligned (AAPCS).");
static_assert(kTickRateMs > 0, "Tick period must be at least 1 ms.");
static_assert(kCpuHz >= 1000, "CPU clock too low to derive a 1 ms SysTick.");

} // namespace acrtos::config

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