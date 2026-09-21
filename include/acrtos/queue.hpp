/**
 * @file queue.hpp
 * @brief Bounded FIFO queue. Items are copied by value.
 *
 * QueueCore is a non-template, byte-oriented implementation compiled once.
 * Queue<T, N> is a thin typed wrapper that owns the storage.
 */
#pragma once
#include "ipc.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace acrtos {

/**
 * @brief Types that may be stored in a Queue.
 */
template <typename T>
concept QueueItem = std::is_object_v<T> &&
                    !std::is_array_v<T> &&
                    !std::is_const_v<T> &&
                    !std::is_volatile_v<T> &&
                    std::is_trivially_copyable_v<T>;


namespace internal {

/**
 * @brief Ring buffer plus two priority-ordered wait queues.
 *
 * A receiver waits only while the queue is empty and a sender only while it is
 * full, so at most one of the two wait queues is non-empty. A blocked task
 * parks a pointer to its own buffer in TaskControlBlock::xfer_ptr and the
 * waking side copies the data directly (handoff), so a woken task never has to
 * re-check the queue.
 */
class QueueCore {
private:
    // Ring-buffer helpers. Call ONLY inside a critical section.
    void push_raw(const void* item) noexcept;
    void pop_raw(void* out) noexcept;

    // The *_locked helpers must be called inside a critical section.
    bool try_send_locked(const void* item, bool& should_yield) noexcept;
    bool try_receive_locked(void* out, bool& should_yield) noexcept;
    [[nodiscard]] TaskControlBlock* block_current_locked(WaitQueue& queue, void* xfer, TickType timeout_ms) noexcept;
    [[nodiscard]] static bool finish_call(TaskControlBlock* blocked, bool should_yield) noexcept;

    WaitQueue senders_;
    WaitQueue receivers_;

    std::byte* storage_;
    std::size_t item_size_;
    std::size_t capacity_;
    std::size_t head_{0};    // index of the oldest item
    std::size_t tail_{0};    // index where the next item goes
    std::size_t count_{0};

public:
    QueueCore(std::byte* storage, std::size_t item_size, std::size_t capacity) noexcept;
    ~QueueCore();

    QueueCore(const QueueCore&) = delete;
    QueueCore& operator=(const QueueCore&) = delete;
    QueueCore(QueueCore&&) = delete;
    QueueCore& operator=(QueueCore&&) = delete;

    /** @brief timeout 0 never blocks, kWaitForever waits without limit. */
    [[nodiscard]] bool send(const void* item, TickType timeout_ms) noexcept;
    [[nodiscard]] bool receive(void* out, TickType timeout_ms) noexcept;

    /** @brief Never blocks. should_yield is set if a higher-priority task became ready. */
    [[nodiscard]] bool send_from_isr(const void* item, bool& should_yield) noexcept;
    [[nodiscard]] bool receive_from_isr(void* out, bool& should_yield) noexcept;

    [[nodiscard]] std::size_t count() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
};

} // namespace internal

/**
 * @brief Fixed-capacity queue of N items of type T.
 */
template <QueueItem T, std::size_t N>
class Queue {
    static_assert(N > 0, "Queue capacity must be at least 1");
    static_assert(sizeof(T) <= 64, "Queue items must be small (<= 64 bytes) to avoid holding critical sections");

private:
    // Member order matters: storage_ must exist before core_ takes its address.
    alignas(T) std::byte storage_[sizeof(T) * N];
    internal::QueueCore core_{storage_, sizeof(T), N};

public:
    Queue() = default;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&) = delete;
    Queue& operator=(Queue&&) = delete;

    template<typename U>
    requires std::same_as<U, T>
    [[nodiscard]] bool send(const U& item, TickType timeout_ms = kWaitForever) noexcept {
        return core_.send(&item, timeout_ms);
    }

    [[nodiscard]] bool receive(T& out, TickType timeout_ms = kWaitForever) noexcept {
        return core_.receive(&out, timeout_ms);
    }

    template<typename U>
    requires std::same_as<U, T>
    [[nodiscard]] bool send_from_isr(const U& item, bool& should_yield) noexcept {
        return core_.send_from_isr(&item, should_yield);
    }

    [[nodiscard]] bool receive_from_isr(T& out, bool& should_yield) noexcept {
        return core_.receive_from_isr(&out, should_yield);
    }

    [[nodiscard]] std::size_t count() const noexcept { return core_.count(); }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }
};

} // namespace acrtos
