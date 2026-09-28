/**
 * @file scheduler.hpp
 * @brief Preemptive O(1) scheduler (priority bitmask + per-priority ready lists).
 *
 * Scheduler methods are meant to be called from task context. The only
 * interrupt-context entry point is tick(), driven by the port's SysTick handler.
 * Interrupt handlers talk to the kernel through the `*_from_isr` methods of the
 * IPC objects.
 */
#pragma once
#include "task.hpp"
#include "port.hpp"
#include "acRtosConfig.hpp"

#include <cstddef>
#include <type_traits>
#include <concepts>
#include <utility>
#include <new>

namespace acrtos::internal {

/**
 * @brief One FIFO list per priority plus a bitmask of non-empty lists.
 * Highest ready priority is 31 - clz(mask) — constant time on Cortex-M.
 */
class ReadyManager {
private:
    TaskList ready_lists_[config::kMaxPriorities];
    uint32_t ready_bitmask_{0};

public:
    constexpr ReadyManager() noexcept = default;
    ReadyManager(const ReadyManager&) = delete;
    ReadyManager& operator=(const ReadyManager&) = delete;

    /** @brief Append @p task to the list of its current priority. */
    void add(TaskControlBlock* task) noexcept;

    /** @brief Remove @p task from the list of its current priority. */
    void remove(TaskControlBlock* task) noexcept;

    /** @brief Pop the oldest task of the highest non-empty priority, or nullptr. */
    [[nodiscard]] TaskControlBlock* get_highest_priority_task() noexcept;

    /** @brief Highest priority that has a ready task. The manager must not be empty. */
    [[nodiscard]] uint8_t peek_highest_priority() const noexcept;

    [[nodiscard]] bool is_empty() const noexcept { return ready_bitmask_ == 0; }
};

} // namespace acrtos::internal

namespace acrtos {

/**
 * @brief The kernel singleton: task table, ready lists, delay list and tick counter.
 *
 * Scheduling policy: strict priority, higher number wins. Tasks of equal priority
 * share the CPU round-robin, switching on every tick.
 */
class Scheduler {
private:
    Scheduler() = default;

    using TrampolineFn = void(*)(void*);
    internal::TaskControlBlock* allocate_tcb() noexcept;
    internal::TaskControlBlock* create_task_impl(uint32_t* stack_buf, std::size_t stack_words, TrampolineFn trampoline,void* callable_ptr,uint8_t priority) noexcept;

    void add_to_ready_queue(internal::TaskControlBlock* tcb) noexcept;
    [[nodiscard]] bool needs_preemption() const noexcept;

    internal::TaskControlBlock task_table_[config::kMaxTasks];

    internal::TaskControlBlock* current_task_{nullptr};
    internal::ReadyManager ready_mgr_;
    internal::DelayList delay_list_;
    uint8_t task_count_{0};
    bool started_{false};
    volatile TickType tick_count_{0};

public:
    static Scheduler& instance() noexcept {
        static Scheduler instance;
        return instance;
    }

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /**
     * @brief Create a task from any callable and make it ready.
     *
     * The task stack is a static array owned by this template instantiation, and the
     * callable is copied to the top of that stack. Each instantiation is unique per
     * source location because of the defaulted @p Tag lambda, so **every task needs
     * its own `create_task(...)` call**. Reaching the same call more than once (for
     * example in a loop) is a programming error: it is caught by an assert, and the
     * extra call returns an invalid Task when asserts are disabled.
     *
     * If the scheduler is already running and the new task has a higher priority than
     * the current one, it preempts immediately.
     *
     * @tparam StackWords Stack size in 32-bit words (at least 64, default config::kStackSize).
     * @tparam Tag        Leave defaulted; makes each call site a distinct instantiation.
     * @tparam F          Callable type, invocable with no arguments.
     * @param callable    Task body. When it returns, the task deletes itself.
     * @param priority    0 .. config::kMaxPriorities-1 (0 is shared with the idle task).
     * @return Handle to the task, or an invalid Task if creation failed.
     */
    template <std::size_t StackWords = config::kStackSize, auto Tag = []{}, typename F>
    requires std::invocable<F>
    Task create_task(F&& callable, uint8_t priority = 1) noexcept {
        static_assert(StackWords >= 64, "stack too small to be useful");
        static_assert((StackWords * sizeof(uint32_t)) % 8 == 0, "must be 8-byte aligned");

        static bool is_created = false;
        ACRTOS_ASSERT(!is_created && "create_task called multiple times at the same call-site (e.g. inside a loop)");
        if (is_created) return Task{};
        is_created = true;
        
        alignas(8) static uint32_t storage[StackWords];

        using DecayedF = std::decay_t<F>;
        static_assert(sizeof(DecayedF) < (StackWords * sizeof(uint32_t)) / 2, "Callable is too large for the task stack");
        static_assert(alignof(DecayedF) <= 8, "Callable alignment too strict for task stack");

        uint8_t* stack_bytes = reinterpret_cast<uint8_t*>(storage + StackWords);
        stack_bytes -= sizeof(DecayedF);
        const std::size_t align_offset = reinterpret_cast<std::uintptr_t>(stack_bytes) % 8;
        stack_bytes -= align_offset;

        DecayedF* stored_callable = new (stack_bytes) DecayedF(std::forward<F>(callable));

        auto trampoline = [](void* ctx) {
            auto* fn = static_cast<DecayedF*>(ctx);
            (*fn)();
            fn->~DecayedF();

            Scheduler::instance().delete_task(Scheduler::instance().get_current_task());
            while (true) { port::wait_for_interrupt(); }
        };

        auto* tcb = create_task_impl(storage, StackWords, trampoline, stored_callable, priority);

        return Task(tcb);
    }

    /**
     * @brief Create the idle task (priority 0), start SysTick and switch to the first task.
     * @note Does not return. Create all initial tasks before calling it.
     */
    [[noreturn]] void start() noexcept;

    /** @brief Ticks since start. Safe to call from an ISR. */
    [[nodiscard]] TickType get_tick_count() const noexcept;

    /**
     * @brief Block the current task until the tick counter reaches @p wake_time.
     * @details Returns immediately if @p wake_time has already passed. Intended for
     *          periodic loops without drift: `t += period; delay_until(t);`.
     */
    void delay_until(TickType wake_time) noexcept;

    /**
     * @brief Block the current task for at least @p ms milliseconds (rounded up to whole ticks).
     * @note `delay_ms(0)` returns immediately without yielding.
     */
    void delay_ms(TickType ms) noexcept;

    /** @brief Advance the tick counter, wake expired sleepers and request a switch if needed. Called by the port. */
    void tick() noexcept;

    /** @brief Task that is currently running, or nullptr before start(). */
    [[nodiscard]] internal::TaskControlBlock* get_current_task() noexcept { return current_task_; }

    /** @brief Kernel internal: used by schedule_next_task(). */
    void set_current_task(internal::TaskControlBlock* task) noexcept { current_task_ = task; }

    /** @brief Kernel internal: used by schedule_next_task(). */
    [[nodiscard]] internal::ReadyManager& get_ready_manager() noexcept { return ready_mgr_; }

    /** @brief Remove a task from scheduling and from any wait/delay list it is in. Yields if it is the caller. */
    void suspend_task(internal::TaskControlBlock* task) noexcept;

    /** @brief Move a suspended task to the ready lists; yields if it should preempt. */
    void resume_task(internal::TaskControlBlock* task) noexcept;

    /** @brief Mark a task Deleted and unlink it from all lists. Its resources are not reclaimed. */
    void delete_task(internal::TaskControlBlock* task) noexcept;

    /**
     * @brief Move a waiting task into the ready lists. Does not yield.
     * @note Call inside a critical section.
     * @return true if the woken task has a higher priority than the running one and a yield is due.
     */
    bool make_task_ready(internal::TaskControlBlock* task) noexcept;

    /** @brief Put the current task into the delay list as a wait timeout. Call inside a critical section. */
    void start_timeout_for_current_task(TickType ticks) noexcept { delay_list_.insert(current_task_, ticks); }

    /** @brief Change the effective priority of @p task and re-sort the list it is in. Used by priority inheritance. */
    void set_task_priority(internal::TaskControlBlock* task, uint8_t priority) noexcept;

    /** @brief Remove @p task from the delay list if it is there. */
    void cancel_timeout(internal::TaskControlBlock* task) noexcept;
};

} // namespace acrtos