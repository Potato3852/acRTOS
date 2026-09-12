#include "port.hpp"
#include "stm32f4xx.h" 

namespace acrtos::port {

void start_hardware_and_yield() noexcept {
    __set_PSP(0);
    NVIC_SetPriority(PendSV_IRQn, 0xFF);

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

    task_yield();
}

} // namespace acrtos::port