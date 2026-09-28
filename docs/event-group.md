# EventGroup

## What problem this solves

`Semaphore` and `Mutex` both answer "wake *someone*" — `give()`/`unlock()` hand off to
exactly one waiter, and any waiter would do. `EventGroup` is for the case where waiters
don't all want the same thing: each one is waiting on its own subset of up to 32 bits,
and each can ask either for *any one* of its bits or for *all* of them
(`wait_all`). Two tasks can be waiting on the same `EventGroup` with completely
different, even disjoint, conditions at the same time.

That's why `set_bits()` can't reuse the "wake highest" pattern every other primitive in
acRTOS follows (see [synchronization.md](synchronization.md)) — there's no single
"highest priority waiter" to hand the event to, because setting bits might satisfy
several waiters' conditions independently, or none of them, regardless of priority.
Instead `scan_and_wake_locked()` walks the whole wait list once and wakes every task
whose own condition (`event_wait_mask` / `event_wait_all`, stashed on its TCB by
`wait_bits()` before parking) is now met, still in priority order.

## API semantics

- **`wait_bits(mask, wait_all, clear_on_exit, timeout_ms)`** — `mask == 0` returns the
  current bits immediately, no wait. Otherwise: if the condition (`ANY` or `ALL` of
  `mask`) is already met, returns immediately (and applies `clear_on_exit`). If not and
  `timeout_ms == 0`, returns the current bits without blocking. Otherwise parks the
  task with its condition recorded on the TCB and blocks until `scan_and_wake_locked()`
  wakes it or the timeout fires.
- **`set_bits(mask)`** — ORs `mask` into the group, wakes every waiter whose condition
  is now satisfied, and returns the bits *after* waking (i.e. after any woken waiter's
  `clear_on_exit` has already applied).
- **`clear_bits(mask)`** — ANDs `~mask` into the group. Does not wake anyone (clearing
  can't satisfy a condition).
- **`set_bits_from_isr(mask, should_yield)`** — same as `set_bits()`, but never yields
  itself; `should_yield` tells the ISR whether to call `task_yield()`.

## `clear_on_exit`

When a waiter's condition is met and `clear_on_exit` is set, its *own* `event_wait_mask`
bits are cleared from the group as part of being woken — not the bits some other
waiter's mask happens to overlap with. If two waiters with `clear_on_exit` share a bit
and both would be satisfied by the same `set_bits()` call, which one actually sees and
consumes the shared bit is determined by wait-list order (priority, then FIFO among
equals), not guaranteed by the API — this is documented behavior, not a bug, and
matches how FreeRTOS documents the same situation for its event groups. If your
protocol needs a bit to be observed by every interested waiter, don't combine it with
`clear_on_exit`.

## Return value contract

`wait_bits()` always returns the **real** bit values at the moment the condition was
matched (or the real current bits on timeout) — never the requested `mask` echoed back.
This matters because a partial match is meaningful: an `ANY`-waiter on `A|B` that only
ever sees `A` set must get back `A` alone, not `A|B` fabricated from what it asked for.
`acrtos_event_selftest.cpp`'s T1 exists specifically to catch a regression of this kind
— the check `(got & kBitB) == 0` fails immediately if the fabrication bug comes back.
The value returned is captured (`satisfied_bits` in `scan_and_wake_locked()`) *before*
`clear_on_exit` is applied, so a waiter that asked for the bits to be cleared still
sees what was actually there when it woke.
