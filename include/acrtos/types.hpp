#pragma once
#include <cstdint>
#include <cstddef>

namespace acrtos {

using TickType = uint32_t;

enum class TaskState : uint8_t {
    Ready,
    Running,
    Blocked,
    Suspended
};

constexpr size_t kMaxTasks = 4;
constexpr size_t kStackSize = 256;

} // namespace acrtos