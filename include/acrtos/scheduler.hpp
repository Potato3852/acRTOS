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

    internal::TaskControlBlock* allocate_tcb() noexcept;
    void add_to_ready_queue(internal::TaskControlBlock* tcb) noexcept;
    [[nodiscard]] bool needs_preemption() const noexcept;

    internal::TaskControlBlock task_table_[config::kMaxTasks];
    internal::TaskControlBlock* current_task_{nullptr};
    internal::ReadyManager ready_mgr_;
    internal::DelayList delay_list_;
    uint8_t task_count_{0};
    bool started_{false};

public:
    static Scheduler& instance() noexcept {
        static Scheduler instance;
        return instance;
    }

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    template <typename F>
    requires std::invocable<F>
    Task create_task(F&& callable, uint8_t priority = 1) noexcept {
        ACRTOS_ASSERT(priority < config::kMaxPriorities && "priority must be 0 .. kMaxPriorities-1");
        if (priority >= config::kMaxPriorities) {
            return Task{nullptr};
        }

        internal::TaskControlBlock* tcb = allocate_tcb();
        if (!tcb) {
            return Task{nullptr};
        }

        using DecayedF = std::decay_t<F>;
        static_assert(sizeof(DecayedF) < (config::kStackSize * sizeof(uint32_t)) / 2, "Callable is too large for the task stack");
        static_assert(alignof(DecayedF) <= 8, "Callable alignment too strict for task stack");

        uint8_t* stack_end = reinterpret_cast<uint8_t*>(&tcb->stack[config::kStackSize]);

        stack_end -= sizeof(DecayedF);
        const std::size_t align_offset = reinterpret_cast<std::uintptr_t>(stack_end) % 8;
        stack_end -= align_offset;

        DecayedF* stored_callable = new (stack_end) DecayedF(std::forward<F>(callable));
        uint32_t* hw_stack_top = reinterpret_cast<uint32_t*>(stack_end);

        auto trampoline = [](void* ctx) {
            auto* fn = static_cast<DecayedF*>(ctx);
            (*fn)();
            fn->~DecayedF();

            Scheduler::instance().suspend_task(Scheduler::instance().get_current_task());
            while (true) {
                port::wait_for_interrupt();
            }
        };

        tcb->sp = internal::init_task_stack(hw_stack_top, trampoline, stored_callable);
        tcb->priority = priority;
        tcb->base_priority = priority;
        tcb->stack[0] = kStackCanary;

        add_to_ready_queue(tcb);

        if (started_ && current_task_ != nullptr &&
            tcb->priority > current_task_->priority) {
            task_yield();
        }

        return Task(tcb);
    }

    [[noreturn]] void start() noexcept;

    void delay_ms(TickType ms) noexcept;
    void tick() noexcept;

    [[nodiscard]] internal::TaskControlBlock* get_current_task() noexcept { return current_task_; }
    void set_current_task(internal::TaskControlBlock* task) noexcept { current_task_ = task; }
    [[nodiscard]] internal::ReadyManager& get_ready_manager() noexcept { return ready_mgr_; }

    void suspend_task(internal::TaskControlBlock* task) noexcept;
    void resume_task(internal::TaskControlBlock* task) noexcept;

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