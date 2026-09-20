/**
 * @file ipc.hpp
 * @brief Synchronization primitives built on WaitQueue.
 */
#pragma once
#include "task.hpp"

namespace acrtos::internal {

/**
 * @brief Ordered list of tasks blocked on one object.
 * Highest priority is woken first; equals stay FIFO.
 */
class WaitQueue {
private:
    TaskList wait_list_{};

public:
    WaitQueue() = default;
    WaitQueue(const WaitQueue&) = delete;
    WaitQueue& operator=(const WaitQueue&) = delete;

    void park(TaskControlBlock* task) noexcept;
    [[nodiscard]] TaskControlBlock* wake_highest() noexcept;
    [[nodiscard]] bool is_empty() const noexcept { return wait_list_.is_empty(); }
};

} // namespace acrtos::internal

namespace acrtos {

/**
 * @brief Counting semaphore. Not ISR-safe (no give_from_isr yet). No timeout. 
 */
class Semaphore {
private:
    internal::WaitQueue wait_;
    uint32_t count_{0};

public:
    explicit Semaphore(uint32_t initial = 0) noexcept : count_(initial) {}

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    [[nodiscard]] bool take(TickType timeout_ms = kWaitForever) noexcept;
    [[nodiscard]] bool try_take() noexcept;
    void give() noexcept;

    [[nodiscard]] bool try_take_from_isr() noexcept;
    [[nodiscard]] bool give_from_isr() noexcept;
};

class Mutex {
private:
    internal::WaitQueue wait_;
    internal::TaskControlBlock* owner_{nullptr};

public:
    [[nodiscard]]bool lock() noexcept;
    [[nodiscard]]bool unlock() noexcept;
};

class LockGuard {
private:
    Mutex& m_;
public:
    explicit LockGuard(Mutex& m) noexcept : m_(m) {
        [[maybe_unused]] const bool ok = m_.lock();
        ACRTOS_ASSERT(ok);
    }
    ~LockGuard() noexcept { (void)m_.unlock(); }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

} // namespace acrtos
