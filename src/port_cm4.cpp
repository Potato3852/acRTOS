#include "acrtos/port.hpp"
#include <cstdint>

namespace {

// Armv7-M System Control Space — architecture registers, not STM32-specific.
constexpr std::uintptr_t kShpr3 = 0xE000ED20;
constexpr std::uintptr_t kSystickCsr = 0xE000E010;
constexpr std::uintptr_t kSystickRvr = 0xE000E014;
constexpr std::uintptr_t kSystickCvr = 0xE000E018;
constexpr std::uintptr_t kCpacr = 0xE000ED88;
constexpr std::uintptr_t kFpccr = 0xE000EF34;

constexpr std::uint32_t kSystickClkSource = 1u << 2;
constexpr std::uint32_t kSystickTickInt = 1u << 1;
constexpr std::uint32_t kSystickEnable = 1u << 0;

volatile std::uint32_t& reg(std::uintptr_t addr) {
    return *reinterpret_cast<volatile std::uint32_t*>(addr);
}

std::uint32_t g_cpu_hz = acrtos::config::kCpuHz;

void enable_fpu_lazy_stacking() {
    // CP10/CP11 full access so -mfloat-abi=hard does not UsageFault.
    reg(kCpacr) |= (0xFu << 20);
    // ASPEN | LSPEN: hardware lazy-stacks s0-s15 on exception entry.
    reg(kFpccr) |= (3u << 30);
    asm volatile("dsb \n isb" ::: "memory");
}

void configure_pendsv_and_systick() {
    std::uint32_t shpr3 = reg(kShpr3);
    shpr3 &= ~0xFFFF0000u;
    shpr3 |= (0xFFu << 16); // PendSV
    shpr3 |= (0xFEu << 24); // SysTick
    reg(kShpr3) = shpr3;

    const std::uint32_t ticks_per_sec = 1000u / acrtos::config::kTickRateMs;
    const std::uint32_t reload = (g_cpu_hz / ticks_per_sec) - 1u;
    ACRTOS_ASSERT(reload > 0 && reload <= 0x00FFFFFFu);

    reg(kSystickCsr) = 0;
    reg(kSystickCvr) = 0;
    reg(kSystickRvr) = reload;
    reg(kSystickCsr) = kSystickClkSource | kSystickTickInt | kSystickEnable;
}

} // namespace

extern "C" {

void acrtos_tick_hook(void) __attribute__((weak));
void acrtos_tick_hook(void) {}

void SysTick_Handler(void) {
    rtos_tick_handler();
    acrtos_tick_hook();
}

} // extern "C"

namespace acrtos::port {

void set_cpu_hz(std::uint32_t hz) noexcept {
    ACRTOS_ASSERT(hz >= 1000);
    g_cpu_hz = hz;
}

std::uint32_t cpu_hz() noexcept {
    return g_cpu_hz;
}

[[noreturn]] void start_hardware_and_yield() noexcept {
    enable_fpu_lazy_stacking();

    asm volatile("msr psp, %0" ::"r"(0) : "memory");

    configure_pendsv_and_systick();
    task_yield();

    while (true) {
        asm volatile("wfi");
    }
}

} // namespace acrtos::port

namespace acrtos::internal {

uint32_t* init_task_stack(uint32_t* stack_top, void (*task_func)(void*), void* param) {
    uint32_t* sp = stack_top;

    *(--sp) = 0x01000000;                                // xPSR (Thumb bit)
    *(--sp) = reinterpret_cast<uint32_t>(task_func) | 1; // PC
    *(--sp) = 0;                                         // LR (task return; trampoline never returns)
    *(--sp) = 0;                                         // R12
    *(--sp) = 0;                                         // R3
    *(--sp) = 0;                                         // R2
    *(--sp) = 0;                                         // R1
    *(--sp) = reinterpret_cast<uint32_t>(param);         // R0

    // Software frame: ldmia {r4-r11, r14}. r14 = EXC_RETURN
    // 0xFFFFFFFD: Thread mode, PSP, integer-only frame.
    *(--sp) = 0xFFFFFFFD;
    for (int i = 0; i < 8; ++i) {
        *(--sp) = 0;
    }

    return sp;
}

} // namespace acrtos::internal
