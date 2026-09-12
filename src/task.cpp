#include "task.hpp"

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

} // namespace acrtos::detail

} // namespace acrctos