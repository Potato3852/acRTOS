#include "acRtosConfig.hpp"

extern "C" __attribute__((weak)) void acrtos_assert_failed(const char* file, int line) {
    (void)file;
    (void)line;

#if defined(__arm__)
    __asm volatile("cpsid i");
    __asm volatile("bkpt #0");
#endif

    while (true) {
        // Intentionally empty
    }
}