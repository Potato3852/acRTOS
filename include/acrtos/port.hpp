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
    /** @brief Stack pointer hand-off between PendSV and schedule_next_task(). */
    extern uint32_t* current_sp;

    /** @brief Pend a PendSV exception; the context switch runs as soon as interrupts allow. */
    void task_yield();

    /** @brief Advance the kernel by one tick. Called from SysTick_Handler. */
    void rtos_tick_handler();

    /** @brief Save the outgoing task, pick the next one and update current_sp. Called from PendSV. */
    void schedule_next_task();

    /**
     * @brief User hook executed at the end of every SysTick interrupt.
     * @details Weak, empty by default. Override it e.g. to call HAL_IncTick().
     *          Runs in interrupt context: keep it short and use only ISR-safe APIs.
     */
    void acrtos_tick_hook(void);
}

namespace acrtos::internal {
    uint32_t* init_task_stack(uint32_t* stack_top, void (*task_func)(void*), void* param);
}

namespace acrtos::port {

#if defined(__arm__)
/**
 * @brief Disable interrupts and return the previous PRIMASK value.
 * @return Value to pass to exit_critical().
 */
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

/** @brief Restore the PRIMASK value returned by enter_critical(). */
inline void exit_critical(uint32_t primask) noexcept {
    asm volatile("msr primask, %0" : : "r"(primask) : "memory");
}

#else
inline uint32_t enter_critical() noexcept { return 0; }
inline void exit_critical(uint32_t) noexcept {}
#endif

#if defined(__arm__)
/** @brief True when called from exception (interrupt) context. */
inline bool in_isr() noexcept {
    uint32_t ipsr;
    asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0;
}

/** @brief Sleep forever with WFI. */
[[noreturn]] inline void wait_for_interrupt() noexcept {
    while (true) asm volatile("wfi");
}
#else
inline bool in_isr() noexcept { return false; }
[[noreturn]] inline void wait_for_interrupt() noexcept { while (true) {} }
#endif

/**
 * @brief RAII critical section.
 * @details The constructor disables interrupts, the destructor restores the previous
 *          PRIMASK. Sections may be nested because each instance remembers its own
 *          saved value.
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

/**
 * @brief Override config::kCpuHz when the application programmed a different SYSCLK.
 * @note Must be called before Scheduler::start().
 */
void set_cpu_hz(std::uint32_t hz) noexcept;
[[nodiscard]] std::uint32_t cpu_hz() noexcept;

/**
 * @brief Enable the FPU, program PendSV/SysTick priorities and the SysTick timer, then trigger the first switch.
 * @note Does not return.
 */
[[noreturn]] void start_hardware_and_yield() noexcept;

} // namespace acrtos::port