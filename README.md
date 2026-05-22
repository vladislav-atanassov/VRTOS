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
- **Memory Management** - Bi-directional dual-ended heap with first-fit allocation, adjacent-block coalescing, cooperative gap pull-back, per-side diagnostics (free/min-ever/largest-block), and static-allocation variants for task/timer/queue
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
├──────────────────┬─────────────────────┤
│   Porting Layer  │  Board Support Pkg  │
│    (Cortex-M4)   │  (Clock, UART, LED) │
├──────────────────┴─────────────────────┤
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

## Building and Running

### Prerequisites

- **arm-none-eabi-gcc** toolchain (≥ 10.x)
- **CMake** ≥ 3.21
- **Ninja** build system
- **OpenOCD** (for flashing; PlatformIO's bundled copy is auto-detected)
- **Python 3.x** (for the `kartos` CLI)
- **STM32F446RE Nucleo board** with on-board ST-Link

### Python environment (one-time)

```bash
python -m venv .venv
.venv\Scripts\activate      # Windows
# source .venv/bin/activate  # Linux/Mac

pip install -e .
```

<details>
<summary>Windows: execution policy error on activate</summary>

If PowerShell blocks `.venv\Scripts\Activate.ps1` with a `SecurityError`, run this once per user account:

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

`RemoteSigned` lets locally-created scripts run without a signature while still requiring downloaded scripts to be signed. No admin rights needed.

</details>

After activation, `kartos` is available for the lifetime of the venv:

```bash
kartos list
kartos test -e basic_blinky --duration 8
kartos monitor
```

### Quick Start

```bash
# Configure for the STM32F446RE Nucleo board
cmake --preset stm32f446re_nucleo

# Build the basic blinky example
cmake --build --preset basic_blinky

# Flash to the board
cmake --build --preset flash-basic_blinky

# Run automated scheduler test (flash + capture + verdict)
kartos test -e test_scheduler_rr_suite --duration 10
```

### Available Environments

**Examples**:

- `basic_blinky` - Simple LED blinking demonstration
- `producer_consumer` - Queue-based sensor data processing
- `profiling_demo` - Cycle counter profiling example
- `fpu_context_test` - FPU context preservation verification

**Scheduler Test Suites**:

- `test_scheduler_preemptive_suite` - Preemptive priority scheduling cases
- `test_scheduler_cooperative_suite` - Cooperative scheduling cases
- `test_scheduler_rr_suite` - Round-robin scheduling cases

**Integration Test Suites**:

- `test_mutex_suite` - Mutex ownership, blocking, priority inheritance, NULL-input asserts, lock/unlock hook payload
- `test_semaphore_suite` - Counting semaphore wait/signal cases, give/take hook payload, NULL-input asserts
- `test_queue_suite` - Queue blocking, wake, ordering, send/receive/block-full/block-empty hook payload, NULL-input asserts
- `test_event_group_suite` - Event group bit-wait cases
- `test_notify_suite` - Task notification cases
- `test_task_state_suite` - Task lifecycle state transitions, NULL-input asserts, TASK_STATE + TICK hook observability
- `test_tickless_idle_suite` - Tickless idle correctness; force-enables `RTOS_CONFIG_USE_TICKLESS_IDLE=1` regardless of board default
- `test_heap_allocator_suite` - Dual-ended heap cases: routing, gap pull-back, coalescing, fragmentation visibility, cross-heap collision, static-create bypass, dynamic-delete reclaim

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
| `test-all` | `--pattern GLOB`, `--duration SEC`, `--skip-host`, `--skip-board` | Run host tests + every on-board `test_*_suite`; aggregated verdict |
| `host-test` | `--reconfigure`, `-v` | Build and run the pure-logic host unit tests (Unity + FFF) |
| `configure` | | Re-run `cmake --preset <board>` |
| `verbosity` | `LEVEL`, `-e VARIANT`, `--no-reset` | Live-patch `klog_verbosity` in `.noinit` RAM (FAULT…TRACE) |
| `list` | | Print available boards and variant names |
| `clean` | | Delete the board's build directory |

### Test Workflow

1. **Upload firmware** to STM32 board
2. **Capture serial logs** (tab-delimited format)
3. **Parse logs** to CSV format (saved alongside the raw log under `tests/artifacts/`)
4. **Analyze verdict** — each suite emits `CASE_RESULT` lines per test case and a final `SUITE_RESULT` line; `test` detects the suite verdict and exits non-zero on failure

### Usage

```bash
# Automated end-to-end test (upload, capture, verdict)
python -m kartos test -e test_mutex_suite --duration 10

# Capture from an already-running board without reflashing
python -m kartos test -e basic_blinky --skip-upload --skip-analysis --duration 8

# Live serial monitor (auto-detects ST-Link COM port)
python -m kartos monitor
```

### Log Format

Tab-delimited structured logging for easy parsing:

```csv
timestamp_ms    level   file    line    func    event   context
00000234        TASK    main.c  45      task1   START   Task1
00000234        TASK    main.c  47      task1   RUN     Task1
00000234        TASK    main.c  52      task1   DELAY   Task1
```

## Performance (STM32F446RE @ 84 MHz)

Measured on hardware via the automated benchmark suite (`bench_*` variants):

| Primitive | Operation | Min | Max | Avg | Description |
| --- | --- | --- | --- | --- | --- |
| **Context Switch** | Yield → restore | 656 cyc (7 µs) | 810 cyc (9 µs) | 738 cyc (8 µs) | Task yield to task restore (2002 switches) |
| **Mutex** | Uncontended | 351 cyc (4 µs) | 351 cyc (4 µs) | 351 cyc (4 µs) | Fast-path lock/unlock |
| **Mutex** | Contended wake | 1948 cyc (23 µs) | 2903 cyc (34 µs) | 1966 cyc (23 µs) | Unlock-to-wake latency with PIP |
| **Semaphore** | Uncontended | 264 cyc (3 µs) | 264 cyc (3 µs) | 264 cyc (3 µs) | Fast-path take/give |
| **Semaphore** | Wake latency | 1864 cyc (22 µs) | 1864 cyc (22 µs) | 1864 cyc (22 µs) | Signal-to-wake latency |
| **Queue** | Delivery | 2103 cyc (25 µs) | 2365 cyc (28 µs) | 2108 cyc (25 µs) | Send to blocked receiver |

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
rtos_timer_delete(timer);

rtos_timer_t my_timer;
rtos_timer_create_static(&my_timer, "MyTimer", 1000, RTOS_TIMER_AUTO_RELOAD,
                         callback, param, &timer);
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

**Validation**: [tests/integration/test_tickless_idle_suite.c](tests/integration/test_tickless_idle_suite.c) runs five focused cases — basic 50 ms wake, direct SysTick CTRL/LOAD probe post-wake, drift across 20 back-to-back tickless cycles, a 3-second soak (regression test for the historical "tick freeze after ~2.6 s" bug), and microsecond-uptime monotonicity. The variant force-defines `RTOS_CONFIG_USE_TICKLESS_IDLE=1U` via `EXTRA_DEFINES` so the suite always exercises the suppress path regardless of the board default. Run with `kartos test -e test_tickless_idle_suite`.

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
// Dynamic create — stack allocated from RTOS_HEAP_HIGH
rtos_task_create(fn, "Task", 1024, NULL, prio, &handle);

// Static create — caller owns the stack buffer (must be 8-byte aligned)
static uint32_t stack[256] __attribute__((aligned(8)));
rtos_task_create_static(fn, "Task", stack, sizeof(stack), NULL, prio, &handle);

// Suspend and resume
rtos_task_suspend(task_handle);  // NULL = self-suspend
rtos_task_resume(task_handle);

// Delete — releases held mutexes; dynamic stacks freed to heap (self-delete
// defers the free to the idle task); static stacks untouched
rtos_task_delete(task_handle);   // NULL = self-delete
```

## Memory Management

**Implementation**: Bi-directional dual-ended heap — two independent first-fit allocators with adjacent-block coalescing share one backing buffer and grow toward each other from opposite ends.

```text
[LH region][ ...... shared gap ...... ][HH region]
 ^        ^                            ^         ^
 0    lh_top                       hh_bot    HEAP_SIZE
```

- **`RTOS_HEAP_LOW`** — small/short-lived control blocks (timer CB, queue CB). Grows up from the buffer start.
- **`RTOS_HEAP_HIGH`** — large/long-lived buffers (task stacks, queue item storage). Grows down from the buffer end.
- **Shared gap** — unallocated memory available to either side. When a freed block sits at its side's high-water mark, the mark pulls back and the memory returns to the gap, so the two heaps cooperate rather than over-reserving.

This clusters allocations by lifetime and size class, reducing fragmentation versus a single unified pool while keeping the implementation simple (~500 LoC, ~150-line free path).

**Public API** ([include/memory.h](include/memory.h)):

```c
// Allocate from a specific side
void *rtos_malloc_from(rtos_heap_id_t heap, size_t size);

// Convenience wrapper — defaults to RTOS_HEAP_LOW
void *rtos_malloc(size_t size);

// Side is inferred from the pointer; coalesces neighbours; pulls back the mark
void rtos_free(void *ptr);

// Diagnostics: per-side (LOW/HIGH) or aggregate (BOTH)
size_t rtos_memory_get_free_size(rtos_heap_id_t heap);
size_t rtos_memory_get_min_ever_free_size(rtos_heap_id_t heap);
size_t rtos_memory_get_largest_free_block(rtos_heap_id_t heap);
```

**Static-allocation variants** (skip the heap entirely; caller owns storage):

```c
rtos_task_create_static(fn, name, stack_buf, stack_size, param, prio, &handle);
rtos_timer_create_static(&timer_buf, name, period, mode, cb, param, &handle);
rtos_queue_create_static(&queue_buf, item_buf, count, item_size, &handle);
```

Each object tracks an `is_static` flag so the corresponding `_delete` skips the free for caller-owned storage. Dynamic-create paths route to the appropriate side automatically (task stack → HIGH, timer CB → LOW, queue CB → LOW, queue item buffer → HIGH).

**Properties**:

- 8-byte aligned allocations (AAPCS)
- 8-byte block header overhead per allocation; `RTOS_CONFIG_HEAP_MIN_BLOCK_SIZE` (default 16) governs split-vs-take-whole at allocation
- Thread-safe via the kernel critical section
- Asserts on ISR-context use (heap is task-context-only), double-free, bad pointers
- O(N) per side over free-list length on alloc/free; free lists stay short in practice because adjacent blocks coalesce on free and boundary blocks pull back to the gap

**Stack Management**:

- Dynamic stacks via `rtos_task_create` allocate from the HIGH heap; reclaimed by `rtos_task_delete`. Self-delete defers the free to the idle task (the doomed task is still running on the stack — freeing it from inside would let PendSV clobber the freed memory during the context switch).
- Static stacks via `rtos_task_create_static` never touch the heap.
- Canary value (`0xC0DEC0DE`) at stack bottom; check via `rtos_task_check_stack()`.

**Config**:

- `RTOS_TOTAL_HEAP_SIZE` — total pool size (default 16 KB; STM32F401RE board overrides to 12 KB)
- `RTOS_CONFIG_HEAP_MIN_BLOCK_SIZE` — minimum residual to justify splitting a free block (default 16 bytes)

**Validation**: The on-board [test_heap_allocator_suite](tests/integration/test_heap_allocator_suite.c) covers 21 cases (routing, coalescing, gap pull-back, fragmentation visibility, cross-heap collision, static bypass, dynamic delete reclaim). The host-side [test_heap_stress](tests/host/tests/test_heap_stress.c) runs 5,000-200,000 iterations of random alloc/free against the production allocator with per-operation invariant checking and unique-pattern corruption detection (ASAN/UBSAN-enabled on Linux/macOS).

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
│   │   └── memory.c       # Bi-directional dual-ended heap allocator
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
│   │   ├── log_common.h   # Shared path utilities: LOG_BASENAME (compile-time) and log_basename (runtime)
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
├── tests/                 # Test suites (suite-per-binary, case-per-invariant)
│   ├── framework/         # Suite runner, invariant table, watchdog, sync helpers, KASSERT catcher
│   ├── integration/       # Sync primitive and scheduler suites (one binary per area)
│   │   ├── test_mutex_suite.c
│   │   ├── test_semaphore_suite.c
│   │   ├── test_queue_suite.c
│   │   ├── test_event_group_suite.c
│   │   ├── test_notify_suite.c
│   │   ├── test_task_state_suite.c
│   │   ├── test_tickless_idle_suite.c
│   │   ├── test_scheduler_cooperative_suite.c
│   │   ├── test_scheduler_rr_suite.c
│   │   └── test_scheduler_preemptive_suite.c
│   ├── host/              # Unity + FFF host-side unit tests (pure logic, no flash)
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
│   └── kartos/            # KARTOS CLI — invoke as: python -m kartos <subcommand>
│       └── __main__.py    # build, upload, monitor, test, configure, list, clean
├── CMakeLists.txt         # Root build file
├── CMakePresets.json      # Configure + build presets for all boards/variants
├── pyproject.toml         # Python package config for the kartos CLI (pip install -e . from repo root)
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

/* Memory — dual-ended heap (LOW grows up, HIGH grows down, shared gap in the middle) */
#define RTOS_TOTAL_HEAP_SIZE              (16384U)  // 16KB pool
#define RTOS_CONFIG_HEAP_MIN_BLOCK_SIZE   (16U)     // split threshold (must be multiple of 8)
#define RTOS_DEFAULT_TASK_STACK_SIZE      (1024U)   // 1KB default
#define RTOS_MINIMUM_TASK_STACK_SIZE      (256U)    // 256B minimum

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

### Shared Utilities (`log_common.h`)

[src/logging/log_common.h](src/logging/log_common.h) provides two path-stripping helpers used by both the kernel logger and the test infrastructure:

- **`LOG_BASENAME(literal)`** — compile-time macro using `__builtin_strrchr`; reduces `__FILE__` to a bare filename with zero runtime cost (GCC constant-folds it on string literals).
- **`log_basename(path)`** — runtime inline function; walks the string and handles both `/` and `\` separators for portability.

Both prevent long build-system paths from bloating log lines and overflowing fixed-size buffers.

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

### Test Framework (`tests/framework/`)

#### Suite + case + invariant model

Each on-board test binary registers a suite of small focused cases via [tests/framework/test_suite.h](tests/framework/test_suite.h) and declares per-case invariants via [tests/framework/test_invariants.h](tests/framework/test_invariants.h). The runner emits one tab-delimited `CASE_RESULT` line per case plus a final `SUITE_RESULT` line. Verdict lines are written via polled-UART (bypassing the ulog ring buffer) so they survive even with interrupts disabled.

#### Three-tier assertions

[tests/framework/test_assert.h](tests/framework/test_assert.h):

- **`TEST_ASSERT(cond, inv_id)`** — hard fail; longjmps out of the case, suite continues
- **`TEST_EXPECT(cond, inv_id)`** — soft fail; records the miss but the case keeps running (use from spawned tasks; never longjmps)
- **`TEST_ASSUME(cond, reason)`** — precondition; failing this marks the case `SKIPPED`, not `FAILED`
- **`TEST_ASSERT_KASSERT_FIRES(stmt, inv_id)`** — negative test; passes iff `RTOS_ASSERT*` fires inside `stmt`

The watchdog macro `TEST_AWAIT_PHASE("phase", budget_ms, { ... })` in [tests/framework/test_watchdog.h](tests/framework/test_watchdog.h) converts hangs into named invariant failures (`INV-WATCHDOG:<phase>`).

#### KASSERT catcher

[tests/framework/test_kassert_catcher.{c,h}](tests/framework/test_kassert_catcher.c) provides a strong override of `rtos_assert_failed` (which is weakly linked in [src/utils/rtos_assert.c](src/utils/rtos_assert.c)). When `TEST_ASSERT_KASSERT_FIRES` arms a setjmp buffer, the catcher re-enables interrupts and longjmps back so the test can record the assert. Without an armed catcher it falls back to the production halt. Each sync primitive's `null_input_asserts` case (mutex, semaphore, queue, task_state) uses this to prove `RTOS_ASSERT_PARAM(... != NULL)` fires for every NULL-input entry point — debug builds halt on programmer error while release builds (`RTOS_ASSERT_ENABLED=0`) keep the documented `ERR_INVALID` return contract.

#### Kernel test hooks

When the project is built with `-DKARTOS_TEST_HOOKS=ON`, the kernel exposes a synchronous hook fire from key events (context switch, task state transition, tick, mutex boost/restore/lock/unlock, queue send/receive/block, sem give/take, notify, event group set/wait). Hook payloads are enqueued in a lock-free 64-entry ring and dispatched to registered callbacks by the `HookDrain` task. Hook-observability cases verify (a) the hook fires at the right moment and (b) the payload names the correct task/object — see `mutex:lock_fires_lock_exit_hook`, `preempt:ctx_switch_hook_fires_on_preempt`, `task_state:task_state_hook_fires_on_wake`, and `task_state:tick_hook_fires_monotonically` for canonical examples.

#### Host-side tests (`tests/host/`)

Pure-logic unit tests compiled with the host compiler. Each test executable links Unity + the framework fakes ([tests/host/fakes/fakes.c](tests/host/fakes/fakes.c) — FFF-backed stubs for port, scheduler, klog, and `rtos_assert_failed`) against exactly one production `.c` file under test. Run with `kartos host-test` or directly via `ctest --preset host`. Existing tests cover mutex list ordering, mutex PIP walker depth, queue ring buffer, event-group bit matching, and a randomized stress test for the dual-ended heap allocator ([tests/host/tests/test_heap_stress.c](tests/host/tests/test_heap_stress.c) — 5,000+ iterations of random alloc/free against the production allocator with overlap, alignment, and unique-pattern corruption checks; sanitizer-instrumented where supported). The `OWN_HEAP` CMake flag on `add_kartos_host_test()` lets a test that links real `memory.c` opt out of the libc-passthrough heap stubs ([tests/host/fakes/heap_passthrough.c](tests/host/fakes/heap_passthrough.c)).
