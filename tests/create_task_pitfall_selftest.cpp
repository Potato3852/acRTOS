/**
 * @file create_task_pitfall_selftest.cpp
 * @brief Demonstrates (does not fix) a known limitation of create_task<>:
 *        see docs/limitations.md and docs/architecture.md#scheduler.
 *
 * Reaching the same create_task(...) call site twice (e.g. inside a loop) aliases
 * the same static stack storage, because the per-call-site uniqueness comes from
 * the defaulted `Tag = []{}` template parameter, whose type is unique per source
 * line, not per invocation. This file proves it with real addresses by deliberately
 * calling one call site in a loop and checking that the two handles DO alias.
 *
 * `Scheduler::create_task` now asserts against this at runtime, so this file
 * predates that assert and is kept as a worked example.
 *
 * No UART: watch g_pitfall in the debugger. verdict: 0 running, 1 confirmed.
 *
 * NOTE: compiling this file requires a friend declaration in Task (task.hpp),
 * scoped to this demo: `friend const void* pitfall_debug_stack_base(const Task&);`
 * Not a pattern to copy elsewhere -- see the comment below.
 */
#include <acrtos.hpp>
#include <cstdint>

using namespace acrtos;

struct PitfallStatus {
    volatile std::uint32_t verdict;          // 0 not yet checked, 1 confirmed (as documented)
    volatile const void* stack_base_a;
    volatile const void* stack_base_b;
};

extern "C" { PitfallStatus g_pitfall{}; }

namespace {

void same_worker() { while (true) Scheduler::instance().delay_ms(1000); }

} // namespace

// Task keeps tcb_ private on purpose; this demo-only function needs friendship to
// read it, so privacy stays intact everywhere except this one proof file.
namespace acrtos {
    const void* pitfall_debug_stack_base(const Task& t) { return t.tcb_ ? t.tcb_->stack_base : nullptr; }
}

void acrtos_pitfall_selftest_run() {
    auto& s = Scheduler::instance();

    // Two different call sites -> two different stacks (the normal, safe case).
    Task ok_a = s.create_task(same_worker, 1);   // call site #1
    Task ok_b = s.create_task(same_worker, 2);   // call site #2
    ACRTOS_ASSERT(pitfall_debug_stack_base(ok_a) != pitfall_debug_stack_base(ok_b));

    // One call site reached twice via a loop -> same Tag -> same static storage.
    const void* aliasing[2] = {nullptr, nullptr};
    for (int i = 0; i < 2; ++i) {
        Task t = s.create_task(same_worker, 3);   // call site #3, looped on purpose
        aliasing[i] = pitfall_debug_stack_base(t);
    }
    g_pitfall.stack_base_a = aliasing[0];
    g_pitfall.stack_base_b = aliasing[1];
    ACRTOS_ASSERT(aliasing[0] == aliasing[1] && "expected aliasing did not occur -- update this demo");

    g_pitfall.verdict = 1;
    while (true) { asm volatile("wfi"); }
}
