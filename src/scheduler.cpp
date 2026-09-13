#include "scheduler.hpp"
#include "port.hpp"

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

void idle_task() {
    while (true) {
        asm volatile("wfi");
    }
}

uint32_t* init_task_stack(uint32_t* stack_top, void (*task_func)()) {
    uint32_t* sp = stack_top;

    *(--sp) = 0x01000000;                                // xPSR (Thumb bit)
    *(--sp) = reinterpret_cast<uint32_t>(task_func) | 1; // PC
    *(--sp) = 0xFFFFFFFD;                                // LR (Return to Thread mode)
    *(--sp) = 0;                                         // R12
    *(--sp) = 0;                                         // R3
    *(--sp) = 0;                                         // R2
    *(--sp) = 0;                                         // R1
    *(--sp) = 0;                                         // R0

    // Registers R4-R11
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;

    return sp;
}

detail::TaskControlBlock* Scheduler::create_task_impl(void (*task_func)(), uint8_t priority, bool reserved) noexcept {
    ACRTOS_ASSERT(priority < kMaxPriorities);
    if (priority >= kMaxPriorities) return nullptr;

    port::CriticalSection guard;

    const size_t capacity = reserved ? kMaxTasks : (kMaxTasks - 1);
    if (task_count_ >= capacity) return nullptr;

    auto& allocate_tcb = task_table_[task_count_];
    uint32_t* stack_top = &allocate_tcb.stack[kStackSize];

    allocate_tcb.sp = init_task_stack(stack_top, task_func);
    allocate_tcb.state = TaskState::Ready;
    allocate_tcb.priority = priority;

    ready_mgr_.add(&allocate_tcb);
    task_count_++;
    return &allocate_tcb;
}

Task Scheduler::create_task(void (*task_func)(), uint8_t priority) noexcept {
    detail::TaskControlBlock* new_tcb = create_task_impl(task_func, priority, false);
    return Task(new_tcb);
}

void Scheduler::delay_ms(TickType ms) noexcept {
    if (ms == 0 || current_task_ == nullptr) return;

    {
        port::CriticalSection guard;
        current_task_->state = TaskState::Blocked;
        delay_list_.insert(current_task_, ms);
    }

    task_yield();
}

void Scheduler::tick() noexcept {
    port::CriticalSection guard;
    delay_list_.tick(ready_mgr_); // O(1)
}

void Scheduler::start() noexcept {
    ACRTOS_ASSERT(!started_ && "Scheduler::start() called twice");
    if (started_) return;
    started_ = true;

    create_task_impl(idle_task, 0, true);

    port::start_hardware_and_yield();

    while (true) {
        asm volatile("wfi");
    }
}

void Scheduler::suspend_task(detail::TaskControlBlock* task) noexcept {
    if (!task || task->state == TaskState::Suspended) return;

    port::CriticalSection guard;

    if (task->state == TaskState::Ready) {
        ready_mgr_.remove(task);
    } else if (task->state == TaskState::Blocked) {
        delay_list_.remove(task);
    }

    const bool is_current = (task == current_task_);
    task->state = TaskState::Suspended;

    if (is_current && started_) {
        task_yield();
    }
}

void Scheduler::resume_task(detail::TaskControlBlock* task) noexcept {
    if (!task || task->state != TaskState::Suspended) return;

    port::CriticalSection guard;

    task->state = TaskState::Ready;
    ready_mgr_.add(task);

    if (started_ && current_task_ && task->priority > current_task_->priority) {
        task_yield();
    }
}

} // namespace acrtos

namespace acrtos::detail {

void ReadyManager::add(TaskControlBlock* task) noexcept {
    if (!task) return;
    const uint8_t prio = task->priority;

    port::CriticalSection guard;
    ready_lists_[prio].push_back(task);
    ready_bitmask_ |= (1U << prio);
}

void ReadyManager::remove(TaskControlBlock* task) noexcept {
    if (!task) return;
    const uint8_t prio = task->priority;

    port::CriticalSection guard;
    ready_lists_[prio].remove(task);

    if (ready_lists_[prio].is_empty()) {
        ready_bitmask_ &= ~(1U << prio);
    }
}

[[nodiscard]] TaskControlBlock* ReadyManager::get_highest_priority_task() noexcept {
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

} // namespace acrtos::detail