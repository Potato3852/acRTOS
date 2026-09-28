# Queue<T, N>

## QueueItem concept

`Queue<T, N>` accepts any `T` that satisfies `QueueItem`: an object type, not an array,
not `const`/`volatile`, and `std::is_trivially_copyable_v<T>`. Items are moved with
`std::memcpy`, not the copy constructor, so anything with non-trivial copy semantics
(owning a pointer, a `std::string`, a virtual destructor) is rejected at compile time
rather than silently bit-copied into a broken state. `send()`/`send_from_isr()` are
templated on `U` with `requires std::same_as<U, T>`, so there's no implicit conversion
on the way in — `queue.send(3)` for a `Queue<uint8_t, N>` is a compile error, not a
silent narrowing.

## Ring buffer

`QueueCore` is the non-template, byte-oriented engine; `Queue<T, N>` is a thin typed
wrapper that owns the backing storage (`alignas(T) std::byte storage_[sizeof(T) * N]`)
and forwards to it. The core itself is a classic circular buffer: `head_` is the index
of the oldest item, `tail_` is where the next `send()` writes, `count_` tracks how many
slots are occupied so full/empty can be told apart without wasting a slot as a sentinel.

## The handoff design

A plain "wake whoever is waiting" wait queue doesn't fit a bounded queue, because
`send()` and `receive()` can each block for a *different* reason: `receive()` blocks
when the buffer is empty, `send()` blocks when it's full. Those are two separate wait
queues (`senders_`, `receivers_`), and by construction at most one of them is ever
non-empty at a time — if the buffer isn't full, no sender is waiting; if it isn't
empty, no receiver is waiting.

The other problem a naive design hits is what to do with the data itself. If a woken
task just re-checked the queue after waking, you'd need the waking side to still hold
the item somewhere, and the woken task would need to re-enter the critical section to
re-read it. `QueueCore` avoids the re-check entirely with a direct handoff: a blocked
task parks a pointer to *its own* buffer in `TaskControlBlock::xfer_ptr` before
sleeping (its own stack, for `send()` the buffer being sent, for `receive()` the buffer
that will receive), and the task that wakes it copies data straight into that pointer
while still holding the critical section. The woken task never has to re-check
anything — by the time it looks at its own buffer, the data is already there.

Concretely: `try_send_locked()` checks `receivers_` first — if a receiver is waiting, it
copies directly into `receiver->xfer_ptr` and calls `make_task_ready()`, skipping the
ring buffer entirely. Only if nobody is waiting does the item actually get pushed into
the ring. `try_receive_locked()` is the mirror: pop from the ring, then if a sender is
waiting, push *its* pending item into the now-freed slot and wake it.

## ISR variants

`send_from_isr()` / `receive_from_isr()` never block — if the non-blocking path fails
(full on send, empty on receive), they just return `false`. Both set `should_yield` to
`true` if the operation woke a higher-priority task; the ISR is expected to call
`task_yield()` itself (typically once, at the end of the handler) rather than the queue
doing it, since `task_yield()` from inside an ISR that's already about to return is
just pending PendSV either way.

## Why items must be small (<= 64 bytes)

`Queue<T, N>` statically rejects `sizeof(T) > 64`. Every `push_raw`/`pop_raw` and every
handoff copy runs with interrupts disabled (inside a `CriticalSection`), because the
ring buffer and both wait queues are shared state. A large item means a long
`memcpy` with interrupts masked, which directly becomes interrupt latency for the whole
system. 64 bytes is a deliberately conservative cap to keep that worst case bounded; if
you need to move something bigger, queue a pointer to it instead of the object itself.
