# KARTOS — Kernel for ARM Real-Time Operating System

A modular, educational Real-Time Operating System (RTOS) implementation for the STM32F446RE Nucleo board, built from scratch with pluggable scheduler architecture and comprehensive synchronization primitives.

## Project Overview

**KARTOS** (**K**ernel for **ARM** **R**eal-**T**ime **O**perating **S**ystem) is an educational RTOS built from scratch for ARM Cortex-M4 microcontrollers. It features a modular architecture with interchangeable scheduling policies, priority inheritance, and comprehensive profiling capabilities.

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
  - **Event Groups** with bitwise wait conditions (wait-any/wait-all) and ISR-safe signaling
- **Task Notifications** - Lightweight direct task-to-task signaling (set bits, increment, overwrite)
- **Software Timers** - One-shot and auto-reload timers with sorted active list
- **Tickless Idle** - Optional low-power mode that suppresses the SysTick during long idle windows and reconciles tick count on wake
- **Task Management** - Dynamic creation, suspend/resume, delete with automatic mutex cleanup
- **Timing Services** - System tick with 1ms resolution, `rtos_delay_ms()` and `rtos_delay_until()`
- **Cortex-M4 Optimization** - Context switching with lazy FPU stacking
- **Memory Management** - Static bump allocator with stack overflow detection (canary values)
- **Profiling Support** - DWT cycle counter-based profiling for WCET analysis
- **Comprehensive Logging** - Zero-allocation, ISR-safe deferred kernel logger (KLog) and user-facing string logger (ULog)

## Architecture

### Layered Design

```md
┌────────────────────────────────────────┐
│           Application Layer            │
│        (User Tasks & Examples)         │
├────────────────────────────────────────┤
│             RTOS API Layer             │
│           (Public Interface)           │
├────────────────────────────────────────┤
│       Synchronization Primitives       │
│  (Mutex, Semaphore, Queue, EventGroup) │
├────────────────────────────────────────┤
│          Scheduler Manager             │
│          (Vtable Interface)            │
├─────────────┬──────────────────────────┤
│ Preemptive  │ Cooperative │ RoundRobin │
│ Scheduler   │ Scheduler   │ Scheduler  │
├─────────────┴──────────────────────────┤
│             Kernel Core                │
│  (Context Switch, Tick, State Mgmt)    │
├────────────────────────────────────────┤
│            Porting Layer               │
│             (Cortex-M4)                │
├────────────────────────────────────────┤
│         Hardware Abstraction           │
│          (STM32F446RE HAL)             │
└────────────────────────────────────────┘
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

## Performance (STM32F446RE @ 84 MHz)

Captured from system profiling and the automated benchmark suite:

### Kernel Core Latencies

| Metric | Min | Max | Avg | Description |
|--------|-----|-----|-----|-------------|
| **ContextSwitch** | 451 cyc (5 µs) | 1214 cyc (14 µs) | 511 cyc (6 µs) | Task yield to restore |
| **PendSV_Full** | 553 cyc (7 µs) | 1433 cyc (17 µs) | 706 cyc (8 µs) | Full PendSV handler |
| **Scheduler** | 30 cyc (<1 µs) | 765 cyc (9 µs) | 65 cyc (<1 µs) | `get_next_task()` decision |
| **TickHandler** | 350 cyc (4 µs) | 519 cyc (6 µs) | 367 cyc (4 µs) | SysTick ISR processing |
| **TickJitter** | 0 cyc (0 µs) | 4 cyc (<1 µs) | 1 cyc (<1 µs) | SysTick timing deviation |
| **SchedLatency** | 862 cyc (10 µs) | 880 cyc (10 µs) | 862 cyc (10 µs) | Ready → Running delay |

### Synchronization & IPC Primitives

| Primitive | Operation | Min | Max | Avg | Description |
|-----------|-----------|-----|-----|-----|-------------|
| **Mutex** | Uncontended | 216 cyc (3 µs) | 898 cyc (11 µs) | 229 cyc (3 µs) | Fast-path lock/unlock |
| **Mutex** | Contended Wake | 1282 cyc (15 µs) | 2690 cyc (32 µs) | 1341 cyc (16 µs) | Waking a blocked task |
| **Semaphore** | Uncontended | 181 cyc (2 µs) | 181 cyc (2 µs) | 181 cyc (2 µs) | Fast-path take/give |
| **Semaphore** | Wake Latency | 1247 cyc (15 µs) | 1247 cyc (15 µs) | 1247 cyc (15 µs) | Waking a blocked task |
| **Queue** | Delivery | 1424 cyc (17 µs) | 2800 cyc (33 µs) | 1531 cyc (18 µs) | Send to blocked receiver |

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
- **Preemption**: Automatic when time slice (1 tick default) expires
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

> **Warning**: Timer callbacks execute in **ISR context** (SysTick handler). They must not call blocking RTOS APIs (`rtos_mutex_lock`, `rtos_semaphore_wait`, `rtos_delay_ms`, etc.). Use ISR-safe APIs only (e.g. `rtos_event_group_set_bits_from_isr`, `rtos_task_notify`).

## Tickless Idle

When the only runnable task is the idle task, KARTOS can suppress the periodic SysTick interrupt and put the CPU into `WFI` until either the next scheduled task wake-up or an external interrupt. On wake, the kernel reads the SysTick counter, computes how long the CPU actually slept, and fast-forwards `rtos_tick_count` so timing services remain correct. Without this feature the SysTick would keep firing every 1 ms, waking the CPU thousands of times per second just to decrement counters.

**How it works** (Cortex-M4, [arch/cortex_m4/port.c](arch/cortex_m4/port.c)):

1. **Idle task** asks the scheduler how many ticks until the next ready/delay expiry — `rtos_scheduler_get_expected_idle_ticks()`.
2. If the result is below `RTOS_CONFIG_EXPECTED_IDLE_TIME_BEFORE_SLEEP`, do nothing — the reprogramming overhead would exceed the savings. Otherwise call `rtos_port_suppress_ticks_and_sleep()`.
3. **Port layer** masks IRQs, re-checks that no `PendSV` is pending, stops SysTick, reprograms `LOAD` to the requested sleep duration (capped to the 24-bit SysTick limit, ~199 ms at 84 MHz), restarts SysTick, then issues `WFI`.
4. **On wake** (either SysTick rollover or an external IRQ), the port reads `SysTick->VAL` and `COUNTFLAG` to determine the actual ticks elapsed, calls `rtos_kernel_step_tick(elapsed)` to advance the kernel's tick count and unblock any tasks whose delays expired, restores the standard 1 ms tick reload, and re-enables IRQs.

**Configuration**:

```c
/* Master switch — disabled by default in include/config.h */
#define RTOS_CONFIG_USE_TICKLESS_IDLE (1U)

/* Minimum idle window (in ticks) before entering tickless sleep.
 * Short windows aren't worth the SysTick reprogramming overhead. */
#define RTOS_CONFIG_EXPECTED_IDLE_TIME_BEFORE_SLEEP (5U)
```

The STM32F446RE board config enables tickless idle by default ([boards/stm32f446re_nucleo/rtos_config.h](boards/stm32f446re_nucleo/rtos_config.h)).

**Caveats**:

- Maximum single sleep duration is bounded by the 24-bit SysTick (~199 ms at 84 MHz). Longer delays wake briefly and re-enter sleep.
- Any peripheral whose clock is gated by `WFI` (e.g. peripherals on a stopped bus clock in `STOP` mode) will not wake the CPU — the current port uses plain `WFI` (Sleep mode), not `Deep Sleep`, so AHB/APB peripherals continue to run.
- Tick-driven profiling counters reflect wall-clock time correctly, but per-task CPU usage measured by counting ticks may show the idle task as "asleep" rather than "running" during suppressed windows.

**Validation**: [tests/integration/test_tickless_idle.c](tests/integration/test_tickless_idle.c) sweeps `rtos_delay_ms()` across 1 ms–2000 ms windows and asserts both the kernel tick count and `rtos_port_get_uptime_us()` advance within ±(2 ticks + 1 % of requested delay).

### Event Groups

**Features**:

- Bitwise wait conditions: wait for ANY or ALL bits
- Clear-on-exit option for automatic bit clearing
- Priority-ordered wait list with multiple concurrent waiters
- ISR-safe `set_bits_from_isr()` variant
- Deferred bit clearing to avoid race conditions

**API**:

```c
rtos_event_group_t eg;
rtos_event_group_init(&eg);

// Wait for bits 0 and 2 to both be set, clear them on exit
uint32_t bits;
rtos_event_group_wait_bits(&eg, 0x05, true, true, &bits, RTOS_EG_MAX_WAIT);

// Set bits from task or ISR context
rtos_event_group_set_bits(&eg, 0x05);
rtos_event_group_set_bits_from_isr(&eg, 0x01);
```

### Task Notifications

**Features**:

- Lightweight direct task-to-task signaling (no kernel object needed)
- Multiple actions: set bits, increment, overwrite, or just signal
- Can be used as a fast binary/counting semaphore replacement
- ISR-safe sending

**API**:

```c
// Send notification with value
rtos_task_notify(target_task, 0x01, RTOS_NOTIFY_ACTION_SET_BITS);

// Lightweight give/take (counting semaphore pattern)
rtos_task_notify_give(target_task);
rtos_task_notify_take(true, RTOS_NOTIFY_MAX_WAIT);

// Bit-level wait with entry/exit clear control
uint32_t value;
rtos_task_notify_wait(0x00, 0xFF, &value, 1000);
```

### Task Lifecycle Management

**API**:

```c
// Suspend and resume
rtos_task_suspend(task_handle);  // NULL = self-suspend
rtos_task_resume(task_handle);

// Delete (automatically releases held mutexes)
rtos_task_delete(task_handle);   // NULL = self-delete
```

## Memory Management

**Current Implementation**: Bump allocator (simple, predictable)

- Static heap: configurable via `RTOS_TOTAL_HEAP_SIZE` (default 16 KB; STM32F446RE board config overrides to 8 KB)
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
KARTOS/
├── include/               # Public API headers
│   ├── KARTOS.h           # Main RTOS header
│   ├── config.h           # Configuration defaults
│   ├── task.h             # Task management API
│   ├── scheduler.h        # Scheduler interface
│   ├── mutex.h            # Mutex API
│   ├── semaphore.h        # Semaphore API
│   ├── queue.h            # Queue API
│   ├── event_group.h      # Event group API
│   ├── timer.h            # Software timer API
│   ├── memory.h           # Memory API
│   ├── profiling.h        # Profiling API
│   ├── rtos_types.h       # Type definitions
│   └── rtos_port.h        # Porting layer interface
├── arch/                  # Architecture porting layer
│   ├── common/            # Shared port contract (port_common.h)
│   └── cortex_m4/         # ARM Cortex-M4F port
│       ├── port_priv.h    # Arch constants + interrupt priorities
│       └── port.c         # Context switch, critical sections
├── boards/                # Board-specific configuration
│   ├── stm32f446re_nucleo/  # STM32F446RE Nucleo
│   │   ├── board.cmake    # CMake board descriptor + kartos::board_support STATIC target
│   │   ├── hardware_env.c # Clock/GPIO/LED bootstrap (BSP — not required by the kernel)
│   │   ├── hardware_env.h # Bootstrap API + hardware_env_cpu_clock_hz()
│   │   ├── uart_tx.c      # UART transport implementation (BSP)
│   │   ├── rtos_config.h  # Board RTOS overrides
│   │   ├── memory_map.h   # Flash/SRAM layout (shared with linker)
│   │   ├── clock_config.h # Clock aliases (RTOS_CPU_CLOCK_HZ, RTOS_CYCLES_PER_TICK)
│   │   ├── linker.ld.in   # Preprocessable linker script template
│   │   └── openocd.cfg    # OpenOCD flash/debug configuration
│   ├── stm32f401re_nucleo/  # STM32F401RE Nucleo (portability target)
│   │   └── ...            # Same layout as above
│   └── templates/         # Skeleton files for porting to a new board
│       ├── rtos_config_template.h
│       ├── clock_config_template.h
│       └── memory_map_template.h
├── cmake/                 # CMake helper modules
│   ├── arm-none-eabi-gcc.cmake    # Cross-compilation toolchain file
│   ├── kartos_add_variant.cmake   # kartos_add_variant() function
│   ├── kartos_build_info.cmake    # Build-info generation
│   ├── kartos_linker_script.cmake # Linker script preprocessing
│   └── variants.cmake             # All build variant definitions
├── mcus/                  # MCU-family CMake configuration
│   └── stm32f4/
│       └── family.cmake   # Compiler flags, startup file, HAL sources
├── src/
│   ├── core/              # Kernel core
│   │   ├── kernel.c       # Kernel initialization and tick
│   │   └── memory.c       # Bump allocator
│   ├── scheduler/         # Scheduler implementations
│   │   ├── scheduler.c    # Scheduler manager
│   │   └── scheduler_types/
│   │       ├── preemptive_sp.c  # Preemptive priority
│   │       ├── cooperative.c    # Cooperative
│   │       └── round_robin.c    # Round-robin
│   ├── task/              # Task management
│   │   ├── task.c         # Task creation and state management
│   │   ├── task_notify.c  # Task notification mechanism
│   │   └── task_priv.h    # Private task definitions
│   ├── sync/              # Synchronization primitives
│   │   ├── mutex/         # Mutex with priority inheritance
│   │   ├── semaphore/     # Counting semaphore
│   │   ├── queue/         # Message queue
│   │   └── event_group/   # Event group (bit-field sync)
│   ├── timer/             # Software timers
│   │   ├── timer.c        # Timer API
│   │   └── timer_list.c   # Active timer list management
│   ├── logging/           # Logging subsystem
│   │   ├── uart_tx.h      # UART transport contract (HAL-free; .c implementation lives in BSP)
│   │   ├── klog.c/h       # High-performance deferred kernel logger
│   │   ├── ulog.c/h       # User-facing string logger
│   │   └── log_flush_task.c/h  # Flush task (formats KLog and drains ULog)
│   ├── profiling/         # Profiling subsystem
│   │   ├── profiling.c    # DWT cycle counter profiling
│   │   └── prof_trace.c/h # Profiling trace ring buffer
│   ├── utils/             # Shared utilities
│   │   ├── ring_buffer.c/h  # General-purpose ring buffer
│   │   └── rtos_assert.c/h  # Assertions
│   └── examples/          # Example applications
│       ├── basic_blinky/
│       ├── producer_consumer/
│       ├── profiling_demo/
│       └── fpu_context_test/
├── tests/                 # Test suite
│   ├── integration/       # Sync primitive invariant tests
│   │   ├── test_mutex_state.c          # PIP + ownership invariants
│   │   ├── test_semaphore_state.c      # Counting semaphore invariants
│   │   ├── test_queue_state.c          # Queue blocking invariants
│   │   ├── test_event_group_state.c    # Event group bit-wait tests
│   │   ├── test_notification_state.c   # Task notification tests
│   │   └── test_task_state_transitions.c # Task lifecycle tests
│   ├── scheduler/         # Scheduler tests (one dir per policy)
│   │   ├── round_robin/
│   │   ├── preemptive/
│   │   └── cooperative/
│   └── benchmarks/        # Cycle-accurate benchmarks
│       ├── bench_context_switch/
│       ├── bench_mutex/
│       ├── bench_queue/
│       └── bench_semaphore/
├── vendor/
│   └── stm32cubef4/       # STM32CubeF4 HAL/CMSIS (git submodule)
├── docs/                  # Documentation
│   └── porting_guide.md   # How to add a new chip/architecture
├── tools/
│   ├── kartos/            # KARTOS CLI — invoke as: python -m kartos <subcommand>
│   │   └── __main__.py    # build, upload, monitor, test, configure, list, clean
│   └── test/              # Supporting utilities
│       └── log_parser.py   # Parse serial logs to CSV
├── CMakeLists.txt         # Root build file
├── CMakePresets.json      # Configure + build presets for all boards/variants
└── .clangd                # clangd cross-compilation settings
```

## Configuration

Configuration uses a hierarchical override system:

```
boards/<board>/rtos_config.h   ← board-specific overrides (included first)
    ├── memory_map.h           ← flash/SRAM layout
    └── clock_config.h         ← clock aliases
include/config.h               ← generic defaults (wrapped in #ifndef guards)
```

Board overrides are applied by defining macros **before** the defaults in `config.h`.
To add a new board, copy `boards/templates/rtos_config_template.h` to `boards/<board>/rtos_config.h`
and uncomment the values you need to override. See [docs/porting_guide.md](docs/porting_guide.md) for the full walkthrough.

### Generic Defaults (`config.h`)

```c
/* System */
#define RTOS_SYSTEM_CLOCK_HZ    (16000000U)  // 16MHz HSI
#define RTOS_TICK_RATE_HZ       (1000U)      // 1ms tick
#define RTOS_MAX_TASKS          (8U)         // Max task slots
#define RTOS_MAX_TASK_PRIORITIES (8U)        // Priority levels 0-7

/* Scheduler */
#define RTOS_SCHEDULER_TYPE RTOS_SCHEDULER_PREEMPTIVE_SP
#define RTOS_TIME_SLICE_TICKS (1)   // 1ms @ 1ms tick

/* Power Management */
#define RTOS_CONFIG_USE_TICKLESS_IDLE               (0U)  // 1 to enable tickless idle
#define RTOS_CONFIG_EXPECTED_IDLE_TIME_BEFORE_SLEEP (5U)  // ticks; below this, stay ticking

/* Memory */
#define RTOS_TOTAL_HEAP_SIZE         (16384U)  // 16KB heap (STM32F446RE overrides to 8KB)
#define RTOS_DEFAULT_TASK_STACK_SIZE (1024U)   // 1KB default
#define RTOS_MINIMUM_TASK_STACK_SIZE (256U)    // 256B minimum

/* Logging */
#define RTOS_UART_BAUD_RATE (921600U)  // UART baud rate for log output

/* Debug */
#define RTOS_ASSERT_ENABLED (1U)
#define RTOS_ENABLE_STACK_OVERFLOW_CHECK (1U)
```

> **Note:** `RTOS_UART_BAUD_RATE` configures the on-board UART only. If you change it, pass the same baud rate to `python -m kartos test -b <baud>` or your serial monitor. Mismatched values produce garbled serial output.

### Port-Layer Constants (`port_priv.h`)

Interrupt priorities are architecture-specific and live in the port layer,
not in `config.h`:

```c
/* Cortex-M4 interrupt priorities (arch/cortex_m4/port_priv.h) */
#define PORT_IRQ_PRIORITY_CRITICAL (0x00)  // Never masked
#define PORT_IRQ_PRIORITY_HIGH     (0x40)  // Can preempt RTOS
#define PORT_IRQ_PRIORITY_KERNEL   (0x80)  // SysTick level
#define PORT_IRQ_PRIORITY_PENDSV   (0xF0)  // Lowest (context switch)
```

## Building and Running

### Prerequisites

- **arm-none-eabi-gcc** toolchain (≥ 10.x)
- **CMake** ≥ 3.21
- **Ninja** build system
- **OpenOCD** (for flashing; PlatformIO's bundled copy is auto-detected)
- **Python 3.x** with `pyserial` (for test automation: `pip install pyserial`)
- **STM32F446RE Nucleo board** with on-board ST-Link

### Quick Start

```bash
# Configure for the STM32F446RE Nucleo board
cmake --preset stm32f446re_nucleo

# Build the basic blinky example
cmake --build --preset basic_blinky

# Flash to the board
cmake --build --preset flash-basic_blinky

# Run automated scheduler test (flash + capture + verdict)
python -m kartos test -e test_scheduler_rr_state --duration 10
```

### Available Environments

**Examples**:

- `basic_blinky` - Simple LED blinking demonstration
- `producer_consumer` - Queue-based sensor data processing
- `profiling_demo` - Cycle counter profiling example
- `fpu_context_test` - FPU context preservation verification

**Scheduler Tests**:

- `test_scheduler_preemptive_state` - Preemptive priority scheduling invariants
- `test_scheduler_cooperative_state` - Cooperative scheduling invariants
- `test_scheduler_rr_state` - Round-robin scheduling invariants

**Integration Tests**:

- `test_mutex_state` - Mutex state and priority inheritance invariants
- `test_semaphore_state` - Counting semaphore invariants
- `test_queue_state` - Queue blocking and wake invariants
- `test_event_group_state` - Event group bit-wait invariants
- `test_notification_state` - Task notification invariants
- `test_task_state_transitions` - Task lifecycle state transitions
- `test_tickless_idle` - Tickless idle: tick/uptime advance correctly across delays from 1 ms to 2 s

**Benchmarks**:

- `bench_context_switch` - Context switch cycle measurement
- `bench_mutex` - Mutex lock/unlock latency
- `bench_queue` - Queue send/receive latency
- `bench_semaphore` - Semaphore signal/wait latency

## KARTOS CLI

`tools/kartos/` is the project CLI. Run it from the repo root with:

```bash
python -m kartos <subcommand> [options]
```

The `--board` flag (or the `KARTOS_BOARD` environment variable, or a `.kartosrc` file) selects the target board; defaults to `stm32f446re_nucleo`.

### Subcommands

| Subcommand | Key options | Description |
| --- | --- | --- |
| `build` | `-e VARIANT` | CMake build for a named variant |
| `upload` | `-e VARIANT` | Build + OpenOCD flash |
| `monitor` | `-p PORT`, `-b BAUD` | Live serial monitor (Ctrl+C to quit) |
| `test` | `-e VARIANT`, `--duration SEC`, `--skip-upload`, `--skip-analysis` | Flash, capture serial, parse, and emit a pass/fail verdict |
| `configure` | | Re-run `cmake --preset <board>` |
| `list` | | Print available boards and variant names |
| `clean` | | Delete the board's build directory |

### Test Workflow

1. **Upload firmware** to STM32 board
2. **Capture serial logs** (tab-delimited format)
3. **Parse logs** to CSV format (`tools/test/log_parser.py`)
4. **Analyze verdict** — each test emits a `RESULT:PASS` or `RESULT:FAIL` line that `test` detects and returns as the exit code

### Usage

```bash
# Automated end-to-end test (upload, capture, verdict)
python -m kartos test -e test_scheduler_rr_state --duration 10

# Capture from an already-running board without reflashing
python -m kartos test -e basic_blinky --skip-upload --skip-analysis --duration 8

# Live serial monitor (auto-detects ST-Link COM port)
python -m kartos monitor

# Parse a captured log file to CSV
python tools/test/log_parser.py captured_log.txt -o parsed.csv
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

## Logging System

KARTOS features a dual-tier logging architecture designed to provide extensive visibility without compromising real-time performance.

### Kernel Logger (KLog)

The `klog` system is designed for high-performance, internal RTOS tracing. It uses a deferred formatting approach to ensure zero allocation and ISR-safety.

- **Zero-Allocation & Fast**: Captures up to 4 raw arguments and metadata pointers (like `__FILE__`, `__LINE__`, and `module`) into a fixed-size packed struct.
- **ISR-Safe**: Uses lockless ring buffers with critical sections, allowing safe logging from any context, including interrupts.
- **Background Formatting**: A dedicated `log_flush_task` pops packets from the ring buffer, runs the `snprintf` formatting (e.g. `00001204 [IdleTask ] [Kernel ] I kernel.c:45 | Entering low power mode`), and transmits them via UART to prevent blocking the RTOS core.

**API**:

```c
// Shorthand macros automatically capture file/line context
KLOGI("Queue", "Created queue %s with %d items", q_name, q_size);
KLOGE("Scheduler", "Failed to start task %d", task_id);
```

### User Logger (ULog)

The `ulog` system provides a standard, `printf`-style deferred logger for user applications.

- **String-based**: Formats strings immediately into a character buffer.
- **Deferred Output**: The `log_flush_task` drains the buffer asynchronously.
- **Not ISR-Safe**: Intended only for application task use.

**API**:

```c
ulog_info("Connection established to %s", ip_addr);
ulog_error("Failed to read sensor: %d", error_code);
```
