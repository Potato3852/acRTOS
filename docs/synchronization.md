# Synchronization primitives

## Semaphore

A counting semaphore (`ipc.hpp`). `count_` starts at whatever `initial` was passed to
the constructor.

- **`take(timeout_ms)`** — if `count_ > 0`, decrements it and returns immediately.
  Otherwise parks the current task on `wait_`, optionally arms a timeout, and calls
  `task_yield()`. Returns `false` if the wait ended by timeout (or by the task being
  suspended/deleted while parked) rather than a real `give()`. `timeout_ms == 0` never
  blocks — it's equivalent to `try_take()`.
- **`give()`** — if a task is already waiting, wakes the highest-priority one directly
  (the count never actually increments in this path — the token goes straight from
  giver to waiter). Otherwise increments `count_` (saturating at `UINT32_MAX`).
- **ISR variants** — `try_take_from_isr()` just reuses `try_take()` (safe from ISR
  context in acRTOS because critical sections are PRIMASK-based, not a task-only lock).
  `give_from_isr()` does the same work as `give()` but never calls `task_yield()`
  itself; it returns `true` if a higher-priority task became ready, and the caller
  (the ISR) is expected to call `task_yield()` once, typically at the end of the
  handler, rather than after every kernel call it makes.

## Mutex

A non-recursive mutex with **priority inheritance**: it exists to bound priority
inversion, the scenario where a low-priority task holds a resource a high-priority task
needs, while an unrelated medium-priority task keeps the CPU and the high-priority task
can't even get scheduled to wait its turn. Without inheritance, that medium-priority
task can starve the high-priority one indefinitely (`acrtos_selftest.cpp`'s T3 checks
exactly this).

- **`lock()`** — if the mutex is free, takes ownership immediately. If it's held and the
  caller's priority is higher than the owner's, the owner is boosted to the caller's
  priority via `Scheduler::set_task_priority()` *before* the caller parks itself — so
  the owner (and whatever it's doing) runs at the borrowed priority for as long as the
  boost stands.
- **`unlock()`** — hands ownership directly to the highest-priority waiter, if any, and
  recomputes the outgoing owner's priority as the max of its own `base_priority` and
  the highest waiter across all mutexes it still holds (`recompute_priority()`, via the
  `held_mutexes` intrusive list on the TCB).

**Known limitations** (see [limitations.md](limitations.md) for the full list):
inheritance is not transitive — if the owner is itself blocked waiting on a *different*
mutex, the boost does not propagate further down that chain — and if the task holding a
mutex is deleted (or otherwise never calls `unlock()`), every waiter is stuck forever;
the scheduler has no reverse mapping from a task to what it owns. This mirrors a
documented trade-off in FreeRTOS's own `vTaskDelete()`, not a beginner oversight.

## The common pattern

Every blocking primitive in acRTOS (`Semaphore::take`, `Mutex::lock`, `QueueCore::send`,
`EventGroup::wait_bits`) follows the same shape:

1. Under a `CriticalSection`, try the non-blocking path first.
2. If it can't succeed immediately and the timeout allows it, park the current task
   (record what it's waiting for, insert it into a wait list, start a timeout).
3. Leave the critical section, call `task_yield()`.
4. On wake, read back what the waking side wrote (or whether the timeout fired).

`Semaphore::take()` is the smallest example of the shape:

```cpp
bool Semaphore::take(TickType timeout_ms) noexcept {
    if (timeout_ms == 0) return try_take();

    internal::TaskControlBlock* task = nullptr;
    {
        port::CriticalSection guard;                 // 1. try the fast path
        if (count_ > 0) { --count_; return true; }

        task = Scheduler::instance().get_current_task();
        task->timeout_expired = false;
        wait_.park(task);                             // 2. park on the wait list
        if (timeout_ms != kWaitForever) {
            Scheduler::instance().start_timeout_for_current_task(ms_to_ticks(timeout_ms));
        }
    }                                                  // 3. critical section ends here

    task_yield();                                      // 3. give the CPU away
    return !task->timeout_expired;                     // 4. read back the outcome
}
```

`QueueCore` and `EventGroup` follow the identical shape, just with more state to record
in step 2 (`xfer_ptr` for the queue item / event mask, `event_wait_all`,
`event_clear_on_exit`) and more to read back in step 4 (the transferred item, or the
matched bits).
