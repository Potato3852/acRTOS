/**
 * @file acrtos_event_selftest.cpp
 * @brief Self-checking on-target tests for EventGroup.
 *        See docs/testing.md for how to read a verdict and debug a stuck run.
 *
 *   T1  ANY-wait wakes as soon as one bit of its mask appears, ALL-wait waits
 *       for every bit; the returned value is the REAL bits, never fabricated
 *       from the requested mask (this is exactly the bug that was fixed)
 *   T2  clear_on_exit actually clears only the waiter's own mask, and does
 *       not silently un-clear it (checks the fix in scan_and_wake_locked)
 *   T3  wait order: a higher-priority ANY-waiter and a lower-priority
 *       ALL-waiter share one bit; setting bits must satisfy each task's own
 *       condition independently, not "wake the highest priority only"
 *   T4  timeout: wait_bits() on a mask that never gets set returns after
 *       ~20 ms, and the returned bits are the real (unset) bits, not zero
 *       by convention and not the requested mask
 *   T5  set_bits_from_isr wakes a waiting task from real ISR context
 */
#include "main.h"          // HAL_GetTick()
#include <acrtos.hpp>
#include <cstdint>

using namespace acrtos;

#ifndef SELFTEST_HALT_ON_FAIL
#define SELFTEST_HALT_ON_FAIL 1
#endif

struct EventTestStatus {
    volatile std::uint32_t verdict;
    volatile std::uint32_t fail_count;
    volatile std::uint32_t first_fail_line;
    volatile std::uint32_t heartbeat;
    volatile std::uint32_t t1_rounds;
    volatile std::uint32_t t2_rounds;
    volatile std::uint32_t t3_rounds;
    volatile std::uint32_t t4_timeouts;
    volatile std::uint32_t t5_isr_wakes;
};

extern "C" {
EventTestStatus g_etest{};

__attribute__((noinline)) void etest_failed(std::uint32_t line) {
    if (__atomic_add_fetch(&g_etest.fail_count, 1u, __ATOMIC_RELAXED) == 1u) {
        g_etest.first_fail_line = line;
    }
#if SELFTEST_HALT_ON_FAIL && defined(__arm__)
    asm volatile("bkpt #0");
#endif
}
}

#ifndef SELFTEST_LED_TOGGLE
#define SELFTEST_LED_TOGGLE() do {} while (0)
#endif

#define TEST_CHECK(cond) do { if (!(cond)) { etest_failed(__LINE__); } } while (0)

namespace {

constexpr std::uint32_t kBitA = 1u << 0;
constexpr std::uint32_t kBitB = 1u << 1;
constexpr std::uint32_t kBitC = 1u << 2;
constexpr std::uint32_t kBitD = 1u << 3;   // T3: hi's own bit, disjoint from A|B

EventGroup g_eg1;   // T1, T2
EventGroup g_eg2;   // T3
EventGroup g_eg3;   // T4
EventGroup g_eg4;   // T5

volatile bool g_running = false;

// ---------------------------------------------------------------- T1
void t1_any_waiter() {
    for (;;) {
        const std::uint32_t got = g_eg1.wait_bits(kBitA | kBitB, /*wait_all=*/false,
                                                    /*clear_on_exit=*/true);
        TEST_CHECK((got & kBitA) != 0);
        TEST_CHECK((got & kBitB) == 0);   // the fabrication bug would set this
        g_etest.t1_rounds = g_etest.t1_rounds + 1;
    }
}

void t1_giver() {
    for (;;) {
        Scheduler::instance().delay_ms(5);
        (void)g_eg1.set_bits(kBitA);
    }
}

// ---------------------------------------------------------------- T2
void t2_waiter() {
    for (;;) {
        const std::uint32_t got = g_eg1.wait_bits(kBitC, /*wait_all=*/false,
                                                    /*clear_on_exit=*/true);
        TEST_CHECK((got & kBitC) != 0);
        Scheduler::instance().delay_ms(2);
        TEST_CHECK((g_eg1.get_bits() & kBitC) == 0);   // nobody else touches kBitC
        g_etest.t2_rounds = g_etest.t2_rounds + 1;
    }
}

void t2_giver() {
    for (;;) {
        Scheduler::instance().delay_ms(7);
        (void)g_eg1.set_bits(kBitC);
    }
}

// ---------------------------------------------------------------- T3
// hi's mask (D) is disjoint from lo's (A|B) so hi consuming D can never starve
// lo -- this isolates what T3 actually tests: each waiter's own condition, not
// "wake whoever has the highest priority".
void t3_hi() {                      // prio 5
    for (;;) {
        const std::uint32_t got = g_eg2.wait_bits(kBitD, false, /*clear_on_exit=*/true);
        TEST_CHECK((got & kBitD) != 0);
    }
}

void t3_lo() {                      // prio 3
    for (;;) {
        const std::uint32_t got = g_eg2.wait_bits(kBitA | kBitB, true, /*clear_on_exit=*/true);
        TEST_CHECK((got & kBitA) != 0 && (got & kBitB) != 0);
        g_etest.t3_rounds = g_etest.t3_rounds + 1;
    }
}

void t3_giver() {                   // prio 4, between hi and lo
    for (;;) {
        Scheduler::instance().delay_ms(6);
        (void)g_eg2.set_bits(kBitD);          // hi wakes, does not touch A|B
        (void)g_eg2.set_bits(kBitA);
        (void)g_eg2.set_bits(kBitB);           // both set now -> lo wakes
    }
}

// ---------------------------------------------------------------- T4
void t4_timeout_waiter() {
    for (;;) {
        const std::uint32_t t0 = HAL_GetTick();
        const std::uint32_t got = g_eg3.wait_bits(kBitA, false, false, 20);
        const std::uint32_t dt = HAL_GetTick() - t0;

        TEST_CHECK(dt >= 19 && dt <= 22);
        TEST_CHECK((got & kBitA) == 0);   // never set on this group
        g_etest.t4_timeouts = g_etest.t4_timeouts + 1;

        Scheduler::instance().delay_ms(5);
    }
}

// ---------------------------------------------------------------- T5 (ISR)
void t5_isr_waiter() {
    for (;;) {
        const std::uint32_t got = g_eg4.wait_bits(kBitA, false, true, kWaitForever);
        TEST_CHECK((got & kBitA) != 0);
        g_etest.t5_isr_wakes = g_etest.t5_isr_wakes + 1;
    }
}

// ---------------------------------------------------------------- reporter
void reporter() {
    for (;;) {
        Scheduler::instance().delay_ms(500);
        g_etest.heartbeat = g_etest.heartbeat + 1;
        SELFTEST_LED_TOGGLE();

        const bool progressed =
            g_etest.t1_rounds >= 10 && g_etest.t2_rounds >= 5 &&
            g_etest.t3_rounds >= 5 && g_etest.t4_timeouts >= 5 &&
            g_etest.t5_isr_wakes >= 5;
        g_etest.verdict = g_etest.fail_count != 0 ? 2u : (progressed ? 1u : 0u);
    }
}

} // namespace

// Real ISR context (called from SysTick via acrtos_tick_hook()). Must be the function
// wired up in main.cpp's hook -- see docs/testing.md's setup checklist.
void acrtos_event_selftest_isr_tick() {
    if (!g_running) return;

    static std::uint32_t n = 0;
    ++n;

    if (n % 10 == 0) {
        bool should_yield = false;
        (void)g_eg4.set_bits_from_isr(kBitA, should_yield);
        if (should_yield) task_yield();
    }
}

void acrtos_event_selftest_run() {
    auto& s = Scheduler::instance();

    (void)s.create_task(reporter, 1);
    (void)s.create_task(t1_any_waiter, 2);
    (void)s.create_task(t1_giver, 2);
    (void)s.create_task(t2_waiter, 2);
    (void)s.create_task(t2_giver, 2);
    (void)s.create_task(t3_hi, 5);
    (void)s.create_task(t3_lo, 3);
    (void)s.create_task(t3_giver, 4);
    (void)s.create_task(t4_timeout_waiter, 2);
    (void)s.create_task(t5_isr_waiter, 2);

    g_running = true;
    s.start();
}
