#include "acrtos/queue.hpp"
#include "acrtos/scheduler.hpp"
#include "acrtos/port.hpp"

#include <cstring>

namespace acrtos::internal {

QueueCore::QueueCore(std::byte* storage, std::size_t item_size, std::size_t capacity) noexcept
    : storage_(storage), item_size_(item_size), capacity_(capacity) {
    ACRTOS_ASSERT(storage != nullptr && item_size > 0 && capacity > 0);
}

QueueCore::~QueueCore() {
    ACRTOS_ASSERT(senders_.is_empty() && receivers_.is_empty() && "queue destroyed while tasks wait on it");
}

std::size_t QueueCore::count() const noexcept {
    port::CriticalSection guard;
    return count_;
}

void QueueCore::push_raw(const void* item) noexcept {
    std::memcpy(storage_ + item_size_ * tail_, item, item_size_);
    if (++tail_ == capacity_) { 
        tail_ = 0;
    }
    ++count_;
}

void QueueCore::pop_raw(void* out) noexcept {
    std::memcpy(out, storage_ + item_size_ * head_, item_size_);
    if (++head_ == capacity_) {
        head_ = 0;
    }
    --count_;
}

bool QueueCore::try_send_locked(const void* item, bool& should_yield) noexcept {
    if (TaskControlBlock* receiver = receivers_.wake_highest()) {
        ACRTOS_ASSERT(count_ == 0);
        std::memcpy(receiver->xfer_ptr, item, item_size_);
        should_yield = Scheduler::instance().make_task_ready(receiver);
        return true;
    }
    if (count_ < capacity_) {
        push_raw(item);
        return true;
    }
    return false;
}

bool QueueCore::try_receive_locked(void* out, bool& should_yield) noexcept {
    if (count_ == 0) {
        return false;
    }
    pop_raw(out);
    if (TaskControlBlock* sender = senders_.wake_highest()) {
        push_raw(sender->xfer_ptr);
        should_yield = Scheduler::instance().make_task_ready(sender);
    }
    return true;
}

TaskControlBlock* QueueCore::block_current_locked(WaitQueue& queue, void* xfer, TickType timeout_ms) noexcept {
    ACRTOS_ASSERT(!port::in_isr() && "blocking queue call from ISR");

    auto& sched = Scheduler::instance();
    TaskControlBlock* task = sched.get_current_task();
    ACRTOS_ASSERT(task != nullptr && "blocking queue call before Scheduler::start()");
    if (task == nullptr) {
        return nullptr;
    }

    task->xfer_ptr = xfer;
    task->timeout_expired = false;
    queue.park(task);
    if (timeout_ms != kWaitForever) {
        sched.start_timeout_for_current_task(ms_to_ticks(timeout_ms));
    }
    return task;
}

bool QueueCore::finish_call(TaskControlBlock* blocked, bool should_yield) noexcept {
    if (blocked == nullptr) {
        if (should_yield) task_yield();
        return true;
    }
    task_yield();
    return !blocked->timeout_expired;
}

bool QueueCore::send(const void* item, TickType timeout_ms) noexcept {
    bool should_yield = false;
    TaskControlBlock* blocked = nullptr;
    {
        port::CriticalSection guard;
        if (!try_send_locked(item, should_yield)) {
            if (timeout_ms == 0) return false;
            blocked = block_current_locked(senders_, const_cast<void*>(item), timeout_ms);
            if (blocked == nullptr) return false;
        }
    }
    return finish_call(blocked, should_yield);
}

bool QueueCore::receive(void* out, TickType timeout_ms) noexcept {
    bool should_yield = false;
    TaskControlBlock* blocked = nullptr;
    {
        port::CriticalSection guard;
        if (!try_receive_locked(out, should_yield)) {
            if (timeout_ms == 0) return false;
            blocked = block_current_locked(receivers_, out, timeout_ms);
            if (blocked == nullptr) return false;
        }
    }
    return finish_call(blocked, should_yield);
}

bool QueueCore::send_from_isr(const void* item, bool& should_yield) noexcept {
    should_yield = false;
    port::CriticalSection guard;
    return try_send_locked(item, should_yield);
}

bool QueueCore::receive_from_isr(void* out, bool& should_yield) noexcept {
    should_yield = false;
    port::CriticalSection guard;
    return try_receive_locked(out, should_yield);
}

} // namespace acrtos::internal
