#pragma once
#include "types.hpp"

namespace acrtos {

namespace detail {
class ReadyManager;

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
    bool is_empty() const { return head == nullptr; }
};

class DelayList {
private:
    TaskControlBlock* head{nullptr};

public:
    DelayList() = default;

    DelayList(const DelayList&) = delete;
    DelayList& operator=(const DelayList&) = delete;

    void insert(TaskControlBlock* task, TickType ticks) noexcept;
    void tick(ReadyManager& ready_mgr) noexcept;
};

} // namespace acrtos::detail

class Task {
private:
    detail::TaskControlBlock* tcb_{nullptr};
public:
    Task() = default;
    explicit Task(detail::TaskControlBlock* tcb) noexcept : tcb_(tcb) {}

    [[nodiscard]] uint8_t get_priority() const noexcept {
        return tcb_ ? tcb_->priority : 0;
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return tcb_ != nullptr;
    }
};

} // namespace acrtos