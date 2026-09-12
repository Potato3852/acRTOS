#pragma once
#include "acrtos/types.hpp"

namespace acrtos {

struct TaskControlBlock {
    uint32_t* sp{nullptr};
    TaskState state{TaskState::Ready};
    TickType delay_ticks{0};
    uint32_t stack[kStackSize]{0};
};

} // namespace acrtos