/**
 * @file task.hpp
 * @brief TCB, ready/wait lists, delay list, and the user-facing Task handle.
 */
#pragma once
#include "types.hpp"

namespace acrtos {
class Scheduler;
class Mutex;

namespace internal {
class ReadyManager;
class TaskList;

/**
 * @brief Per-task kernel bookkeeping. Lives in Scheduler::task_table_.
 */
struct TaskControlBlock {
    uint32_t* sp{nullptr};
    TaskState state{TaskState::Ready};
    TickType delay_ticks{0};

    uint32_t* stack_base{nullptr};
    uint32_t* stack_end{nullptr};

    uint8_t priority{0};
    uint8_t base_priority{0};

    TaskControlBlock* prev{nullptr};
    TaskControlBlock* next{nullptr};
    TaskControlBlock* delay_prev{nullptr};
    TaskControlBlock* delay_next{nullptr};

    Mutex* held_mutexes{nullptr};          ///< Head of the intrusive list of mutexes owned by this task.

    TaskList* wait_list{nullptr};          ///< Wait list the task is parked on, or nullptr.
    bool in_delay_list{false};             ///< True while linked into the DelayList.
    bool timeout_expired{false};           ///< Set when a wait ended by timeout, suspend or delete.
    void* xfer_ptr{nullptr};               ///< Queue: buffer of the blocked task. EventGroup: where to store the matched bits.

    uint32_t event_wait_mask{0};           ///< EventGroup wait parameters of a parked waiter.
    bool event_wait_all{false};
    bool event_clear_on_exit{false};
};

/**
 * @brief Intrusive doubly linked list of TCBs, linked through TaskControlBlock::prev/next.
 * @details A task can be in at most one TaskList at a time. Not thread-safe:
 *          callers must hold a critical section.
 */
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

    /** @brief Remove and return the head, or nullptr if the list is empty. */
    TaskControlBlock* pop_front();

    /** @brief Unlink @p task, which must currently be in this list. */
    void remove(TaskControlBlock* task);

    [[nodiscard]] bool is_empty() const { return head == nullptr; }
    [[nodiscard]] TaskControlBlock* peek_front() const {return head; }
};

/**
 * @brief Delta list of sleeping tasks, ordered by wake-up time.
 * @details Each node stores the ticks remaining after its predecessor expires, so a
 *          tick only decrements the head; work per tick is O(k) for k tasks that
 *          expire on that tick. Not thread-safe: callers must hold a critical section.
 */
class DelayList {
private:
    TaskControlBlock* head{nullptr};

public:
    DelayList() = default;

    DelayList(const DelayList&) = delete;
    DelayList& operator=(const DelayList&) = delete;

    /** @brief Insert @p task to wake up after @p ticks (must be > 0). */
    void insert(TaskControlBlock* task, TickType ticks) noexcept;

    /** @brief Remove @p task if it is in the list (no-op otherwise). */
    void remove(TaskControlBlock* task) noexcept;

    /**
     * @brief Advance time by one tick and move expired tasks to the ready lists.
     * @details A task that was also parked on a wait list is unlinked from it and
     *          gets TaskControlBlock::timeout_expired set.
     */
    void tick(ReadyManager& ready_mgr) noexcept;
};

} // namespace internal

/**
 * @brief Copyable handle to a task in the scheduler's TCB table.
 * @details A Task does not own the underlying task: destroying the handle does not
 *          delete the task. A default-constructed handle is invalid.
 */
class Task {
private:
    internal::TaskControlBlock* tcb_{nullptr};

public:
    Task() = default;
    explicit Task(internal::TaskControlBlock* tcb) noexcept : tcb_(tcb) {}

    /** @brief Take the task out of scheduling until resume(). */
    void suspend() noexcept;

    /** @brief Make a suspended task ready again. No effect on tasks in other states. */
    void resume() noexcept;

    /** @brief Delete the task. Its TCB slot and stack are not reclaimed. */
    void terminate() noexcept;

    /** @brief Current state; an invalid handle reports TaskState::Suspended. */
    [[nodiscard]] TaskState get_state() const noexcept { return tcb_ ? tcb_->state : TaskState::Suspended; }

    /** @brief Effective priority (including inheritance); 0 for an invalid handle. */
    [[nodiscard]] uint8_t get_priority() const noexcept { return tcb_ ? tcb_->priority : 0; }

    /** @brief False for handles returned by a failed create_task(). */
    [[nodiscard]] bool is_valid() const noexcept { return tcb_ != nullptr; }
};

static_assert(sizeof(Task) == sizeof(void*), "Task facade must be a single pointer");

} // namespace acrtos