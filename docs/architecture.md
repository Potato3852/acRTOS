# Architecture

## Task Control Block

`TaskControlBlock` (`task.hpp`) is a plain struct, one per task slot in
`Scheduler::task_table_`. No task owns memory outside its own stack; everything the
kernel needs to schedule and unblock a task lives in this struct.

| Field | Purpose |
|---|---|
| `sp` | Saved PSP while the task is not running. Written by `schedule_next_task()`. |
| `state` | Current `TaskState` (see below). |
| `delay_ticks` | Delta to the previous node while the task is linked into `DelayList`. |
| `stack_base` / `stack_end` | Bounds of the task's stack. `stack_base[0]` holds `kStackCanary`, checked on every switch. |
| `priority` / `base_priority` | Effective priority (may be raised by inheritance) and the priority the task was created with. |
| `prev` / `next` | Links in whichever `TaskList` currently owns the task (a ready list or a wait list). |
| `delay_prev` / `delay_next` | Links in `DelayList`, independent of the fields above — a task can be linked into a wait list *and* the delay list at the same time (blocking with a timeout). |
| `held_mutexes` | Head of an intrusive singly linked list of `Mutex` objects this task owns, via `Mutex::next_held_`. Used by priority inheritance to recompute the task's effective priority. |
| `wait_list` | The `WaitQueue`/`TaskList` the task is parked on, or `nullptr`. Lets `suspend_task()` / `delete_task()` find and unlink a blocked task without knowing which primitive it's blocked on. |
| `in_delay_list` | True while linked into `DelayList`. |
| `timeout_expired` | Set by whatever unblocked the task other than a normal wake: timeout, suspend, or delete. The primitive's blocking call reads this after `task_yield()` returns to decide its own return value. |
| `xfer_ptr` | Handoff pointer. For `Queue`: address of the blocked task's own item buffer. For `EventGroup`: address to write the matched bits into. |
| `event_wait_mask` / `event_wait_all` / `event_clear_on_exit` | A parked `EventGroup` waiter's own wait condition, read back by `scan_and_wake_locked()`. |

## Task states

```
              create_task()
                   |
                   v
     +--------> Ready <---------------+
     |            |                   |
     | switch-in  | switch-out        | wake / resume
     |            v                   |
     |         Running                |
     |            |                   |
     |   blocks --+-- suspend/delete  |
     |            |                   |
     |            v                   |
     +------- Blocked / Suspended ----+
                   |
                   | delete_task()
                   v
                Deleted
```

`Deleted` is terminal: `delete_task()` unlinks the task from every list and marks it
`Deleted`, but its TCB slot and stack memory are **not** reclaimed or reused — see
[limitations.md](limitations.md).

## Scheduler

- **Ready list.** One FIFO `TaskList` per priority (`ReadyManager::ready_lists_`) plus a
  32-bit bitmask of non-empty priorities. The highest ready priority is found in O(1) via
  `31 - __builtin_clz(ready_bitmask_)`, which is why `config::kMaxPriorities` is capped
  at 32.
- **Delay list.** A delta-list (`DelayList`): each node stores ticks remaining *after*
  its predecessor expires, so `tick()` only decrements the head and only walks as many
  nodes as actually expire that tick — not the whole list.
- **`create_task<StackWords, Tag>` trick.** The task's stack is a `static uint32_t
  storage[StackWords]` array, and the callable is placement-new'd onto the top of it.
  That static storage has to belong to one, and only one, task — so each instantiation
  of the template needs to be unique. The defaulted `Tag = []{}` non-type template
  parameter achieves that: a lambda's closure type is unique per point in the source
  where it's written, so every *source line* that calls `create_task(...)` gets its own
  `storage`. The consequence is the rule enforced by the `is_created` guard: **one
  `create_task(...)` call per task, never inside a loop with a shared callable** — the
  same call site reached twice reuses the same `Tag`, hence the same `storage`, and the
  second "task" would alias the first one's stack. See
  `create_task_pitfall_selftest.cpp` for a runnable demonstration and
  [limitations.md](limitations.md) for the guard that turns this into a loud assert
  instead of silent corruption.

## Context switching

The scheduler never switches context itself — it only decides *who* should run next.
The actual switch happens in the `PendSV_Handler` (`port_asm.s`), because PendSV is the
one exception on Cortex-M whose priority can be set to the lowest in the system, so it
always runs after every other ISR has finished and never interrupts one.

- **`task_yield()`** (`port_asm.s`) just pends PendSV (`ICSR.PENDSVSET`) and returns.
  The actual switch happens later, once the CPU is free to take the lowest-priority
  exception — this is what makes it safe to call from inside a `CriticalSection` (PRIMASK
  doesn't block SysTick/PendSV pending, only their handlers).
- **`PendSV_Handler`** saves the callee-saved registers of the outgoing task (r4-r11,
  and s16-s31 if the FPU is in use), stores the resulting `psp` into `current_sp`, calls
  `schedule_next_task()`, then restores the same set of registers for the task
  `schedule_next_task()` selected and returns via `EXC_RETURN` into it.
- **`schedule_next_task()`** (`scheduler.cpp`) is the only place that talks to both
  `current_sp` and the `Scheduler`: it stores `current_sp` into the outgoing task's
  `sp`, puts it back on the ready list if it's still `Running` (i.e. it wasn't blocked,
  suspended or deleted before yielding), asks `ReadyManager` for the next task, and
  loads its `sp` into `current_sp` for PendSV to restore. Before touching the outgoing
  task it also checks `stack_base[0] == kStackCanary` — this is the stack-overflow
  check, and it only catches overflow *at the moment of a switch*, not the instant it
  happens.
- The very first switch (`port::start_hardware_and_yield()`) sets `psp` to 0 so that
  `PendSV_Handler` recognizes there is no outgoing task to save and jumps straight to
  restoring the first one.

## Critical sections

PRIMASK-based (`CriticalSection`, RAII): the constructor disables all interrupts and the
destructor restores the previous PRIMASK, so nested sections are safe. All IPC
primitives modify shared state only inside a critical section; see
[synchronization.md](synchronization.md) for the pattern every primitive follows
(`try_*_locked` / `block_current_locked` / handoff via `xfer_ptr`).

Masking through PRIMASK rather than BASEPRI means a critical section blocks *every*
interrupt, not just ones at or below the kernel's own priority — see
[limitations.md](limitations.md).
