/**
 * @file task.hpp
 * @brief Task control structures and user-facing Task API.
 */

#pragma once
#include "types.hpp"

namespace acrtos {
class Scheduler;

namespace detail {
class ReadyManager;

/**
 * @struct TaskControlBlock
 * @brief Internal representation of a thread (TCB).
 * @details Maintains the stack pointer, execution state, priority, and 
 *          intrusive linked-list pointers for O(1) queue management.
 */
struct TaskControlBlock {
    uint32_t* sp{nullptr};
    TaskState state{TaskState::Ready};
    TickType delay_ticks{0};
    uint32_t stack[kStackSize]{0};

    uint8_t priority{0};
    TaskControlBlock* prev{nullptr};
    TaskControlBlock* next{nullptr};
};   

class TaskList {
private:
    TaskControlBlock* head{nullptr};
    TaskControlBlock* tail{nullptr};
public:
    explicit TaskList() = default;

    TaskList(TaskList&) = delete;
    TaskList& operator=(TaskList&) = delete;

    void push_back(TaskControlBlock* task);
    TaskControlBlock* pop_front();
    void remove(TaskControlBlock* task);
    [[nodiscard]] bool is_empty() const { return head == nullptr; }
};

/**
 * @class DelayList
 * @brief Time-based Delta List for managing blocked tasks.
 * @details Ensures O(1) time complexity during the system tick interrupt
 *          by tracking only the relative time difference (delta) between tasks.
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

} // namespace acrtos::detail

/**
 * @class Task
 * @brief A lightweight, safe handle for managing a task's lifecycle.
 * @details Acts as a user-friendly facade over the internal TaskControlBlock. 
 *          Provides methods to suspend, resume, and inspect the task.
 */
class Task {
private:
    /**
     * @brief Private TCB for secure work with Task
     */
    detail::TaskControlBlock* tcb_{nullptr};
public:
    Task() = default;
    explicit Task(detail::TaskControlBlock* tcb) noexcept : tcb_(tcb) {}

    /**
     * @brief A safe function for suspending the current task.
     */
    void suspend() noexcept;

    /**
     * @brief A safe function for resuming the current task.
     */
    void resume() noexcept;

    /**
     * @brief A method for obtaining the status of the current task.
     * @return The state of the current task from the enum class TaskState: uint8_t.
     */
    [[nodiscard]] TaskState get_state() const noexcept { return tcb_ ? tcb_->state : TaskState::Suspended; }

    /**
     * @brief A method for obtaining the priority of the current task.
     * @return uint8_t number from 0 to kMaxPriorities.
     */
    [[nodiscard]] uint8_t get_priority() const noexcept { return tcb_ ? tcb_->priority : 0; }

    /**
     * @brief A method for checking the existence of a task.
     * @return True - task is valid. False - task is broken.
     */
    [[nodiscard]] bool is_valid() const noexcept { return tcb_ != nullptr; }
};

} // namespace acrtos