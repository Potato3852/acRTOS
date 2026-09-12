#pragma once
#include "acrtos/tcb.hpp"

namespace acrtos {

class Scheduler {
public:
    static Scheduler& instance() {
        static Scheduler instance;
        return instance;
    }

    bool create_task(void (*task_func)());
    void start();
    void delay_ms(TickType ms);
    void tick();

    TaskControlBlock& get_task(size_t index) { return task_table_[index]; }
    size_t get_task_count() const { return task_count_; }
    uint8_t get_current_index() const { return current_task_index_; }
    void set_current_index(uint8_t idx) { current_task_index_ = idx; }

private:
    Scheduler() = default;

    TaskControlBlock task_table_[kMaxTasks];
    uint8_t task_count_{0};
    volatile uint8_t current_task_index_{0};
};

} // namespace acrtos