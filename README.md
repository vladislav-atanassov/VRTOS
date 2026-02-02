# VRTOS - Educational Real-Time Operating System

A modular, educational Real-Time Operating System (RTOS) implementation for the STM32F446RE Nucleo board, built from scratch with pluggable scheduler architecture and comprehensive synchronization primitives.

## Project Overview

VRTOS is a lightweight, feature-rich RTOS designed for learning and experimentation on ARM Cortex-M4 microcontrollers. It features a clean, modular architecture with interchangeable scheduling policies, priority inheritance, and comprehensive debugging capabilities.

### Key Features

- **Modular Scheduler Architecture** - Pluggable scheduler implementations via vtable interface
- **Multiple Scheduling Policies**:
  - **Preemptive Static Priority** (default) - Priority-based preemption with O(1) lookup
  - **Cooperative** - Non-preemptive, yield-based scheduling
  - **Round-Robin** - Time-sliced FIFO scheduling with configurable quantum
- **Synchronization Primitives**:
  - **Mutexes** with Priority Inheritance Protocol (PIP) to prevent priority inversion
  - **Counting Semaphores** with timeout support
  - **Message Queues** with blocking send/receive and priority-ordered wait lists
- **Software Timers** - One-shot and auto-reload timers with sorted active list
- **Task Management** - Dynamic task creation with configurable priorities and stack sizes
- **Timing Services** - System tick with 1ms resolution and delay functions
- **Cortex-M4 Optimization** - Efficient context switching and interrupt handling
- **Memory Management** - Static bump allocator with stack overflow detection (canary values)
- **Profiling Support** - DWT cycle counter-based profiling for WCET analysis
- **Comprehensive Logging** - Multi-level debug output via UART with structured test logging

## Architecture

### Layered Design

```md
┌─────────────────────────────────────┐
│           Application Layer         │
│        (User Tasks & Examples)      │
├─────────────────────────────────────┤
│             RTOS API Layer          │
│           (Public Interface)        │
├─────────────────────────────────────┤
│       Synchronization Primitives    │
│       (Mutex, Semaphore, Queue)     │
├─────────────────────────────────────┤
│          Scheduler Manager          │
│          (Vtable Interface)         │
├─────────────┬───────────────────────┤
│ Preemptive  │ Cooperative │ RoundRb │
│ Scheduler   │ Scheduler   │ Schedul │
├─────────────┴───────────────────────┤
│             Kernel Core             │
│  (Context Switch, Tick, State Mgmt) │
├─────────────────────────────────────┤
│            Porting Layer            │
│             (Cortex-M4)             │
├─────────────────────────────────────┤
│         Hardware Abstraction        │
│          (STM32F446RE HAL)          │
└─────────────────────────────────────┘
```

### Scheduler Architecture

The scheduler system uses a vtable-based design allowing compile-time scheduler selection:

```c
struct rtos_scheduler {
    /* Core scheduling operations */
    rtos_status_t (*init)(rtos_scheduler_instance_t *instance);
    rtos_task_handle_t (*get_next_task)(rtos_scheduler_instance_t *instance);
    bool (*should_preempt)(rtos_scheduler_instance_t *instance, 
                          rtos_task_handle_t new_task);
    void (*task_completed)(rtos_scheduler_instance_t *instance, 
                          rtos_task_handle_t completed_task);
    
    /* Scheduler-specific list management */
    void (*add_to_ready_list)(rtos_scheduler_instance_t *instance, 
                             rtos_task_handle_t task_handle);
    void (*remove_from_ready_list)(rtos_scheduler_instance_t *instance, 
                                  rtos_task_handle_t task_handle);
    void (*add_to_delayed_list)(rtos_scheduler_instance_t *instance, 
                               rtos_task_handle_t task_handle, 
                               rtos_tick_t delay_ticks);
    void (*remove_from_delayed_list)(rtos_scheduler_instance_t *instance, 
                                    rtos_task_handle_t task_handle);
    void (*update_delayed_tasks)(rtos_scheduler_instance_t *instance);
    
    /* Optional statistics */
    size_t (*get_statistics)(rtos_scheduler_instance_t *instance, 
                           void *stats_buffer, size_t buffer_size);
};
```

## Scheduling Policies

### Preemptive Static Priority (Default)

- **Algorithm**: Highest priority task always runs
- **Preemption**: Immediate when higher priority task becomes ready
- **Data Structure**: Per-priority ready lists with bitmask for O(1) lookup
- **Use Case**: Hard real-time systems requiring deterministic behavior

**Key Characteristics**:

- Priority-based preemption (0-7, higher number = higher priority)
- FIFO ordering within same priority level
- Time-sorted delayed list for efficient timeout management
- Bitmask optimization for fast highest-priority search

### Cooperative (Yield-Based)

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

### Round-Robin (Time-Sliced)

- **Algorithm**: FIFO queue with automatic time-slice preemption
- **Preemption**: Automatic when time slice (20 ticks default) expires
- **Data Structure**: Circular FIFO ready list with tail pointer
- **Use Case**: Fair CPU distribution among equal-priority tasks

**Key Characteristics**:

- Equal time slices for all tasks
- Automatic preemption on quantum expiration
- Tasks rotated to end of queue after yielding
- Time-sorted delayed list for sleeping tasks
- Configurable time slice via `RTOS_TIME_SLICE_TICKS`

## Synchronization Primitives

### Mutexes with Priority Inheritance

**Features**:

- Recursive locking support (same task can lock multiple times)
- Priority Inheritance Protocol (PIP) prevents priority inversion
- Transitive priority inheritance (walks blocking chain)
- Priority-ordered wait queue (highest priority wakes first)
- Timeout support with proper cleanup

**API**:

```c
rtos_mutex_t mutex;
rtos_mutex_init(&mutex);
rtos_mutex_lock(&mutex, RTOS_MAX_WAIT);  // Block forever
rtos_mutex_lock(&mutex, 100);             // 100 tick timeout
rtos_mutex_unlock(&mutex);
```

### Counting Semaphores

**Features**:

- Binary and counting semaphore support
- Priority-ordered wait queue
- Timeout support (0 = try-once, RTOS_MAX_WAIT = forever)
- Thread-safe operations with critical sections

**API**:

```c
rtos_semaphore_t sem;
rtos_semaphore_init(&sem, 0, 5);  // Initial=0, Max=5
rtos_semaphore_wait(&sem, RTOS_SEM_MAX_WAIT);
rtos_semaphore_signal(&sem);
uint32_t count = rtos_semaphore_get_count(&sem);
```

### Message Queues

**Features**:

- Fixed-size circular buffer implementation
- Blocking send/receive with timeout
- Priority-ordered sender and receiver wait lists
- Separate wait lists for full/empty conditions
- Thread-safe with proper critical sections

**API**:

```c
rtos_queue_handle_t queue;
rtos_queue_create(&queue, 10, sizeof(sensor_data_t));
rtos_queue_send(queue, &data, 100);      // Block up to 100 ticks
rtos_queue_receive(queue, &buffer, RTOS_MAX_DELAY);
uint32_t items = rtos_queue_messages_waiting(queue);
```

## Software Timers

**Features**:

- One-shot and auto-reload modes
- Sorted active list for O(n) tick processing
- Wraparound-safe time comparison
- User callback execution in timer tick context
- Create, start, stop, change period, delete operations

**API**:

```c
rtos_timer_handle_t timer;
rtos_timer_create("MyTimer", 1000, RTOS_TIMER_AUTO_RELOAD, 
                  callback, param, &timer);
rtos_timer_start(timer);
rtos_timer_change_period(timer, 500);
rtos_timer_stop(timer);
```

## Memory Management

**Current Implementation**: Bump allocator (simple, predictable)

- Static heap: 16KB configurable via `RTOS_TOTAL_HEAP_SIZE`
- 8-byte alignment for all allocations
- No deallocation (suitable for static task creation)
- Stack overflow detection via canary values (`0xC0DEC0DE`)

**Stack Management**:

- Dynamic stack allocation from heap
- Canary value at stack bottom
- Stack checking via `rtos_task_check_stack()`
- Per-task configurable stack sizes

## Profiling Support

**DWT Cycle Counter-based profiling**:

- System profiling: Context switch, scheduler, tick handler timing
- User profiling: Custom code block measurements
- Min/Max/Average cycle tracking
- Microsecond conversion for readability
- Enable/disable via `RTOS_PROFILING_SYSTEM_ENABLED` and `RTOS_PROFILING_USER_ENABLED`

**Usage**:

```c
rtos_profile_stat_t my_stats = {UINT32_MAX, 0, 0, 0, "MyBlock"};

RTOS_USER_PROFILE_START(work);
// ... code to profile ...
RTOS_USER_PROFILE_END(work, &my_stats);

rtos_profiling_print_stat(&my_stats);
```

## Directory Structure

```md
VRTOS/
├── include/VRTOS/          # Public API headers
│   ├── VRTOS.h            # Main RTOS header
│   ├── config.h           # Configuration parameters
│   ├── task.h             # Task management API
│   ├── scheduler.h        # Scheduler interface
│   ├── mutex.h            # Mutex API
│   ├── semaphore.h        # Semaphore API
│   ├── queue.h            # Queue API
│   ├── timer.h            # Software timer API
│   ├── memory.h           # Memory API
│   ├── profiling.h        # Profiling API
│   ├── rtos_types.h       # Type definitions
│   └── rtos_port.h        # Porting layer interface
├── src/
│   ├── core/              # Kernel core
│   │   ├── kernel.c       # Kernel initialization and tick
│   │   └── memory.c       # Bump allocator
│   ├── scheduler/         # Scheduler implementations
│   │   ├── scheduler.c    # Scheduler manager
│   │   └── scheduler_types/
│   │       ├── preemptive_sp.c  # Preemptive priority
│   │       ├── cooperative.c     # Cooperative
│   │       └── round_robin.c     # Round-robin
│   ├── task/              # Task management
│   │   ├── task.c         # Task creation and state management
│   │   └── task_priv.h    # Private task definitions
│   ├── sync/              # Synchronization primitives
│   │   ├── mutex/
│   │   │   └── mutex.c    # Mutex with priority inheritance
│   │   ├── semaphore/
│   │   │   └── semaphore.c # Counting semaphore
│   │   └── queue/
│   │       └── queue.c    # Message queue
│   ├── timer/             # Software timers
│   │   ├── timer.c        # Timer API
│   │   └── timer_list.c   # Active timer list management
│   ├── port/cortex_m4/    # ARM Cortex-M4 port
│   │   └── port.c         # Context switch, critical sections
│   ├── utils/             # Utilities
│   │   ├── log.c          # UART logging
│   │   ├── profiling.c    # DWT profiling
│   │   ├── rtos_assert.c  # Assertions
│   │   └── hardware_env.c # Hardware initialization
│   └── examples/          # Example applications
│       ├── basic_blinky/
│       ├── producer_consumer/
│       └── profiling_demo/
├── tests/                 # Test suite
│   └── scheduler/         # Scheduler tests
│       ├── test_scheduler_preemptive.c
│       ├── test_scheduler_cooperative.c
│       └── test_scheduler_rr.c
├── config/                # Configuration files
│   ├── stm32f446re/       # Target-specific config
│   │   ├── rtos_config.h
│   │   └── memory_map.h
│   └── test/              # Test configuration
│       └── test_config.h
├── tools/                 # Development tools
│   ├── scripts/           # Build scripts
│   │   ├── pre_build.py
│   │   └── post_build.py
│   └── test/              # Test automation
│       ├── test_runner.py      # Automated test execution
│       ├── log_parser.py       # Parse test logs to CSV
│       ├── timeline_analyzer.py # Compare actual vs expected
│       └── expected_timeline_*.csv # Expected scheduler behavior
└── platformio.ini         # PlatformIO configuration
```

## Configuration

Main configuration in `include/VRTOS/config.h`:

```c
/* System Configuration */
#define RTOS_SYSTEM_CLOCK_HZ    (16000000U)  // 16MHz HSI
#define RTOS_TICK_RATE_HZ       (1000U)      // 1ms tick
#define RTOS_MAX_TASKS          (8U)         // Max task slots
#define RTOS_MAX_TASK_PRIORITIES (8U)        // Priority levels 0-7

/* Scheduler Selection */
#define RTOS_SCHEDULER_TYPE RTOS_SCHEDULER_PREEMPTIVE_SP
// Options: RTOS_SCHEDULER_PREEMPTIVE_SP
//          RTOS_SCHEDULER_COOPERATIVE
//          RTOS_SCHEDULER_ROUND_ROBIN

/* Round-Robin Time Slice */
#define RTOS_TIME_SLICE_TICKS (20)  // 20ms @ 1ms tick

/* Memory Configuration */
#define RTOS_TOTAL_HEAP_SIZE         (16384U)  // 16KB heap
#define RTOS_DEFAULT_TASK_STACK_SIZE (1024U)   // 1KB default
#define RTOS_MINIMUM_TASK_STACK_SIZE (256U)    // 256B minimum

/* Interrupt Priorities (Cortex-M4 4-bit) */
#define RTOS_IRQ_PRIORITY_CRITICAL (0x00)  // Never masked
#define RTOS_IRQ_PRIORITY_HIGH     (0x40)  // Can preempt RTOS
#define RTOS_IRQ_PRIORITY_KERNEL   (0x80)  // SysTick level
#define RTOS_IRQ_PRIORITY_PENDSV   (0xF0)  // Lowest (context switch)

/* Debug Features */
#define RTOS_DEBUG_ENABLED  (1U)
#define RTOS_ASSERT_ENABLED (1U)
#define RTOS_ENABLE_STACK_OVERFLOW_CHECK (1U)
```

## Building and Running

### Prerequisites

- **PlatformIO** (with STM32 platform support)
- **STM32F446RE Nucleo board**
- **ST-Link** programmer (on-board)
- **Python 3.x** (for test automation)

### Quick Start

```bash
# Build and upload basic blinky example
pio run -e basic_blinky --target upload

# Build and upload producer-consumer example
pio run -e producer_consumer --target upload

# Monitor serial output (115200 baud)
pio device monitor -e basic_blinky

# Run automated scheduler test
cd tools/test
python test_runner.py test_scheduler_rr --duration 10
```

### Available Environments

**Examples**:

- `basic_blinky` - Simple LED blinking demonstration
- `producer_consumer` - Queue-based sensor data processing
- `profiling_demo` - Cycle counter profiling example

**Scheduler Tests**:

- `test_scheduler_preemptive` - Preemptive priority scheduling
- `test_scheduler_cooperative` - Cooperative scheduling
- `test_scheduler_rr` - Round-robin scheduling

## Test Automation

The test suite includes automated validation of scheduler behavior:

### Test Workflow

1. **Upload firmware** to STM32 board
2. **Capture serial logs** (tab-delimited format)
3. **Parse logs** to CSV format
4. **Analyze timeline** against expected behavior

### Running Tests

```bash
# Automated end-to-end test
python tools/test/test_runner.py test_scheduler_rr --duration 10

# Manual steps
python tools/test/log_parser.py captured_log.txt -o parsed.csv
python tools/test/timeline_analyzer.py parsed.csv expected_timeline_rr.csv
```

### Log Format

Tab-delimited structured logging for easy parsing:

```csv
timestamp_ms    level   file    line    func    event   context
00000234        TASK    main.c  45      task1   START   Task1
00000234        TASK    main.c  47      task1   RUN     Task1
00000234        TASK    main.c  52      task1   DELAY   Task1
```

## Examples

### Creating Tasks

```c
void my_task(void *param) {
    while (1) {
        // Do work
        led_toggle();
        
        // Yield to scheduler
        rtos_delay_ms(100);
    }
}

int main(void) {
    rtos_init();
    
    rtos_task_handle_t task;
    rtos_task_create(my_task, "MyTask", 
                     RTOS_DEFAULT_TASK_STACK_SIZE,
                     NULL, 5, &task);
    
    rtos_start_scheduler();
}
```

### Using Mutexes

```c
rtos_mutex_t shared_resource_mutex;

void high_priority_task(void *param) {
    while (1) {
        rtos_mutex_lock(&shared_resource_mutex, RTOS_MAX_WAIT);
        // Critical section - priority inherited if needed
        access_shared_resource();
        rtos_mutex_unlock(&shared_resource_mutex);
        
        rtos_delay_ms(100);
    }
}
```

### Producer-Consumer with Queue

```c
rtos_queue_handle_t data_queue;

void producer_task(void *param) {
    sensor_data_t data;
    while (1) {
        data = read_sensor();
        rtos_queue_send(data_queue, &data, 100);  // 100 tick timeout
        rtos_delay_ms(50);
    }
}

void consumer_task(void *param) {
    sensor_data_t data;
    while (1) {
        if (rtos_queue_receive(data_queue, &data, RTOS_MAX_DELAY) == RTOS_SUCCESS) {
            process_data(&data);
        }
    }
}
```

## Debugging Features

### Stack Overflow Detection

```c
// Check all tasks
if (rtos_task_check_stack(NULL)) {
    log_error("Stack overflow detected!");
}

// Check specific task
if (rtos_task_check_stack(my_task_handle)) {
    log_error("Task stack overflow!");
}
```

### Task State Inspection

```c
rtos_task_state_t state = rtos_task_get_state(task_handle);
rtos_priority_t priority = rtos_task_get_priority(task_handle);
rtos_task_debug_print_all();  // Print all task information
```

### Profiling

```c
rtos_profiling_init();

// Profile code block
RTOS_USER_PROFILE_START(my_work);
do_expensive_operation();
RTOS_USER_PROFILE_END(my_work, &my_stats);

// Print system profiling report
rtos_profiling_report_system_stats();
```
