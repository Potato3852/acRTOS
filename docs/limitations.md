# Known limitations

Honest list of things this kernel deliberately does not solve, and why.

- **No memory reclamation for deleted tasks.** `delete_task()` marks a task `Deleted`
  and unlinks it from every list, but its TCB slot and stack memory are never reused.
  Stacks are fixed-size static arrays sized per `create_task<StackWords, Tag>` call
  site at *compile* time, not allocated from a heap — there is no relocating allocator
  behind them to safely defragment or repurpose the memory once a task is gone, and
  adding one would mean dynamic allocation, which the kernel deliberately avoids
  everywhere else. In practice this means `kMaxTasks` should be sized for the maximum
  number of tasks *ever* created over the process lifetime, not the maximum alive at
  once, if the application deletes tasks at runtime.

- **Deleting a task that owns a `Mutex`, or that another task is blocked waiting on
  (`Semaphore`/`Mutex`/`Queue`/`EventGroup`), leaves the waiter stuck.** The scheduler
  has no reverse mapping from a task to the IPC objects it currently owns or is awaited
  by, so `delete_task()` can unlink the deleted task from its *own* wait list but has
  no way to find and release resources it *owned*. This is the same trade-off FreeRTOS
  documents for its own `vTaskDelete()` — it is a known, accepted gap in this class of
  kernel, not a beginner mistake specific to acRTOS. The practical rule is: design tasks
  so they release what they hold before they can be deleted, or simply never delete a
  task that might be holding a mutex another task needs.

- **Priority inheritance is not transitive** and does not track multiple
  simultaneously-held mutexes per task beyond taking the max of their waiters (see
  [synchronization.md](synchronization.md)). If task A owns mutex M1 and is itself
  blocked waiting on mutex M2 owned by task B, a task C waiting on M1 boosts A, but
  that boost does not propagate from A to B through M2.

- **A `create_task(...)` call reached more than once at the same source location
  aliases the same static stack storage.** Guarded by a runtime assert
  (`create_task called multiple times at the same call-site`), not a compile error.
  The root cause: the per-call-site uniqueness comes from the defaulted `auto Tag =
  []{}` template parameter, whose type is unique per *source line*, not per
  invocation — so the same line reached twice (e.g. inside a loop) instantiates the
  same template, and thus reuses the same `static uint32_t storage[]`.
  `create_task_pitfall_selftest.cpp` demonstrates the aliasing with real stack
  addresses; see [architecture.md](architecture.md#scheduler) for the full mechanism.

- **PRIMASK-based critical sections, not BASEPRI.** All interrupts are masked during
  a critical section, including ones that could in principle run at a truly higher
  priority than the kernel's own bookkeeping. A BASEPRI-based scheme (masking only
  interrupts at or below a configured priority, as FreeRTOS's
  `configMAX_SYSCALL_INTERRUPT_PRIORITY` does) would let a genuinely high-priority
  interrupt preempt even a kernel critical section; that is not implemented here.
