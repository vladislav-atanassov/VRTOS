# VRTOS - Educational Real-Time Operating System

A modular, educational Real-Time Operating System (RTOS) implementation for the STM32F446RE Nucleo board, built from scratch with pluggable scheduler architecture.

## Project Overview

VRTOS is a lightweight RTOS designed for learning and experimentation. It features a clean, modular architecture with interchangeable scheduling policies and comprehensive debugging capabilities.

### Key Features

- **Modular Scheduler Architecture** - Pluggable scheduler implementations via vtable interface
- **Multiple Scheduling Policies**:
  - Preemptive Static Priority (default)
  - Cooperative Round-Robin
- **Task Management** - Dynamic task creation with configurable priorities and stack sizes
- **Timing Services** - System tick with 1ms resolution and delay functions
- **Cortex-M4 Optimization** - Efficient context switching and interrupt handling
- **Static Memory Management** - Predictable memory allocation with stack overflow detection
- **Comprehensive Logging** - Multi-level debug output via UART

## Architecture

### Layered Design

```
┌─────────────────────────────────────┐
│      Application Layer              │
│      (User Tasks)                   │
├─────────────────────────────────────┤
│      RTOS API Layer                 │
│      (Public Interface)             │
├─────────────────────────────────────┤
│      Scheduler Manager              │
│      (Vtable Interface)             │
├─────────────┬───────────────────────┤
│ Preemptive  │  Cooperative  │ ...   │
│ Scheduler   │  Scheduler    │       │
├─────────────┴───────────────────────┤
│      Kernel Core                    │
│      (Context Switch, Tick)         │
├─────────────────────────────────────┤
│      Porting Layer                  │
│      (Cortex-M4)                    │
├─────────────────────────────────────┤
│      Hardware Abstraction           │
│      (STM32F446RE HAL)              │
└─────────────────────────────────────┘
```

### Scheduler Architecture

The scheduler system uses a vtable-based design allowing runtime scheduler selection:

```c
struct rtos_scheduler {
    // Core scheduling operations
    rtos_status_t (*init)(rtos_scheduler_instance_t *instance);
    rtos_task_handle_t (*get_next_task)(rtos_scheduler_instance_t *instance);
    bool (*should_preempt)(rtos_scheduler_instance_t *instance, rtos_task_handle_t new_task);
    void (*task_completed)(rtos_scheduler_instance_t *instance, rtos_task_handle_t completed_task);
    
    // List management operations
    void (*add_to_ready_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle);
    void (*remove_from_ready_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle);
    void (*add_to_delayed_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle, rtos_tick_t delay_ticks);
    void (*remove_from_delayed_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle);
    void (*update_delayed_tasks)(rtos_scheduler_instance_t *instance);
    
    // Optional statistics
    size_t (*get_statistics)(rtos_scheduler_instance_t *instance, void *stats_buffer, size_t buffer_size);
};
```

## Scheduling Policies

### Preemptive Static Priority (Default)

- **Algorithm**: Highest priority task always runs
- **Preemption**: Immediate when higher priority task becomes ready
- **Data Structure**: Per-priority ready lists with bitmask for O(1) lookup
- **Use Case**: Hard real-time systems requiring deterministic behavior

**Key Characteristics**:
- Priority-based preemption
- FIFO ordering within same priority level
- Time-sorted delayed list for efficient timeout management
- Bitmask optimization for fast highest-priority search

### Cooperative (Yield-Based Round-Robin)

- **Algorithm**: FIFO queue with round-robin on yield
- **Preemption**: None - tasks must explicitly yield
- **Data Structure**: Single FIFO ready list
- **Use Case**: Simple applications, reduced context switch overhead

**Key Characteristics**:
- Non-preemptive execution
- Tasks run until voluntary yield (`rtos_yield()` or delay)
- Yielding tasks move to end of queue (round-robin behavior)
- Lower interrupt overhead
- No time-slicing - task scheduling is purely voluntary
