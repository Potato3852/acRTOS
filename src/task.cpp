#include "acrtos/task.hpp"
#include "acrtos/scheduler.hpp"

namespace acrtos {

void Task::suspend() noexcept {
    if (tcb_) {
        Scheduler::instance().suspend_task(tcb_);
    }
}

void Task::resume() noexcept {
    if (tcb_) {
        Scheduler::instance().resume_task(tcb_);
    }
}

void Task::terminate() noexcept {
    if (tcb_) {
        Scheduler::instance().delete_task(tcb_);
    }
}

namespace internal {

void TaskList::push_back(TaskControlBlock* task) {
    ACRTOS_ASSERT(task != nullptr);

    task->next = nullptr;
    if (tail == nullptr) {
        task->prev = nullptr;
        head = task;
        tail = task;
    } else {
        task->prev = tail;
        tail->next = task;
        tail = task;
    }
}

void TaskList::insert_by_priority(TaskControlBlock* task) {
    ACRTOS_ASSERT(task != nullptr);

    if (head == nullptr) {
        push_back(task);
        return;
    }

    TaskControlBlock* current = head;
    while (current != nullptr && current->priority >= task->priority) {
        current = current->next;
    }

    if (current == nullptr) {
        push_back(task);
        return;
    }

    task->next = current;
    task->prev = current->prev;
    if (current->prev != nullptr) {
        current->prev->next = task;
    } else {
        head = task;
    }
    current->prev = task;
}

TaskControlBlock* TaskList::pop_front() {
    if (head == nullptr) {
        return nullptr;
    }

    TaskControlBlock* task = head;
    head = head->next;

    if (head != nullptr) {
        head->prev = nullptr;
    } else {
        tail = nullptr;
    }

    task->next = nullptr;
    task->prev = nullptr;
    return task;
}

void TaskList::remove(TaskControlBlock* task) {
    if (!task) {
        return;
    }

    if (task->prev != nullptr) {
        task->prev->next = task->next;
    } else {
        head = task->next;
    }

    if (task->next != nullptr) {
        task->next->prev = task->prev;
    } else {
        tail = task->prev;
    }

    task->next = nullptr;
    task->prev = nullptr;
}

void DelayList::insert(TaskControlBlock* task, TickType ticks) noexcept {
    ACRTOS_ASSERT(task != nullptr);
    ACRTOS_ASSERT(!task->in_delay_list);

    TaskControlBlock* prev = nullptr;
    TaskControlBlock* current = head;

    while (current != nullptr && ticks >= current->delay_ticks) {
        ticks -= current->delay_ticks;
        prev = current;
        current = current->delay_next;
    }

    task->delay_ticks = ticks;
    task->delay_prev = prev;
    task->delay_next = current;

    if (prev != nullptr) prev->delay_next = task; else head = task;
    if (current != nullptr) {
        current->delay_prev = task;
        current->delay_ticks -= ticks;
    }
    task->in_delay_list = true;
}

void DelayList::remove(TaskControlBlock* task) noexcept {
    if (task == nullptr || !task->in_delay_list) {
        return;
    }

    if (task->delay_next != nullptr) {
        task->delay_next->delay_ticks += task->delay_ticks;
        task->delay_next->delay_prev = task->delay_prev;
    }
    if (task->delay_prev != nullptr) {
        task->delay_prev->delay_next = task->delay_next;
    } else {
        head = task->delay_next;
    }

    task->delay_next = nullptr;
    task->delay_prev = nullptr;
    task->delay_ticks = 0;
    task->in_delay_list = false;
}

void DelayList::tick(ReadyManager& ready_mgr) noexcept {
    if (head == nullptr) return;

    if (head->delay_ticks > 0) {
        head->delay_ticks -= 1;
    }

    while (head != nullptr && head->delay_ticks == 0) {
        TaskControlBlock* t = head;
        head = t->delay_next;
        if (head != nullptr) head->delay_prev = nullptr;

        t->delay_next = nullptr;
        t->delay_prev = nullptr;
        t->in_delay_list = false;

        if (t->wait_list != nullptr) {
            t->wait_list->remove(t);
            t->wait_list = nullptr;
            t->timeout_expired = true;
        }

        t->state = TaskState::Ready;
        ready_mgr.add(t);
    }
}

} // namespace internal

} // namespace acrtos