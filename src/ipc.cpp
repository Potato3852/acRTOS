#include "ipc.hpp"
#include "scheduler.hpp"
#include "port.hpp"

namespace acrtos::internal {

void WaitQueue::park(TaskControlBlock* task) noexcept {
    ACRTOS_ASSERT(task != nullptr && "WaitQueue::park(nullptr)");
    ACRTOS_ASSERT(task->wait_list == nullptr && "Task already parked on a wait list");

    task->state = TaskState::Blocked;
    task->wait_list = &wait_list_;
    wait_list_.insert_by_priority(task);
}

TaskControlBlock* WaitQueue::wake_highest() noexcept {
    TaskControlBlock* task = wait_list_.pop_front();
    if (task != nullptr) {
        task->wait_list = nullptr;
    }
    return task;
}

} // namespace acrtos::internal

namespace acrtos {

void Semaphore::take() noexcept {
    bool should_block = false;

    {
        port::CriticalSection guard;
        if (count_ > 0) {
            --count_;
        } else {
            internal::TaskControlBlock* self = Scheduler::instance().get_current_task();
            ACRTOS_ASSERT(self != nullptr && "Semaphore::take() with no current task");
            wait_.park(self);
            should_block = true;
        }
    }

    if (should_block) {
        task_yield();
    }
}

bool Semaphore::try_take() noexcept {
    port::CriticalSection guard;
    if (count_ == 0) {
        return false;
    }
    --count_;
    return true;
}

void Semaphore::give() noexcept {
    internal::TaskControlBlock* woken = nullptr;
    bool should_preempt = false;

    {
        port::CriticalSection guard;
        woken = wait_.wake_highest();
        if (woken != nullptr) {
            should_preempt = Scheduler::instance().make_task_ready(woken);
        } else if (count_ < UINT32_MAX) {
            ++count_;
        }
    }

    if (should_preempt) {
        task_yield();
    }
}

} // namespace acrtos
