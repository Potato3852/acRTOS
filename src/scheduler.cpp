#include "acrtos/scheduler.hpp"
#include "acrtos/port.hpp"

extern "C" {
    uint32_t* current_sp = nullptr;

    void schedule_next_task() {
        auto& sched = acrtos::Scheduler::instance();
        auto& ready_mgr = sched.get_ready_manager();
        auto* prev_task = sched.get_current_task();

        if (prev_task != nullptr) {
            prev_task->sp = current_sp;
            if (prev_task->state == acrtos::TaskState::Running) {
                prev_task->state = acrtos::TaskState::Ready;
                ready_mgr.add(prev_task);
            }
        }

        auto* next_task = ready_mgr.get_highest_priority_task();
        ACRTOS_ASSERT(next_task != nullptr && "ready queue empty (idle task missing?)");

        if (next_task != nullptr) {
            next_task->state = acrtos::TaskState::Running;
            sched.set_current_task(next_task);
            current_sp = next_task->sp;
        }
    }

    void rtos_tick_handler() {
        acrtos::Scheduler::instance().tick();
    }
}

namespace acrtos {

internal::TaskControlBlock* Scheduler::allocate_tcb() noexcept {
    port::CriticalSection guard;
    if (task_count_ >= config::kMaxTasks) {
        return nullptr;
    }

    auto& tcb = task_table_[task_count_++];
    tcb.state = TaskState::Suspended;
    tcb.wait_list = nullptr;
    tcb.prev = nullptr;
    tcb.next = nullptr;
    return &tcb;
}

void Scheduler::add_to_ready_queue(internal::TaskControlBlock* tcb) noexcept {
    port::CriticalSection guard;
    tcb->state = TaskState::Ready;
    ready_mgr_.add(tcb);
}

void Scheduler::delay_ms(TickType ms) noexcept {
    const TickType ticks = ms_to_ticks(ms);
    if (ticks == 0 || current_task_ == nullptr) {
        return;
    }

    {
        port::CriticalSection guard;
        current_task_->state = TaskState::Blocked;
        delay_list_.insert(current_task_, ticks);
    }

    task_yield();
}

bool Scheduler::needs_preemption() const noexcept {
    if (current_task_ == nullptr) {
        return !ready_mgr_.is_empty();
    }
    if (ready_mgr_.is_empty()) {
        return false;
    }

    const uint8_t top = ready_mgr_.peek_highest_priority();
    return top >= current_task_->priority;
}

void Scheduler::tick() noexcept {
    bool need_switch = false;
    {
        port::CriticalSection guard;
        delay_list_.tick(ready_mgr_);
        need_switch = needs_preemption();
    }

    if (need_switch) {
        task_yield();
    }
}

[[noreturn]] void Scheduler::start() noexcept {
    ACRTOS_ASSERT(!started_ && "Scheduler::start() called twice");
    if (started_) {
        while (true) {
            asm volatile("wfi");
        }
    }

    create_task([]() {
        while (true) {
            asm volatile("wfi");
        }
    }, 0);

    started_ = true;
    port::start_hardware_and_yield();
}

void Scheduler::suspend_task(internal::TaskControlBlock* task) noexcept {
    if (!task || task->state == TaskState::Suspended) {
        return;
    }

    bool yield_self = false;
    {
        port::CriticalSection guard;

        if (task->state == TaskState::Ready) {
            ready_mgr_.remove(task);
        } else if (task->state == TaskState::Blocked) {
            if (task->wait_list != nullptr) {
                task->wait_list->remove(task);
                task->wait_list = nullptr;
            } else {
                delay_list_.remove(task);
            }
        }

        yield_self = (task == current_task_) && started_;
        task->state = TaskState::Suspended;
    }

    if (yield_self) {
        task_yield();
    }
}

bool Scheduler::make_task_ready(internal::TaskControlBlock* task) noexcept {
    if (!task) {
        return false;
    }

    task->state = TaskState::Ready;
    task->wait_list = nullptr;
    ready_mgr_.add(task);

    return started_ && current_task_ != nullptr &&
           task->priority > current_task_->priority;
}

void Scheduler::resume_task(internal::TaskControlBlock* task) noexcept {
    if (!task || task->state != TaskState::Suspended) {
        return;
    }

    bool should_preempt = false;
    {
        port::CriticalSection guard;
        task->state = TaskState::Ready;
        ready_mgr_.add(task);
        should_preempt = started_ && current_task_ != nullptr &&
                         task->priority > current_task_->priority;
    }

    if (should_preempt) {
        task_yield();
    }
}

} // namespace acrtos

namespace acrtos::internal {

void ReadyManager::add(TaskControlBlock* task) noexcept {
    if (!task) {
        return;
    }

    ACRTOS_ASSERT(task->priority < config::kMaxPriorities);
    const uint8_t prio = task->priority;

    port::CriticalSection guard;
    ready_lists_[prio].push_back(task);
    ready_bitmask_ |= (1U << prio);
}

void ReadyManager::remove(TaskControlBlock* task) noexcept {
    if (!task) {
        return;
    }

    const uint8_t prio = task->priority;
    port::CriticalSection guard;
    ready_lists_[prio].remove(task);

    if (ready_lists_[prio].is_empty()) {
        ready_bitmask_ &= ~(1U << prio);
    }
}

TaskControlBlock* ReadyManager::get_highest_priority_task() noexcept {
    port::CriticalSection guard;

    if (ready_bitmask_ == 0) [[unlikely]] {
        return nullptr;
    }

    const uint8_t top_prio = static_cast<uint8_t>(31 - __builtin_clz(ready_bitmask_));
    TaskControlBlock* task = ready_lists_[top_prio].pop_front();

    if (ready_lists_[top_prio].is_empty()) {
        ready_bitmask_ &= ~(1U << top_prio);
    }

    return task;
}

uint8_t ReadyManager::peek_highest_priority() const noexcept {
    ACRTOS_ASSERT(ready_bitmask_ != 0);
    return static_cast<uint8_t>(31 - __builtin_clz(ready_bitmask_));
}

} // namespace acrtos::internal