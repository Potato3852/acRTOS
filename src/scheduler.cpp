#include "acrtos/scheduler.hpp"
#include "acrtos/port.hpp"

extern "C" {
    uint32_t* current_sp = nullptr;

    void schedule_next_task() {
        auto& sched = acrtos::Scheduler::instance();
        if (sched.get_task_count() == 0) return;

        sched.get_task(sched.get_current_index()).sp = current_sp;

        uint8_t next_task = sched.get_current_index();
        bool found = false;

        for (size_t i = 0; i < sched.get_task_count(); ++i) {
            next_task = (next_task + 1) % sched.get_task_count();
            if (next_task != 0 && sched.get_task(next_task).state == acrtos::TaskState::Ready) {
                sched.set_current_index(next_task);
                found = true;
                break;
            }
        }

        if (!found) {
            sched.set_current_index(0);
        }

        current_sp = sched.get_task(sched.get_current_index()).sp;
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

    *(--sp) = 0x01000000;
    *(--sp) = reinterpret_cast<uint32_t>(task_func) | 1;
    *(--sp) = 0xFFFFFFFD;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;

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

bool Scheduler::create_task(void (*task_func)()) {
    if (task_count_ >= kMaxTasks) return false;

    auto& task = task_table_[task_count_];
    uint32_t* stack_top = &task.stack[kStackSize];
    task.sp = init_task_stack(stack_top, task_func);
    task.state = TaskState::Ready;

    task_count_++;
    return true;
}

void Scheduler::delay_ms(TickType ms) {
    if (ms == 0) return;
    task_table_[current_task_index_].delay_ticks = ms;
    task_table_[current_task_index_].state = TaskState::Blocked;
    task_yield();
}

void Scheduler::tick() {
    for (size_t i = 0; i < task_count_; ++i) {
        if (task_table_[i].state == TaskState::Blocked) {
            if (task_table_[i].delay_ticks > 0) {
                task_table_[i].delay_ticks--;
            }
            if (task_table_[i].delay_ticks == 0) {
                task_table_[i].state = TaskState::Ready;
            }
        }
    }
}

void Scheduler::start() {
    create_task(idle_task);
}

} // namespace acrtos