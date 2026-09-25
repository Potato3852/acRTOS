/**
 * @file scheduler.hpp
 * @brief Preemptive O(1) scheduler (priority bitmask + ready lists).
 *
 * Do not call these methods from an ISR unless a dedicated `_from_isr` API
 * exists (none yet). Ticking is done from the port's SysTick handler.
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

    void add(TaskControlBlock* task) noexcept;
    void remove(TaskControlBlock* task) noexcept;

    [[nodiscard]] TaskControlBlock* get_highest_priority_task() noexcept;
    [[nodiscard]] uint8_t peek_highest_priority() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept { return ready_bitmask_ == 0; }
};

} // namespace acrtos::internal

namespace acrtos {

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

    template <std::size_t StackWords = config::kStackSize, auto Tag = []{}, typename F>
    requires std::invocable<F>
    Task create_task(F&& callable, uint8_t priority = 1) noexcept {
        static_assert(StackWords >= 64, "stack too small to be useful");
        static_assert((StackWords * sizeof(uint32_t)) % 8 == 0, "must be 8-byte aligned");

        static bool is_created = false;
        ACRTOS_ASSERT(!is_created && "create_task called multiple times at the same call-site (e.g. inside a loop)");
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

            Scheduler::instance().suspend_task(Scheduler::instance().get_current_task());
            while (true) {
                port::wait_for_interrupt();
            }
        };

        auto* tcb = create_task_impl(storage, StackWords, trampoline, stored_callable, priority);

        return Task(tcb);
    }

    [[noreturn]] void start() noexcept;

    [[nodiscard]] TickType get_tick_count() const noexcept;
    void delay_until(TickType wake_time) noexcept;
    void delay_ms(TickType ms) noexcept;
    void tick() noexcept;

    [[nodiscard]] internal::TaskControlBlock* get_current_task() noexcept { return current_task_; }
    void set_current_task(internal::TaskControlBlock* task) noexcept { current_task_ = task; }
    [[nodiscard]] internal::ReadyManager& get_ready_manager() noexcept { return ready_mgr_; }

    void suspend_task(internal::TaskControlBlock* task) noexcept;
    void resume_task(internal::TaskControlBlock* task) noexcept;
    void delete_task(internal::TaskControlBlock* task) noexcept;

    /**
     * @brief Move a waiting task into the ready lists. Does not yield.
     * @return true if the woken task should preempt the current one.
     */
    bool make_task_ready(internal::TaskControlBlock* task) noexcept;
    void start_timeout_for_current_task(TickType ticks) noexcept { delay_list_.insert(current_task_, ticks); }
    void set_task_priority(internal::TaskControlBlock* task, uint8_t priority) noexcept;
    void cancel_timeout(internal::TaskControlBlock* task) noexcept;
};

} // namespace acrtos