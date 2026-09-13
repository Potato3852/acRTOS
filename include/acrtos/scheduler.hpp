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

namespace acrtos::detail {

/**
 * @class ReadyManager
 * @brief Manages the queues of tasks ready to execute.
 * @details Utilizes an array of task lists and a hardware-optimized priority 
 *          bitmask to find the highest-priority task in O(1) time.
 */
class ReadyManager {
private:
    TaskList ready_lists_[kMaxPriorities];
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

} // namespace acrtos::detail

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
    
    detail::TaskControlBlock* create_task_impl(void (*task_func)(), uint8_t priority, bool reserved) noexcept;
    
    detail::TaskControlBlock task_table_[kMaxTasks];
    detail::TaskControlBlock* current_task_{nullptr};
    detail::ReadyManager ready_mgr_;
    detail::DelayList delay_list_;
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

    /**
     * @brief Creates a new task and adds it to the Ready queue.
     * @param task_func Pointer to the task's main function.
     * @param priority Task priority (higher number = higher priority).
     * @return Task A handle to the newly created task.
     */
    Task create_task(void (*task_func)(), uint8_t priority = 1) noexcept;

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

    [[nodiscard]] detail::TaskControlBlock* get_current_task() noexcept { return current_task_; }
    void set_current_task(detail::TaskControlBlock* task) noexcept { current_task_ = task; }
    [[nodiscard]] detail::ReadyManager& get_ready_manager() noexcept { return ready_mgr_; }

    void suspend_task(detail::TaskControlBlock* task) noexcept;
    void resume_task(detail::TaskControlBlock* task) noexcept;
};

} // namespace acrtos