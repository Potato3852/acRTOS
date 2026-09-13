/**
 * @file types.hpp
 * @brief Core data types and enumerations for the RTOS.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include "acRtosConfig.hpp"

namespace acrtos {

/** @brief Represents time durations and timestamps within the kernel. */
using TickType = uint32_t;

/**
 * @enum TaskState
 * @brief Represents the current lifecycle state of a task.
 */
enum class TaskState : uint8_t {
    Ready,      /**< Task is ready to run and waiting in the ReadyManager. */
    Running,    /**< Task is currently executing on the CPU. */
    Blocked,    /**< Task is sleeping (e.g., in DelayList) waiting for a timeout. */
    Suspended   /**< Task is paused indefinitely until explicitly resumed. */
};

} // namespace acrtos