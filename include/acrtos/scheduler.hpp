/**
 * @file scheduler.hpp
 * @brief Task scheduler and RTOS execution context management.
 * 
 * @details Implements a preemprive O(1) scheduler based on a priority bitmask.
 * Supports up to <kMaxPriority> priority levels and delay management via Delta List.
 * @warning Scheduler methods are not intended to be called from interrupt service routines (ISRs) unless the `_from_isr` suffix is explicitly specified.
 */

#pragma once
#include "task.hpp"
#include "acRtosConfig.hpp"
#include <concepts>
#include <utility>
#include <new>

namespace acrtos::internal {

uint32_t* init_task_stack(uint32_t* stack_top, void (*task_func)(void*), void* param);

/**
 * @class ReadyManager
 * @brief Manages the queues of tasks ready to execute.
 * @details Utilizes an array of task lists and a hardware-optimized priority 
 *          bitmask to find the highest-priority task in O(1) time.
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
    [[nodiscard]] bool is_empty() const noexcept { return ready_bitmask_ == 0; }
};

} // namespace acrtos::internal

namespace acrtos {

/**
 * @class Scheduler
 * @brief The central brain of the RTOS (Singleton).
 * @details Manages all task allocations, context switching, and timing. 
 *          Tasks are statically allocated in an internal memory pool to 
 *          avoid dynamic memory fragmentation.
 */
class Scheduler { 
private:
    Scheduler() = default;

    internal::TaskControlBlock* allocate_tcb() noexcept;

    void add_to_ready_queue(internal::TaskControlBlock* tcb) noexcept;
    
    internal::TaskControlBlock task_table_[config::kMaxTasks];
    internal::TaskControlBlock* current_task_{nullptr};
    internal::ReadyManager ready_mgr_;
    internal::DelayList delay_list_;
    uint8_t task_count_{0};
    bool started_{false};
    
public:
    /**
     * @brief Retrieves the singleton instance of the Scheduler.
     * @return Reference to the Scheduler.
     */
    static Scheduler& instance() noexcept {
        static Scheduler instance;
        return instance;
    }

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    template <typename F>
    requires std::invocable<F>
    Task create_task(F&& callable, uint8_t priority = 1) noexcept {
        internal::TaskControlBlock* tcb = allocate_tcb();
        if (!tcb) return Task{nullptr};

        using DecayedF = std::decay_t<F>;

        static_assert(sizeof(DecayedF) < (config::kStackSize * sizeof(uint32_t)) / 2,
                      "Callable object is too large for the task stack!");

        uint8_t* stack_end = reinterpret_cast<uint8_t*>(&tcb->stack[config::kStackSize]);

        stack_end -= sizeof(DecayedF);

        std::size_t align_offset = reinterpret_cast<std::uintptr_t>(stack_end) % 8;
        stack_end -= align_offset;

        DecayedF* stored_callable = new (stack_end) DecayedF(std::forward<F>(callable));
        uint32_t* hw_stack_top = reinterpret_cast<uint32_t*>(stack_end);

        auto trampoline = [](void* ctx) {
            auto* fn = static_cast<DecayedF*>(ctx);
            (*fn)();

            Scheduler::instance().suspend_task(Scheduler::instance().get_current_task());
            while(true) { asm volatile("wfi"); }
        };

        tcb->sp = internal::init_task_stack(hw_stack_top, trampoline, stored_callable);
        tcb->priority = priority;

        add_to_ready_queue(tcb);
        return Task(tcb);
    }

    /**
     * @brief Starts the RTOS scheduler and hardware timers.
     * @warning This function never returns.
     */
    void start() noexcept;

    /**
     * @brief Blocks the currently running task for a specified duration.
     * @param ms The number of milliseconds to sleep.
     */
    void delay_ms(TickType ms) noexcept;
    void tick() noexcept;

    [[nodiscard]] internal::TaskControlBlock* get_current_task() noexcept { return current_task_; }
    void set_current_task(internal::TaskControlBlock* task) noexcept { current_task_ = task; }
    [[nodiscard]] internal::ReadyManager& get_ready_manager() noexcept { return ready_mgr_; }

    void suspend_task(internal::TaskControlBlock* task) noexcept;
    void resume_task(internal::TaskControlBlock* task) noexcept;
};

} // namespace acrtos