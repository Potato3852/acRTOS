/**
 * @file port.hpp
 * @brief Cortex-M4/M4F hardware port (no vendor HAL).
 *
 * Critical sections use PRIMASK (all IRQs off). Context switch is PendSV.
 * SysTick lives in this port: the application must not define SysTick_Handler
 * unless it is a strong override that still calls rtos_tick_handler().
 */
#pragma once
#include <cstdint>
#include "acRtosConfig.hpp"

extern "C" {
    extern uint32_t* current_sp;
    void task_yield();
    void rtos_tick_handler();
    void schedule_next_task();
    void acrtos_tick_hook(void);
}

namespace acrtos::internal {
    uint32_t* init_task_stack(uint32_t* stack_top, void (*task_func)(void*), void* param);
}

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

#if defined(__arm__)
inline bool in_isr() noexcept {
    uint32_t ipsr;
    asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0;
}
[[noreturn]] inline void wait_for_interrupt() noexcept {
    while (true) asm volatile("wfi");
}
#else
inline bool in_isr() noexcept { return false; }
[[noreturn]] inline void wait_for_interrupt() noexcept { while (true) {} }
#endif

/**
 * @brief RAII lock: constructor disables IRQs, destructor restores the previous mask.
 * Nested locks are safe because each instance remembers its own PRIMASK.
 */
class CriticalSection {
public:
    CriticalSection() noexcept : primask_(enter_critical()) {}
    ~CriticalSection() noexcept { exit_critical(primask_); }

    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;

private:
    uint32_t primask_;
};

/** @brief Override the default kCpuHz if the app programmed a different SYSCLK. */
void set_cpu_hz(std::uint32_t hz) noexcept;
[[nodiscard]] std::uint32_t cpu_hz() noexcept;

/** @brief Program PendSV + SysTick and kick the first context switch. Does not return. */
[[noreturn]] void start_hardware_and_yield() noexcept;

} // namespace acrtos::port