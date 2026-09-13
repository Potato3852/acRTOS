/**
 * @file port.hpp
 * @brief Hardware Abstraction Layer (HAL) for ARM Cortex-M architecture.
 * @details Contains architecture-specific assembly instructions, interrupt 
 *          management, and stack initialization routines.
 */

#pragma once
#include <cstdint>

extern "C" {
    extern uint32_t* current_sp;
    void task_yield();
    void rtos_tick_handler();
    void schedule_next_task();
}

namespace acrtos::detail {
    uint32_t* init_task_stack(uint32_t* stack_top, void (*task_func)());
} // namespace acrtos::detail

namespace acrtos::port {

#if defined(__arm__)

inline uint32_t enter_critical() noexcept {
    uint32_t primask;
    asm volatile(
        "mrs %0, primask \n\t"
        "cpsid i         \n\t"
        : "=r"(primask)
        :
        : "memory"
    );
    return primask;
}

inline void exit_critical(uint32_t primask) noexcept {
    asm volatile("msr primask, %0" : : "r"(primask) : "memory");
}

#else
inline uint32_t enter_critical() noexcept { return 0; }
inline void exit_critical(uint32_t) noexcept {}
#endif

/**
 * @class CriticalSection
 * @brief RAII wrapper for managing hardware interrupts.
 * @details Disables interrupts upon construction and restores the previous 
 *          interrupt state upon destruction (going out of scope).
 */
class CriticalSection {
public:
    /** @brief Disables interrupts and saves the PRIMASK state. */
    CriticalSection() noexcept : primask_(enter_critical()) {}
    
    /** @brief Restores the previously saved PRIMASK state. */
    ~CriticalSection() noexcept { exit_critical(primask_); }

    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;

private:
    uint32_t primask_;
};

void start_hardware_and_yield() noexcept;

} // namespace acrtos::port