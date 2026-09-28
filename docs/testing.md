# Testing

acRTOS has no unit test framework running on the target. Instead, each subsystem has
a self-checking on-target test file (`acrtos_*_selftest.cpp`) that runs forever inside
the RTOS itself and reports its own verdict.

## Why this shape

- **No UART needed.** Results live in a plain global status struct (`g_selftest`,
  `g_qtest`, `g_etest`, ...). Read it with Live Watch / Live Expressions while the
  target runs, or with a normal Watch while paused. No printf, no retargeting, no
  extra wiring.
- **Fails loud, not silent.** Every check goes through a `TEST_CHECK(cond)` macro that,
  on failure, records the failing line and executes `bkpt #0`. The debugger stops at
  the exact moment something goes wrong, with the call stack pointing at the task (or
  ISR) that caught it — not minutes later when the system has drifted into an
  unrelated state.
- **A stuck test looks different from a slow one.** Every test file has a low-priority
  `reporter` task that increments `heartbeat` every 500 ms. If `heartbeat` stops moving
  but `fail_count` is still 0, the scheduler itself has stopped (see
  [Debugging a stuck heartbeat](#debugging-a-stuck-heartbeat) below) — that is a
  different failure mode than a check tripping, and the two are easy to tell apart.

## Reading a verdict

Every status struct follows the same three fields:

| Field | Meaning |
|---|---|
| `verdict` | `0` still warming up, `1` PASS, `2` FAIL |
| `fail_count` / `first_fail_line` | how many `TEST_CHECK`s failed, and the line of the first one |
| `heartbeat` | increments every 500 ms as long as the scheduler is alive |

`verdict` only becomes `1` once every sub-test has run a minimum number of rounds
(`reporter`'s `progressed` condition) — a fresh boot legitimately sits at `0` for a
second or two, that is not a bug.

## The test files

| File | Covers | Key checks |
|---|---|---|
| `acrtos_selftest.cpp` | Scheduler, `Mutex`, `Semaphore` | mutual exclusion, priority inheritance (and its effect on latency under CPU load), semaphore timeout, event conservation, FPU context preserved across switches |
| `acrtos_queue_selftest.cpp` | `Queue<T, N>` | producer/consumer sequencing, per-producer FIFO, timeouts, priority-ordered wake, `send_from_isr`/`receive_from_isr` |
| `acrtos_event_selftest.cpp` | `EventGroup` | ANY vs ALL waits, `clear_on_exit`, independent per-waiter conditions, timeouts, `set_bits_from_isr` |
| `acrtos_time_selftest.cpp` | `get_tick_count()`, `delay_until()` | **skeleton** — the `TODO_CHECK` comments are intentionally left for you to fill in, this file documents what each check should prove, not a finished test |
| `create_task_pitfall_selftest.cpp` | `create_task<>` | not a pass/fail test — a controlled demonstration of a known limitation (see below) |

## Setup checklist

Each file's header comment lists its own requirements, but the two mistakes that
actually happened during development are worth calling out explicitly:

1. **`acrtos_tick_hook()` must call the right `_isr_tick()` function.** Every test that
   exercises an ISR path (`send_from_isr`, `set_bits_from_isr`) needs its own
   `acrtos_*_selftest_isr_tick()` called from the SysTick hook:
   ```cpp
   extern "C" void acrtos_tick_hook(void) {
       HAL_IncTick();
       acrtos_event_selftest_isr_tick();   // must match whichever *_run() you call in main()
   }
   ```
   Wiring the hook to a *different* test's ISR function than the one whose `_run()`
   you call in `main()` compiles and links fine — nothing complains — it just means
   the ISR-side checks (`t5_isr_wakes`, `t6_isr_recv`, ...) never move. This happened
   once during development and looked exactly like a mystery hang until the call
   stack and a careful read of `main.cpp` showed the mismatch.
2. **`kMaxTasks` must cover every task each file creates**, including the idle task
   `start()` adds implicitly. Each header comment states the minimum; running out
   trips `ACRTOS_ASSERT(tcb != nullptr && "kMaxTasks too small")` in `create_task_impl`,
   which is a clean, loud failure rather than a silent one.

## Debugging a stuck heartbeat

If `heartbeat` is not increasing:

1. Pause the debugger (not reset) and read the call stack.
2. If it's sitting in `wait_for_interrupt()` under `Scheduler::start()`'s idle lambda,
   that is the **idle task**, not a hang — the CPU genuinely has nothing else to do
   right now. Resume and check again a moment later.
3. If it's sitting in `acrtos_assert_failed`, an internal `ACRTOS_ASSERT` fired (not a
   `TEST_CHECK`) — `fail_count` will still read `0` in that case, since assertion
   failures and test-check failures are reported through different mechanisms.
4. Set breakpoints on `acrtos_assert_failed` and `HardFault_Handler` up front so the
   *next* run stops exactly where the problem starts, instead of needing a manual
   pause-and-guess.

## Mutation testing

A test that has never seen a real bug is not proven to catch one. Each selftest here
was validated by deliberately breaking the implementation it targets and confirming
the relevant check fails — for example, removing the line in `EventGroup::scan_and_wake_locked`
that captures `satisfied_bits` before applying `clear_on_exit` reintroduces a bug where
`wait_bits()` reports bits that were never actually set, and T1 in
`acrtos_event_selftest.cpp` catches it immediately. This is the standard this test
suite holds itself to: a green `verdict` should mean something broke and was caught,
not "the test never really exercised the failure path."

## The `create_task<>` pitfall test

`create_task_pitfall_selftest.cpp` is not a pass/fail check — it exists to document,
with a runnable example, a real limitation described in
[docs/limitations.md](limitations.md): a `create_task(...)` call reached more than
once at the same source location (e.g. inside a loop) aliases the same static stack
storage, because the per-call-site uniqueness comes from the `Tag = []{}` template
default, which is tied to where the code is written, not how many times it runs.
`Scheduler::create_task` now asserts against this at runtime
(`create_task called multiple times at the same call-site`), so in practice this
mistake fails loudly rather than silently corrupting a stack — this file predates
that assert and is kept as a worked example of *why* the rule exists.
