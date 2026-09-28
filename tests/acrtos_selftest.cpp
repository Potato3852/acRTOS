/**
 * @file acrtos_selftest.cpp
 * @brief Self-checking on-target tests for the scheduler, Mutex and Semaphore.
 *        See docs/testing.md for how to read a verdict and debug a stuck run.
 *
 * Requires: kMaxTasks >= 7 (6 tasks + idle); acrtos_tick_hook() must call HAL_IncTick().
 *
 *   T1  mutual exclusion             g_in_cs never > 1
 *   T2  priority inheritance         LOW is 3 while HIGH waits, back to 1 after unlock
 *   T3  PI effect on latency         HIGH waits < 10 ms although MED burns CPU 20 ms in a row
 *   T4  semaphore timeout            take(20) -> false after 19..22 ms
 *   T5  event conservation           gives - takes is 0 or 1
 */
#define SELFTEST_LED_TOGGLE() HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13)
#include "main.h"          // HAL_GetTick()
#include <acrtos.hpp>

#include <cstdint>

#ifndef SELFTEST_HALT_ON_FAIL
#define SELFTEST_HALT_ON_FAIL 1
#endif

#ifndef SELFTEST_LED_TOGGLE
#define SELFTEST_LED_TOGGLE() do {} while (0)
#endif

using namespace acrtos;

struct SelfTestStatus {
    volatile std::uint32_t verdict;          // 0 running, 1 PASS, 2 FAIL
    volatile std::uint32_t fail_count;
    volatile std::uint32_t first_fail_line;
    volatile std::uint32_t heartbeat;
    volatile std::uint32_t low_cycles;
    volatile std::uint32_t pi_checks;
    volatile std::uint32_t high_grabs;
    volatile std::uint32_t high_max_wait_ms;
    volatile std::uint32_t gives;
    volatile std::uint32_t takes;
    volatile std::uint32_t timeout_ok;
    volatile std::uint32_t fpu_a, fpu_b;
};

extern "C" {
SelfTestStatus g_selftest{};

__attribute__((noinline)) void selftest_failed(std::uint32_t line) {
    if (__atomic_add_fetch(&g_selftest.fail_count, 1u, __ATOMIC_RELAXED) == 1u) {
        g_selftest.first_fail_line = line;
    }
#if SELFTEST_HALT_ON_FAIL && defined(__arm__)
    asm volatile("bkpt #0");
#endif
}
}

#define TEST_CHECK(cond) do { if (!(cond)) { selftest_failed(__LINE__); } } while (0)

namespace {

Mutex     g_mtx;
Semaphore g_data_sem(0);    // LOW -> CONSUMER events
Semaphore g_never_sem(0);   // nobody ever gives: pure timeout test

Task g_low;                 // handle, to inspect LOW's priority

volatile std::uint32_t g_in_cs     = 0;
volatile bool          g_h_waiting = false;

// Busy-wait on HAL_GetTick() so duration is stable regardless of optimization level.
void spin_ms(std::uint32_t ms) {
    const std::uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < ms) {
        asm volatile("" ::: "memory");
    }
}

// LOW (prio 1): holds the mutex, produces semaphore events.
void task_low() {
    auto& st = g_selftest;
    for (;;) {
        (void)g_mtx.lock();
        g_in_cs = g_in_cs + 1;
        TEST_CHECK(g_in_cs == 1);                       // T1

        const std::uint32_t t0 = HAL_GetTick();
        while (!g_h_waiting && HAL_GetTick() - t0 < 30) {
            asm volatile("" ::: "memory");
        }
        if (g_h_waiting) {
            spin_ms(2);                                 // let HIGH actually block first
            TEST_CHECK(g_low.get_priority() == 3);      // T2: inherited
            st.pi_checks = st.pi_checks + 1;
        }

        st.low_cycles = st.low_cycles + 1;
        if (st.low_cycles % 10 == 0) {
            st.gives = st.gives + 1;
            g_data_sem.give();
        }

        g_in_cs = g_in_cs - 1;
        (void)g_mtx.unlock();
        TEST_CHECK(g_low.get_priority() == 1);          // T2: restored

        Scheduler::instance().delay_ms(1);
    }
}

// MED (prio 2): CPU noise; without priority inheritance it would starve LOW.
void task_med() {
    for (;;) {
        spin_ms(20);
        Scheduler::instance().delay_ms(5);
    }
}

// HIGH (prio 3): wants the mutex while LOW holds it.
void task_high() {
    auto& st = g_selftest;
    for (;;) {
        Scheduler::instance().delay_ms(7);

        const std::uint32_t t0 = HAL_GetTick();
        g_h_waiting = true;
        (void)g_mtx.lock();
        g_h_waiting = false;

        const std::uint32_t waited = HAL_GetTick() - t0;
        if (waited > st.high_max_wait_ms) st.high_max_wait_ms = waited;
        TEST_CHECK(waited < 10);                        // T3

        g_in_cs = g_in_cs + 1;
        TEST_CHECK(g_in_cs == 1);                       // T1
        g_in_cs = g_in_cs - 1;

        (void)g_mtx.unlock();
        st.high_grabs = st.high_grabs + 1;
    }
}

// CONSUMER (prio 4): counts semaphore events.
void task_consumer() {
    auto& st = g_selftest;
    for (;;) {
        if (g_data_sem.take(50)) {
            st.takes = st.takes + 1;
        }
    }
}

// TIMEOUT (prio 4): a semaphore nobody gives.
void task_timeout() {
    auto& st = g_selftest;
    for (;;) {
        const std::uint32_t t0 = HAL_GetTick();
        const bool ok = g_never_sem.take(20);
        const std::uint32_t dt = HAL_GetTick() - t0;

        TEST_CHECK(!ok);                                // T4
        TEST_CHECK(dt >= 19 && dt <= 22);               // T4 (1 ms tick granularity)
        st.timeout_ok = st.timeout_ok + 1;

        Scheduler::instance().delay_ms(10);
    }
}

// REPORTER (prio 1): computes the verdict twice a second.
void task_reporter() {
    auto& st = g_selftest;
    for (;;) {
        Scheduler::instance().delay_ms(500);

        std::uint32_t gives, takes;
        {
            port::CriticalSection cs;                   // consistent snapshot
            gives = st.gives;
            takes = st.takes;
        }
        TEST_CHECK(gives >= takes && gives - takes <= 1);   // T5

        st.heartbeat = st.heartbeat + 1;
        SELFTEST_LED_TOGGLE();

        const bool progressed = st.pi_checks >= 5 && st.high_grabs >= 5 &&
                                st.timeout_ok >= 5 && st.takes >= 3;
        st.verdict = st.fail_count != 0 ? 2u : (progressed ? 1u : 0u);
    }
}

// s16-s31 are callee-saved FPU registers NOT covered by hardware lazy stacking
// (unlike s0-s15), so PendSV has to save/restore them explicitly. Two instances
// of this loop (different Seed) would corrupt each other's pattern the first time
// they interleave if that save/restore were missing or wrong. Written directly into
// s16-s19 with inline asm so the values sit exactly where a broken switch loses them
// -- plain C++ locals under -O0 usually live on the stack instead, and wouldn't catch it.
template <std::uint32_t Seed>
void fpu_loop(volatile std::uint32_t& counter) {
    std::uint32_t n = 0;
    for (;;) {
        const std::uint32_t pat = Seed + n;
        asm volatile("vmov s16, %0 \n\t vmov s17, %0 \n\t vmov s18, %0 \n\t vmov s19, %0" : : "r"(pat) : "s16", "s17", "s18", "s19");

        Scheduler::instance().delay_ms(1);   // a context switch happens here

        std::uint32_t a, b, c, d;
        asm volatile("vmov %0, s16 \n\t vmov %1, s17 \n\t vmov %2, s18 \n\t vmov %3, s19"
                     : "=r"(a), "=r"(b), "=r"(c), "=r"(d));
        TEST_CHECK(a == pat && b == pat && c == pat && d == pat);

        counter = ++n;
    }
}

} // namespace

void acrtos_selftest_run() {
    auto& s = Scheduler::instance();

    g_low = s.create_task<128>(task_low, 1);
    (void)s.create_task<128>(task_reporter, 1);
    (void)s.create_task<128>(task_med, 2);
    (void)s.create_task<128>(task_high, 3);
    (void)s.create_task<128>(task_consumer, 4);
    (void)s.create_task<128>(task_timeout, 4);
    (void)s.create_task<128>([] { fpu_loop<1000>(g_selftest.fpu_a); }, 3);
    (void)s.create_task<128>([] { fpu_loop<5000>(g_selftest.fpu_b); }, 2);
    s.start();
}
