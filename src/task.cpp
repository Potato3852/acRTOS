#include "task.hpp"
#include "scheduler.hpp"

namespace acrtos {

namespace detail {

void TaskList::push_back(TaskControlBlock* task) {
    if (!task) return;

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

TaskControlBlock* TaskList::pop_front() {
    if (head == nullptr) return nullptr;

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
    if (!task) return;

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
    if (!task) return;

    if (head == nullptr) {
        task->delay_ticks = ticks;
        task->next = nullptr;
        task->prev = nullptr;
        head = task;
        return;
    }

    auto current = head;
    detail::TaskControlBlock* prev = nullptr;

    while (current != nullptr) {
        if (ticks >= current->delay_ticks) {
            ticks -= current->delay_ticks;
            prev = current;
            current = current->next;
        } else {
            break;
        }
    }

    task->delay_ticks = ticks;
    task->next = current;
    task->prev = prev;

    if (prev != nullptr) {
        prev->next = task;
    } else {
        head = task;
    }

    if (current != nullptr) {
        current->prev = task;
        current->delay_ticks -= ticks;
    }
}

void DelayList::tick(ReadyManager& ready_mgr) noexcept {
    if (head == nullptr) return;

    head->delay_ticks -= 1;

    while (head != nullptr and head->delay_ticks == 0) {
        auto temp = head;
        head = head->next;
        if (head != nullptr) {
            head->prev = nullptr;
        }
        temp->next = nullptr;
        temp->prev = nullptr;

        temp->state = TaskState::Ready;
        ready_mgr.add(temp);
    }
}

} // namespace acrtos::detail

} // namespace acrctos