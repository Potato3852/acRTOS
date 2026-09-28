/**
 * @file ipc.hpp
 * @brief Synchronization primitives: Semaphore, Mutex, LockGuard and EventGroup.
 *
 * All blocking calls must be made from task context. Objects must outlive every
 * task that uses them and must not be destroyed while tasks are blocked on them.
 */
#pragma once
#include "task.hpp"

namespace acrtos::internal {

/**
 * @brief List of tasks blocked on one object, ordered by priority.
 * @details The highest priority is woken first; equal priorities are served FIFO.
 *          Not thread-safe: callers must hold a critical section.
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
    [[nodiscard]] TaskControlBlock* peek_highest() const noexcept { return wait_list_.peek_front(); }
};

} // namespace acrtos::internal

namespace acrtos {

/**
 * @brief Counting semaphore.
 *
 * give() hands the token directly to the highest-priority waiter if there is one;
 * otherwise it increments the count (saturating at UINT32_MAX).
 */
class Semaphore {
private:
    internal::WaitQueue wait_;
    uint32_t count_{0};

public:
    explicit Semaphore(uint32_t initial = 0) noexcept : count_(initial) {}

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    /**
     * @brief Take a token, blocking if none is available.
     * @param timeout_ms 0 never blocks, kWaitForever waits without limit.
     * @return true if a token was obtained; false on timeout, or if the task was
     *         suspended or deleted while waiting.
     * @note Task context only.
     */
    [[nodiscard]] bool take(TickType timeout_ms = kWaitForever) noexcept;

    /** @brief Take a token without blocking. @return true on success. */
    [[nodiscard]] bool try_take() noexcept;

    /** @brief Give a token; yields if a higher-priority waiter became ready. */
    void give() noexcept;

    /** @brief ISR-safe try_take(). */
    [[nodiscard]] bool try_take_from_isr() noexcept;

    /**
     * @brief Give a token from an ISR. Never yields.
     * @return true if a higher-priority task became ready; the caller should then
     *         call task_yield() (typically at the end of the ISR).
     */
    [[nodiscard]] bool give_from_isr() noexcept;
};

/**
 * @brief Non-recursive mutex with priority inheritance.
 *
 * While a higher-priority task waits, the owner runs at the waiter's priority. A task
 * that owns several mutexes runs at the highest priority among its own base priority
 * and their waiters. Inheritance is *not* transitive across chains of mutexes.
 * Ownership is passed directly to the highest-priority waiter on unlock().
 *
 * Restrictions: task context only, no timeout, no recursive locking, and only the
 * owner may unlock. A task must release its mutexes before it terminates.
 */
class Mutex {
private:
    internal::WaitQueue wait_;
    internal::TaskControlBlock* owner_{nullptr};
    Mutex* next_held_{nullptr};

    void add_held(internal::TaskControlBlock* t) noexcept;
    void remove_held(internal::TaskControlBlock* t) noexcept;
    [[nodiscard]] static uint8_t recompute_priority(const internal::TaskControlBlock* t) noexcept;

public:
    /**
     * @brief Acquire the mutex, blocking until it is available.
     * @return true when the mutex is owned by the caller; false if the caller tried
     *         to lock a mutex it already owns, or was suspended or deleted while waiting.
     */
    [[nodiscard]] bool lock() noexcept;

    /**
     * @brief Release the mutex, restoring the caller's inherited priority.
     * @return false if the caller is not the owner.
     */
    [[nodiscard]] bool unlock() noexcept;
};

/**
 * @brief RAII wrapper: locks a Mutex on construction and unlocks it on destruction.
 * @details Asserts (ACRTOS_ASSERT) if the lock could not be acquired.
 */
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

/**
 * @brief Set of 32 event flags that tasks can wait on with per-task conditions.
 *
 * Each waiter states its own condition: ANY or ALL of a bit mask. Because conditions
 * differ per waiter, set_bits() scans the whole wait list and wakes every task whose
 * condition is satisfied, in priority order. All 32 bits are available to the user.
 */
class EventGroup {
private:
    uint32_t current_bits_{0};
    internal::TaskList waiters_;

    [[nodiscard]] static bool bits_satisfy(uint32_t bits, uint32_t mask, bool wait_all) noexcept {
        return wait_all ? (bits & mask) == mask : (bits & mask) != 0;
    }
    [[nodiscard]] static bool condition_met(const internal::TaskControlBlock* t, uint32_t bits) noexcept {
        return t->event_wait_all ? (bits & t->event_wait_mask) == t->event_wait_mask
                                 : (bits & t->event_wait_mask) != 0;
    }
    [[nodiscard]] bool scan_and_wake_locked() noexcept;

public:
    [[nodiscard]] uint32_t wait_bits(uint32_t mask, bool wait_all, 
                                     bool clear_on_exit, 
                                     TickType timeout_ms = kWaitForever) noexcept;

    uint32_t set_bits(uint32_t mask) noexcept;
    uint32_t clear_bits(uint32_t mask) noexcept;
    [[nodiscard]] uint32_t get_bits() const noexcept;
    uint32_t set_bits_from_isr(uint32_t mask, bool& should_yield) noexcept;

};

} // namespace acrtos
