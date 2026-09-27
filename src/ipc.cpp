#include "acrtos/ipc.hpp"
#include "acrtos/scheduler.hpp"
#include "acrtos/port.hpp"

#include <cstdint>

namespace acrtos::internal {

void WaitQueue::park(TaskControlBlock* task) noexcept {
    ACRTOS_ASSERT(task != nullptr);
    ACRTOS_ASSERT(task->wait_list == nullptr && "task already parked on a wait list");

    task->state = TaskState::Blocked;
    task->wait_list = &wait_list_;
    wait_list_.insert_by_priority(task);
}

TaskControlBlock* WaitQueue::wake_highest() noexcept {
    TaskControlBlock* task = wait_list_.pop_front();
    if (task != nullptr) {
        task->wait_list = nullptr;
        Scheduler::instance().cancel_timeout(task);
    }
    return task;
}

} // namespace acrtos::internal

namespace acrtos {

bool Semaphore::take(TickType timeout_ms) noexcept {
    if (timeout_ms == 0) return try_take();
    ACRTOS_ASSERT(!port::in_isr() && "blocking take() from ISR");

    auto& sched = Scheduler::instance();
    internal::TaskControlBlock* task = nullptr;
    {
        port::CriticalSection guard;

        if (count_ > 0) {
            --count_;
            return true;
        }

        task = sched.get_current_task();
        ACRTOS_ASSERT(task != nullptr && "take() before Scheduler::start()");
        if (task == nullptr) return false;

        task->timeout_expired = false;
        wait_.park(task);
        if (timeout_ms != kWaitForever) {
            sched.start_timeout_for_current_task(ms_to_ticks(timeout_ms));
        }
    }

    task_yield();
    return !task->timeout_expired;
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
    bool should_preempt = false;

    {
        port::CriticalSection guard;
        internal::TaskControlBlock* woken = wait_.wake_highest();
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

bool Semaphore::try_take_from_isr() noexcept {
    // In acRTOS the call is safe from ISR
    // So just reuse base method
    return try_take();
}

bool Semaphore::give_from_isr() noexcept {
    bool should_preempt = false;

    {
        port::CriticalSection guard;
        internal::TaskControlBlock* woken = wait_.wake_highest();
        
        if (woken != nullptr) {
            should_preempt = Scheduler::instance().make_task_ready(woken);
        } else if (count_ < UINT32_MAX) {
            ++count_;
        }
    }

    // Do NOT call task_yield() here
    return should_preempt;
}

bool Mutex::lock() noexcept {
    ACRTOS_ASSERT(!port::in_isr() && "Mutex::lock() from ISR");

    internal::TaskControlBlock* current = nullptr;
    {
        port::CriticalSection guard;
        current = Scheduler::instance().get_current_task();
        ACRTOS_ASSERT(current != nullptr && "lock() before Scheduler::start()");
        if (current == nullptr) return false;

        if (owner_ == nullptr) {
            owner_ = current;
            return true;
        }
        if (owner_ == current) {
            ACRTOS_ASSERT(false && "task tried to lock its own mutex");
            return false;
        }

        if (current->priority > owner_->priority) {
            Scheduler::instance().set_task_priority(owner_, current->priority);
        }

        current->timeout_expired = false;
        wait_.park(current);
    }

    task_yield();
    return !current->timeout_expired;
}

bool Mutex::unlock() noexcept {
    bool should_preempt = false;
    {
        port::CriticalSection guard;
        auto* current = Scheduler::instance().get_current_task();

        if (current != owner_) {
            ACRTOS_ASSERT(false && "unlock() by a task that is not the owner");
            return false;
        }

        if (current->priority != current->base_priority) {
            Scheduler::instance().set_task_priority(current, current->base_priority);
            should_preempt = true;
        }

        if (auto* next = wait_.wake_highest()) {
            owner_ = next;
            should_preempt |= Scheduler::instance().make_task_ready(next);
        } else {
            owner_ = nullptr;
        }
    }

    if (should_preempt) task_yield();
    return true;
}

bool EventGroup::scan_and_wake_locked() noexcept {
    bool should_yield = false;
    internal::TaskControlBlock* cur = waiters_.peek_front();

    while (cur != nullptr) {
        internal::TaskControlBlock* next = cur->next;

        if (condition_met(cur, current_bits_)) {
            waiters_.remove(cur);
            Scheduler::instance().cancel_timeout(cur);

            const uint32_t satisfied_bits = current_bits_;

            if (cur->event_clear_on_exit) {
                current_bits_ &= ~cur->event_wait_mask;
            }
            if (cur->xfer_ptr != nullptr) {
                *static_cast<uint32_t*>(cur->xfer_ptr) = satisfied_bits;
            }

            should_yield |= Scheduler::instance().make_task_ready(cur);
        }

        cur = next;
    }
    return should_yield;
}

[[nodiscard]] uint32_t EventGroup::wait_bits(uint32_t mask, bool wait_all, bool clear_on_exit, TickType timeout_ms) noexcept {
    if (mask == 0) return get_bits();

    uint32_t return_bits = 0;
    auto* current_task = Scheduler::instance().get_current_task();
    ACRTOS_ASSERT(current_task != nullptr && "wait_bits() before Scheduler::start()");
    ACRTOS_ASSERT(!port::in_isr() && "blocking wait_bits() from ISR");
    if (current_task == nullptr) return current_bits_;

    {
        port::CriticalSection guard;

        bool match = bits_satisfy(current_bits_, mask, wait_all);
        
        if (match) {
            return_bits = current_bits_;
            if (clear_on_exit) {
                current_bits_ &= ~mask;
            }
            return return_bits;
        }

        if (timeout_ms == 0) { return current_bits_; }

        current_task->event_wait_mask = mask;
        current_task->event_wait_all = wait_all;
        current_task->event_clear_on_exit = clear_on_exit;
        current_task->xfer_ptr = &return_bits; 
        current_task->timeout_expired = false;
        
        ACRTOS_ASSERT(current_task->wait_list == nullptr && "task already parked on a wait list");
        current_task->state = TaskState::Blocked;
        current_task->wait_list = &waiters_;
        waiters_.insert_by_priority(current_task);

        if (timeout_ms != kWaitForever) {
            Scheduler::instance().start_timeout_for_current_task(ms_to_ticks(timeout_ms));
        }
    }

    task_yield();

    if (current_task->timeout_expired) {
        port::CriticalSection guard;
        return_bits = current_bits_;
    }
    return return_bits;
}

uint32_t EventGroup::set_bits(uint32_t mask) noexcept {
    bool should_yield = false;
    uint32_t return_bits = 0;

    {
        port::CriticalSection guard;
        current_bits_ |= mask;
        should_yield = scan_and_wake_locked();
        return_bits = current_bits_;
    }

    if (should_yield) {
        task_yield();
    }

    return return_bits;
}

uint32_t EventGroup::set_bits_from_isr(uint32_t mask, bool& should_yield) noexcept {
    should_yield = false;
    port::CriticalSection guard;
    current_bits_ |= mask;
    
    if (scan_and_wake_locked()) {
        should_yield = true;
    }
    
    return current_bits_;
}

uint32_t EventGroup::clear_bits(uint32_t mask) noexcept {
    port::CriticalSection guard;
    current_bits_ &= ~mask;
    return current_bits_;
}

uint32_t EventGroup::get_bits() const noexcept {
    port::CriticalSection guard;
    return current_bits_;
}

} // namespace acrtos