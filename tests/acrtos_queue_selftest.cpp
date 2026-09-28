/**
 * @file acrtos_queue_selftest.cpp
 * @brief Self-checking on-target tests for Queue<T, N>.
 *        See docs/testing.md for how to read a verdict and debug a stuck run.
 *
 * Requires: kMaxTasks >= 14 (12 test tasks + idle + spare); main.cpp's
 * acrtos_tick_hook() must call acrtos_queue_selftest_isr_tick() (it doubles as the
 * ISR source for T5/T6).
 *
 *   T1  one producer / one consumer, random delays: strict sequence, both full and empty paths hit
 *   T2  two producers with different priorities, capacity 1: per-producer FIFO, no loss, no duplicates
 *   T3  timeouts: receive on empty and send on full return false after 19..22 ms, queue content intact
 *   T4  wake order: the highest-priority waiting receiver is served first, not the oldest
 *   T5  ISR -> task (send_from_isr): strict sequence, drops happen only when the queue is full
 *   T6  task -> ISR (receive_from_isr): a blocked sender is released by the ISR, order preserved
 */
#include "main.h"          // HAL_GetTick()
#include <acrtos.hpp>
#include "acrtos/queue.hpp"

#include <cstdint>

#ifndef SELFTEST_HALT_ON_FAIL
#define SELFTEST_HALT_ON_FAIL 1
#endif

using namespace acrtos;

struct QueueTestStatus {
    volatile std::uint32_t verdict;            // 0 running, 1 PASS, 2 FAIL
    volatile std::uint32_t fail_count;
    volatile std::uint32_t first_fail_line;
    volatile std::uint32_t heartbeat;
    volatile std::uint32_t t1_recv, t1_full_hits, t1_empty_hits;
    volatile std::uint32_t t2_recv_a, t2_recv_b;
    volatile std::uint32_t t3_recv_timeouts, t3_send_timeouts;
    volatile std::uint32_t t4_rounds;
    volatile std::uint32_t t5_recv, t5_isr_dropped;
    volatile std::uint32_t t6_sent, t6_isr_recv;
};

extern "C" {
QueueTestStatus g_qtest{};

__attribute__((noinline)) void qtest_failed(std::uint32_t line) {
    if (__atomic_add_fetch(&g_qtest.fail_count, 1u, __ATOMIC_RELAXED) == 1u) {
        g_qtest.first_fail_line = line;
    }
#if SELFTEST_HALT_ON_FAIL && defined(__arm__)
    asm volatile("bkpt #0");
#endif
}
}

#ifndef SELFTEST_LED_TOGGLE
#define SELFTEST_LED_TOGGLE() do {} while (0)
#endif

#define TEST_CHECK(cond) do { if (!(cond)) { qtest_failed(__LINE__); } } while (0)

namespace {

struct Msg1 { std::uint32_t seq; std::uint32_t inv; };
struct Msg2 { std::uint32_t producer; std::uint32_t seq; std::uint32_t inv; };

Queue<Msg1, 3>           g_q1;
Queue<Msg2, 1>           g_q2;
Queue<std::uint32_t, 2>  g_q3_empty;
Queue<std::uint32_t, 1>  g_q3_full;
Queue<std::uint32_t, 2>  g_q4;
Queue<Msg1, 4>           g_q5;
Queue<std::uint32_t, 2>  g_q6;

volatile bool g_running = false;

constexpr std::uint32_t kNone = 0xFFFFFFFFu;
volatile std::uint32_t g_t4_hi = kNone;
volatile std::uint32_t g_t4_lo = kNone;

std::uint32_t xorshift32(std::uint32_t x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

// ---------------------------------------------------------------- T1
void t1_producer() {
    std::uint32_t rng = 0x1234567u;
    std::uint32_t next = 0;
    for (;;) {
        const Msg1 m{next, ~next};
        if (!g_q1.send(m, 0)) {
            g_qtest.t1_full_hits = g_qtest.t1_full_hits + 1;
            TEST_CHECK(g_q1.send(m, kWaitForever));
        }
        ++next;

        rng = xorshift32(rng);
        if (const std::uint32_t d = rng % 4; d != 0) Scheduler::instance().delay_ms(d);
    }
}

void t1_consumer() {
    std::uint32_t rng = 0x7654321u;
    std::uint32_t expected = 0;
    for (;;) {
        Msg1 m{};
        if (!g_q1.receive(m, 0)) {
            g_qtest.t1_empty_hits = g_qtest.t1_empty_hits + 1;
            TEST_CHECK(g_q1.receive(m, kWaitForever));
        }
        TEST_CHECK(m.seq == expected && m.inv == ~expected);
        ++expected;
        g_qtest.t1_recv = expected;

        rng = xorshift32(rng);
        if (const std::uint32_t d = rng % 4; d != 0) Scheduler::instance().delay_ms(d);
    }
}

// ---------------------------------------------------------------- T2
template <std::uint32_t Id>
void t2_producer() {
    std::uint32_t next = 0;
    for (;;) {
        const Msg2 m{Id, next, ~next};
        TEST_CHECK(g_q2.send(m, kWaitForever));
        ++next;
        Scheduler::instance().delay_ms(1);
    }
}

void t2_consumer() {
    std::uint32_t expected[2] = {0, 0};
    for (;;) {
        Msg2 m{};
        TEST_CHECK(g_q2.receive(m, kWaitForever));
        TEST_CHECK(m.producer < 2);
        if (m.producer < 2) {
            TEST_CHECK(m.seq == expected[m.producer] && m.inv == ~m.seq);
            ++expected[m.producer];
        }
        g_qtest.t2_recv_a = expected[0];
        g_qtest.t2_recv_b = expected[1];
    }
}

// ---------------------------------------------------------------- T3
void t3_timeouts() {
    constexpr std::uint32_t kSentinel = 0xC0FFEEu;
    std::uint32_t v = 0;

    TEST_CHECK(g_q3_full.send(kSentinel, 0));
    for (;;) {
        std::uint32_t t0 = HAL_GetTick();
        bool ok = g_q3_empty.receive(v, 20);
        std::uint32_t dt = HAL_GetTick() - t0;
        TEST_CHECK(!ok);
        TEST_CHECK(dt >= 19 && dt <= 22);
        g_qtest.t3_recv_timeouts = g_qtest.t3_recv_timeouts + 1;

        t0 = HAL_GetTick();
        ok = g_q3_full.send(kSentinel + 1u, 20);
        dt = HAL_GetTick() - t0;
        TEST_CHECK(!ok);
        TEST_CHECK(dt >= 19 && dt <= 22);
        TEST_CHECK(g_q3_full.count() == 1);
        TEST_CHECK(g_q3_full.receive(v, 0) && v == kSentinel);   // the timed-out item never entered
        TEST_CHECK(g_q3_full.send(kSentinel, 0));
        g_qtest.t3_send_timeouts = g_qtest.t3_send_timeouts + 1;

        Scheduler::instance().delay_ms(10);
    }
}

// ---------------------------------------------------------------- T4
void t4_receiver_hi() {                         // prio 5
    Scheduler::instance().delay_ms(2);          // let lo block first, to prove hi still wins
    for (;;) {
        std::uint32_t v = 0;
        TEST_CHECK(g_q4.receive(v, kWaitForever));
        g_t4_hi = v;
        Scheduler::instance().delay_ms(4);      // stay away while the second item is delivered
    }
}

void t4_receiver_lo() {                         // prio 4
    for (;;) {
        std::uint32_t v = 0;
        TEST_CHECK(g_q4.receive(v, kWaitForever));
        g_t4_lo = v;
    }
}

void t4_giver() {                               // prio 3
    std::uint32_t round = 0;
    for (;;) {
        Scheduler::instance().delay_ms(5);
        const std::uint32_t a = round * 2;
        const std::uint32_t b = round * 2 + 1;

        TEST_CHECK(g_q4.send(a, 0));            // both receivers wait: hi must get it
        Scheduler::instance().delay_ms(1);
        TEST_CHECK(g_t4_hi == a && g_t4_lo == kNone);

        TEST_CHECK(g_q4.send(b, 0));            // hi is asleep: lo gets it
        Scheduler::instance().delay_ms(1);
        TEST_CHECK(g_t4_lo == b);

        Scheduler::instance().delay_ms(5);
        g_t4_hi = kNone;
        g_t4_lo = kNone;
        ++round;
        g_qtest.t4_rounds = round;
    }
}

// ---------------------------------------------------------------- T5 / T6 (task side)
void t5_consumer() {
    std::uint32_t expected = 0;
    for (;;) {
        Msg1 m{};
        TEST_CHECK(g_q5.receive(m, kWaitForever));
        TEST_CHECK(m.seq == expected && m.inv == ~expected);
        ++expected;
        g_qtest.t5_recv = expected;
        if (expected % 50 == 0) Scheduler::instance().delay_ms(20);   // let the ISR overflow the queue
    }
}

void t6_producer() {
    std::uint32_t next = 0;
    for (;;) {
        TEST_CHECK(g_q6.send(next, kWaitForever));
        ++next;
        g_qtest.t6_sent = next;
    }
}

// ---------------------------------------------------------------- reporter
void reporter() {
    auto& st = g_qtest;
    for (;;) {
        Scheduler::instance().delay_ms(500);
        st.heartbeat = st.heartbeat + 1;
        SELFTEST_LED_TOGGLE();

        const bool progressed =
            st.t1_recv >= 50 && st.t1_full_hits >= 3 && st.t1_empty_hits >= 3 &&
            st.t2_recv_a >= 20 && st.t2_recv_b >= 20 &&
            st.t3_recv_timeouts >= 5 && st.t3_send_timeouts >= 5 &&
            st.t4_rounds >= 5 &&
            st.t5_recv >= 20 && st.t5_isr_dropped >= 1 &&
            st.t6_isr_recv >= 20;
        st.verdict = st.fail_count != 0 ? 2u : (progressed ? 1u : 0u);
    }
}

} // namespace

// Real ISR context (called from SysTick via acrtos_tick_hook()). Must be the function
// wired up in main.cpp's hook -- see docs/testing.md's setup checklist.
void acrtos_queue_selftest_isr_tick() {
    if (!g_running) return;

    static std::uint32_t n = 0;
    static std::uint32_t t5_seq = 0;
    static std::uint32_t t6_expected = 0;
    ++n;

    bool yield = false;

    if (n % 3 == 0) {                            // T5: ISR -> task
        bool y = false;
        const Msg1 m{t5_seq, ~t5_seq};
        if (g_q5.send_from_isr(m, y)) {
            ++t5_seq;
        } else {
            g_qtest.t5_isr_dropped = g_qtest.t5_isr_dropped + 1;
        }
        yield = yield || y;
    }

    if (n % 5 == 0) {                            // T6: task -> ISR
        bool y = false;
        std::uint32_t v = 0;
        if (g_q6.receive_from_isr(v, y)) {
            TEST_CHECK(v == t6_expected);
            ++t6_expected;
            g_qtest.t6_isr_recv = t6_expected;
        }
        yield = yield || y;
    }

    if (yield) task_yield();
}

void acrtos_queue_selftest_run() {
    auto& s = Scheduler::instance();

    (void)s.create_task(reporter, 1);
    (void)s.create_task(t1_producer, 2);
    (void)s.create_task(t1_consumer, 3);
    (void)s.create_task(t2_producer<0>, 2);
    (void)s.create_task(t2_producer<1>, 4);
    (void)s.create_task(t2_consumer, 3);
    (void)s.create_task(t3_timeouts, 4);
    (void)s.create_task(t4_receiver_lo, 4);
    (void)s.create_task(t4_receiver_hi, 5);
    (void)s.create_task(t4_giver, 3);
    (void)s.create_task(t5_consumer, 3);
    (void)s.create_task(t6_producer, 3);

    g_running = true;
    s.start();
}
