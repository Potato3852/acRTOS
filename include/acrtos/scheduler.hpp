#pragma once
#include "task.hpp"

namespace acrtos::detail {

constexpr size_t kMaxPriorities = 32;

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
    static Scheduler& instance() noexcept {
        static Scheduler instance;
        return instance;
    }

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    Task create_task(void (*task_func)(), uint8_t priority = 1) noexcept;
    void start() noexcept;
    void delay_ms(TickType ms) noexcept;
    void tick() noexcept;

    [[nodiscard]] detail::TaskControlBlock* get_current_task() noexcept { return current_task_; }
    void set_current_task(detail::TaskControlBlock* task) noexcept { current_task_ = task; }
    [[nodiscard]] detail::ReadyManager& get_ready_manager() noexcept { return ready_mgr_; }

    void suspend_task(detail::TaskControlBlock* task) noexcept;
    void resume_task(detail::TaskControlBlock* task) noexcept;
};

} // namespace acrtos