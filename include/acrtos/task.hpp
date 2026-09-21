/**
 * @file task.hpp
 * @brief TCB, ready/wait lists, delay list, and the user-facing Task handle.
 */
#pragma once
#include "types.hpp"

namespace acrtos {
class Scheduler;

namespace internal {
class ReadyManager;
class TaskList;

struct TaskControlBlock {
    uint32_t* sp{nullptr};
    TaskState state{TaskState::Ready};
    TickType delay_ticks{0};
    uint32_t stack[config::kStackSize]{0};

    uint8_t priority{0};
    uint8_t base_priority{0};

    TaskControlBlock* prev{nullptr};
    TaskControlBlock* next{nullptr};
    TaskControlBlock* delay_prev{nullptr};
    TaskControlBlock* delay_next{nullptr};

    TaskList* wait_list{nullptr};
    bool in_delay_list{false};
    bool timeout_expired{false};
    void* xfer_ptr{nullptr};
};

class TaskList {
private:
    TaskControlBlock* head{nullptr};
    TaskControlBlock* tail{nullptr};

public:
    TaskList() = default;

    TaskList(const TaskList&) = delete;
    TaskList& operator=(const TaskList&) = delete;

    void push_back(TaskControlBlock* task);
    /**
     * @brief Insert so higher priority is closer to the head.
     * Equal priorities stay FIFO (new task goes after existing equals).
     */
    void insert_by_priority(TaskControlBlock* task);
    TaskControlBlock* pop_front();
    void remove(TaskControlBlock* task);
    [[nodiscard]] bool is_empty() const { return head == nullptr; }
};

/**
 * @brief Delta-list of sleeping tasks. Tick interrupt only looks at the head, so
 * waking expired tasks is O(k) in the number of tasks that expire this tick.
 */
class DelayList {
private:
    TaskControlBlock* head{nullptr};

public:
    DelayList() = default;

    DelayList(const DelayList&) = delete;
    DelayList& operator=(const DelayList&) = delete;

    void insert(TaskControlBlock* task, TickType ticks) noexcept;
    void remove(TaskControlBlock* task) noexcept;
    void tick(ReadyManager& ready_mgr) noexcept;
};

} // namespace internal

/**
 * Copyable handle to a TCB in the scheduler pool. Does not own the task:
 * destroying a Task object does not delete the thread.
 */
class Task {
private:
    internal::TaskControlBlock* tcb_{nullptr};

public:
    Task() = default;
    explicit Task(internal::TaskControlBlock* tcb) noexcept : tcb_(tcb) {}

    void suspend() noexcept;
    void resume() noexcept;

    [[nodiscard]] TaskState get_state() const noexcept { return tcb_ ? tcb_->state : TaskState::Suspended; }
    [[nodiscard]] uint8_t get_priority() const noexcept { return tcb_ ? tcb_->priority : 0; }
    [[nodiscard]] bool is_valid() const noexcept { return tcb_ != nullptr; }
};

static_assert(sizeof(Task) == sizeof(void*), "Task facade must be a single pointer");

} // namespace acrtos