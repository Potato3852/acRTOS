#pragma once
#include <cstdint>

extern "C" {
    extern uint32_t* current_sp;
    void task_yield();
    void rtos_tick_handler();
    void schedule_next_task();
}

namespace acrtos {
    uint32_t* init_task_stack(uint32_t* stack_top, void (*task_func)());
}