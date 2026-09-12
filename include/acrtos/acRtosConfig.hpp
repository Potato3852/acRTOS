#pragma once
#include <cstddef>
#include <cstdint>

namespace acrtos {

inline constexpr std::size_t kMaxTasks = 4;
inline constexpr std::size_t kStackSize = 256; // 1024 bytes
inline constexpr std::size_t kMaxPriorities = 32;
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