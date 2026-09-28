# acRTOS

A small preemptive real-time kernel for Cortex-M4, written from scratch in C++20 as a
learning project. No vendor HAL dependency in the kernel itself.

I started this in January while getting into embedded development, to actually
understand how an RTOS works under the hood — not just use one — and to see what C++20
buys you over the C FreeRTOS is written in (concepts for `Queue<T, N>`'s item type, a
`CriticalSection` RAII guard instead of manual enter/exit pairs, `Task` as a small
value-type handle instead of a raw `TaskHandle_t`).

## Features

- Preemptive priority scheduler, O(1) task selection via a 32-bit ready bitmask
- Priority inheritance mutexes, counting semaphores
- Bounded queues (`Queue<T, N>`) with direct handoff between sender and receiver
- Event groups (`EventGroup`) with ANY/ALL waits and `clear_on_exit`
- Per-task stacks sized at compile time, no dynamic allocation
- Self-checking on-target test suites (no UART required, results readable in a debugger)

See [docs/architecture.md](docs/architecture.md) for how it fits together.

## Quick start

```cpp
#include <acrtos.hpp>

void blink() {
    while (true) {
        // toggle a pin
        acrtos::Scheduler::instance().delay_ms(500);
    }
}

int main() {
    auto& sched = acrtos::Scheduler::instance();
    sched.create_task(blink, /*priority=*/1);
    sched.start();   // never returns
}
```

## Building

<!-- TODO: describe your toolchain / build steps here (e.g. IDE used, compiler,
     required flags such as -std=c++20 -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16,
     and how to run the on-target selftests from docs/testing.md). -->

## Documentation

| Topic | |
|---|---|
| [Architecture](docs/architecture.md) | TCB layout, scheduler, ready/wait/delay lists, PendSV |
| [Synchronization primitives](docs/synchronization.md) | Semaphore, Mutex, priority inheritance |
| [Queues](docs/queue.md) | `Queue<T, N>`, the handoff design |
| [Event groups](docs/event-group.md) | ANY/ALL waits, ISR usage |
| [Testing](docs/testing.md) | on-target self-checking test suites |
| [Known limitations](docs/limitations.md) | what this kernel deliberately does not do |

## Status

Personal/educational project, not production-hardened. The scheduler, priority
inheritance, queues and event groups are exercised by the on-target self-checking test
suites in `tests/` (see [docs/testing.md](docs/testing.md)); `acrtos_time_selftest.cpp`
is still a skeleton. Known gaps are listed honestly in
[docs/limitations.md](docs/limitations.md) rather than hidden.

## License

All rights reserved. No license is currently granted for reuse; this is published for
reading and learning purposes. Reach out if you'd like to use it for something.
