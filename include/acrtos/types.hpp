#pragma once
#include <cstdint>
#include <cstddef>
#include "acRtosConfig.hpp"

namespace acrtos {

using TickType = uint32_t;

enum class TaskState : uint8_t {
    Ready,
    Running,
    Blocked,
    Suspended
};

} // namespace acrtos