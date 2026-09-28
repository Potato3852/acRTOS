# acRTOS (absolute-cinema-Real-Time-Operating-System)

A small preemptive real-time kernel for Cortex-M4, written from scratch in C++20 as a
learning project. No vendor HAL dependency in the kernel itself.

![acRTOS](docs/assets/absolute-cinema.png)


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

## Documentation

| Topic | |
|---|---|
| [Architecture](docs/architecture.md) | TCB layout, scheduler, ready/wait/delay lists, PendSV |
| [Synchronization primitives](docs/synchronization.md) | Semaphore, Mutex, priority inheritance |
| [Queues](docs/queue.md) | `Queue<T, N>`, the handoff design |
| [Event groups](docs/event-group.md) | ANY/ALL waits, ISR usage |
| [Testing](docs/testing.md) | on-target self-checking test suites |
| [Known limitations](docs/limitations.md) | what this kernel deliberately does not do |

## Motivation
Why build another RTOS when FreeRTOS and Zephyr already exist?

- **Demystifying the black magic:** I wanted to write `PendSV` context switches in raw ARM assembly, trigger genuine HardFaults, and actually understand where every single register goes.
- **Modern C++20 on bare metal:** FreeRTOS is iconic, but `void*` and C macros in 2026 feel a bit rustic. `acRTOS` uses C++20 concepts, compile-time stack sizing, and `placement new` without any dynamic memory allocation (`malloc` and heap are strictly forbidden).
- **Absolute Cinema naming, serious internals:** Behind the meme name lies a full $O(1)$ scheduler, priority inheritance to avoid priority inversion, and zero-copy queue handoffs.
- **No vendor HAL black boxes:** Just pure C++, assembly, and register manipulation running on STM32F401.

## Status

Personal learning project, not production-hardened. The scheduler, mutexes,
semaphores, queues and event groups are exercised by on-target self-checking
test suites (see [docs/testing.md](docs/testing.md)) and have survived
mutation testing (deliberately reintroducing bugs to confirm the tests catch
them). Tested only on STM32F401 (Cortex-M4F). See
[docs/limitations.md](docs/limitations.md) for known gaps.

## Acknowledgements & AI Usage

- **Nikita Kuzmin** — Author & Architect. Drove all architectural decisions, wrote the C++20 and PendSV assembly, and pestered the LLMs with relentless questions.
- **Gemini & Claude Code** — Virtual Senior Developers. Answered the 3 AM questions, guided the implementation, and gave virtual slaps on the wrist every time I tried to slip in dodgy or poorly written code.

## License

[MIT](LICENSE)
