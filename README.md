# KARTOS — Kernel for ARM Real-Time Operating System

A modular, educational Real-Time Operating System (RTOS) implementation for the STM32F446RE Nucleo board, built from scratch with pluggable scheduler architecture and comprehensive synchronization primitives.

## Project Overview

**KARTOS** (**K**ernel for **ARM** **R**eal-**T**ime **O**perating **S**ystem) is an educational RTOS built from scratch for ARM Cortex-M4 microcontrollers. It features a modular architecture with interchangeable scheduling policies, priority inheritance, and comprehensive profiling capabilities.

### Key Features

- **Modular Scheduler Architecture** - Pluggable scheduler implementations via vtable interface
- **Multiple Scheduling Policies**:
  - **Preemptive Static Priority** (default) - Priority-based preemption; per-priority ready buckets with a `__builtin_clz` bitmask for O(1) highest-priority lookup and an O(1) tail pointer for ready-list append
  - **Cooperative** - Non-preemptive, yield-based, priority-then-FIFO within a bucket (same O(1) lookup as preemptive)
  - **Round-Robin** - Time-sliced priority-then-FIFO with configurable quantum (same O(1) lookup as preemptive)
- **Synchronization Primitives**:
  - **Mutexes** with Priority Inheritance Protocol (PIP) to prevent priority inversion and a true cycle check on lock that returns `RTOS_MUTEX_ERR_DEADLOCK` instead of hanging
  - **Counting Semaphores** with timeout support and an ISR-safe `signal_from_isr` variant
  - **Message Queues** with blocking send/receive, priority-ordered wait lists, and ISR-safe `send_from_isr` / `receive_from_isr` variants
  - **Event Groups** with bitwise wait conditions (wait-any/wait-all) and ISR-safe signaling
- **Task Notifications** - Lightweight direct task-to-task signaling (set bits, increment, overwrite) with task-context and ISR-context (`_from_isr`) variants
- **Scheduler Control** - Nested `rtos_scheduler_suspend()` / `rtos_scheduler_resume()` for atomic multi-step updates; deferred yield is fired on the matching resume
- **Software Timers** - One-shot and auto-reload timers with sorted active list; opt-in service task (`rtos_timer_task_init()`) moves callback dispatch from SysTick ISR to task context so callbacks may use blocking RTOS APIs
- **Application Hooks** - Weak `rtos_application_*_hook()` symbols for idle, tick, stack overflow, malloc failure, and mutex deadlock; override in the app to halt, log, or telemeter
- **Tickless Idle** - Optional low-power mode that suppresses the SysTick during long idle windows and reconciles tick count on wake
- **Task Management** - Dynamic creation, suspend/resume, delete with automatic mutex cleanup
- **Stack Overflow Detection** - Per-task canary checked on every context switch (gated by `RTOS_CONFIG_AUTO_STACK_CHECK_ON_SWITCH`) plus on-demand `rtos_task_check_stack()`
- **Timing Services** - System tick with 1ms resolution, `rtos_delay_ms()` and `rtos_delay_until()`
- **Cortex-M4 Optimization** - Context switching with lazy FPU stacking
- **Memory Management** - Bi-directional dual-ended heap with first-fit allocation, adjacent-block coalescing, cooperative gap pull-back, per-side diagnostics (free/min-ever/largest-block), and static-allocation variants for task/timer/queue
- **Profiling Support** - DWT cycle counter-based profiling for WCET analysis
- **Comprehensive Logging** - Unified core (shared `log_level_t`, single auto-created flush task, `.noinit` verbosity globals) with two backends: zero-allocation ISR-safe kernel logger (KLog) and printf-style user logger (ULog); live-patchable per-backend via the `kartos verbosity` CLI

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

- `test_mutex_suite` - Mutex ownership, blocking, priority inheritance, deadlock cycle detection, recursion-saturation status, NULL-input asserts, lock/unlock hook payload
- `test_semaphore_suite` - Counting semaphore wait/signal cases, give/take hook payload, NULL-input asserts
- `test_queue_suite` - Queue blocking, wake, ordering, send/receive/block-full/block-empty hook payload, NULL-input asserts
- `test_event_group_suite` - Event group bit-wait cases
- `test_notify_suite` - Task notification cases
- `test_task_state_suite` - Task lifecycle state transitions, NULL-input asserts, TASK_STATE + TICK hook observability
- `test_tickless_idle_suite` - Tickless idle correctness; force-enables `RTOS_CONFIG_USE_TICKLESS_IDLE=1` regardless of board default
- `test_heap_allocator_suite` - Dual-ended heap cases: routing, gap pull-back, coalescing, fragmentation visibility, cross-heap collision, static-create bypass, dynamic-delete reclaim
- `test_hooks_suite` - Application hook fire sites: idle, tick (value + cadence), stack overflow on demand and on context switch, malloc-failed (OOM)
- `test_scheduler_suspend_suite` - `rtos_scheduler_suspend()` / `rtos_scheduler_resume()` counter mechanics, tick advance during suspend, delayed-wake landing on the ready list, yield-under-suspend is a no-op
- `test_isr_safe_suite` - `_from_isr` wake paths for semaphore / queue (send + receive) / task notify (notify + give); a 1-tick one-shot timer is the deterministic ISR source
- `test_timer_task_suite` - Optional software timer service task (`rtos_timer_task_init()`): auto-reload in task context, callback acquiring a contended mutex, long-running callback does not stall SysTick

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
| `upload-monitor` | `-e VARIANT`, `-p PORT`, `-b BAUD` | Attach monitor first, then flash — catches boot output of fast-completing firmware |
| `test` | `-e VARIANT`, `--duration SEC`, `--skip-upload`, `--skip-analysis` | Flash, capture serial, parse, and emit a pass/fail verdict |
| `test-all` | `--pattern GLOB`, `--duration SEC`, `--skip-host`, `--skip-board` | Run host tests + every on-board `test_*_suite`; aggregated verdict |
| `host-test` | `--reconfigure`, `-v` | Build and run the pure-logic host unit tests (Unity + FFF) |
| `configure` | | Re-run `cmake --preset <board>` |
| `verbosity` | `LEVEL`, `-t klog\|ulog\|both`, `-e VARIANT`, `--no-reset` | Live-patch `klog_verbosity` and/or `ulog_verbosity` in `.noinit` RAM (FAULT…TRACE). Defaults to `--target=klog`; use `-t both` to patch both backends in one shot. |
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

Measured on hardware via the automated benchmark suite (`bench_*` variants),
with kernel tooling tuned down to a production-style baseline:
`KARTOS_TEST_HOOKS=OFF`, `RTOS_ASSERT_ENABLED=0`,
`RTOS_CONFIG_AUTO_STACK_CHECK_ON_SWITCH=0`, `RTOS_CONFIG_USE_TICKLESS_IDLE=0`,
and `RTOS_KLOG_MIN_LEVEL=0` (FAULT-only). System-profiling stats stay enabled
because the benchmarks read them.

| Primitive | Operation | Min | Max | Avg | Description |
| --- | --- | --- | --- | --- | --- |
| **Context Switch** | Yield → restore | 639 cyc (7 µs) | 775 cyc (9 µs) | 705 cyc (8 µs) | Full `rtos_kernel_switch_context()` over 2002 switches |
| **Mutex** | Uncontended | 346 cyc (4 µs) | 346 cyc (4 µs) | 346 cyc (4 µs) | Fast-path lock/unlock |
| **Mutex** | Contended wake | 1951 cyc (23 µs) | 2886 cyc (34 µs) | 1969 cyc (23 µs) | Unlock-to-wake latency with PIP |
| **Semaphore** | Uncontended | 269 cyc (3 µs) | 269 cyc (3 µs) | 269 cyc (3 µs) | Fast-path take/give |
| **Semaphore** | Wake latency | 1872 cyc (22 µs) | 1872 cyc (22 µs) | 1872 cyc (22 µs) | Signal-to-wake latency |
| **Queue** | Delivery | 2131 cyc (25 µs) | 3067 cyc (36 µs) | 2152 cyc (25 µs) | Send to blocked receiver |

Numbers are µs-rounded from cycle counts at 84 MHz (1 µs ≈ 84 cyc). The
context-switch path still runs the scheduler-suspend gate and the standard
priority bookkeeping; with the bench config the per-switch stack-canary check
(`RTOS_CONFIG_AUTO_STACK_CHECK_ON_SWITCH`) is off, so the application
stack-overflow hook only fires on demand via `rtos_task_check_stack()`. Mutex
lock still walks the wait-for graph to prevent `RTOS_MUTEX_ERR_DEADLOCK` from
turning into a hang. Re-enable any of the debug knobs above if you want the
production-debug numbers instead — expect roughly +10% on the context-switch
path and +30% on the uncontended fast paths once asserts come back.

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

- **Algorithm**: Priority-then-FIFO; runs the highest-priority ready task and rotates within that bucket on yield
- **Preemption**: None — tasks must explicitly yield
- **Data Structure**: Per-priority ready lists with `__builtin_clz` bitmask (O(1) highest-priority lookup) and per-bucket tail pointer (O(1) append)
- **Use Case**: Simple applications, reduced context switch overhead

**Key Characteristics**:

- Non-preemptive execution
- Tasks run until voluntary yield (`rtos_yield()` or delay)
- Within a priority bucket, yielding tasks move to the tail (round-robin)
- Lower interrupt overhead
- No time-slicing - task scheduling is purely voluntary

### Round-Robin (Time-Sliced)

- **Algorithm**: Priority-then-FIFO with automatic time-slice preemption inside the highest-priority bucket
- **Preemption**: Automatic when the time slice (1 tick default) expires
- **Data Structure**: Per-priority ready lists with `__builtin_clz` bitmask (O(1) highest-priority lookup) and per-bucket tail pointer (O(1) append + rotate)
- **Use Case**: Fair CPU distribution among equal-priority tasks

**Key Characteristics**:

- Equal time slices for tasks at the same priority
- Strict priority order across buckets (higher priority always preempts lower)
- Automatic preemption on quantum expiration
- Tasks rotate to the tail of their bucket after a slice expires
- Time-sorted delayed list for sleeping tasks
- Configurable time slice via `RTOS_TIME_SLICE_TICKS`

## Synchronization Primitives

### Mutexes with Priority Inheritance

**Features**:

- Recursive locking support (same task can lock multiple times, up to 255)
- Priority Inheritance Protocol (PIP) prevents priority inversion
- Transitive priority inheritance (walks blocking chain)
- Priority-ordered wait queue (highest priority wakes first)
- Timeout support with proper cleanup
- **Deadlock cycle detection** — every `rtos_mutex_lock()` walks the wait-for graph; a lock that would close a cycle (A holds X waiting Y, B holds Y waiting X) returns `RTOS_MUTEX_ERR_DEADLOCK` and fires `rtos_application_deadlock_hook(waiter, mutex)` instead of hanging the caller. The walk is bounded by `RTOS_MAX_TASKS` so it terminates even if the graph is corrupted.
- **Distinct recursion-saturation status** — the 256th nested lock returns the dedicated `RTOS_MUTEX_ERR_RECURSION` (previously collapsed into `RTOS_MUTEX_ERR_GENERAL`).

**API**:

```c
rtos_mutex_t mutex;
rtos_mutex_init(&mutex);
rtos_mutex_lock(&mutex, RTOS_MAX_WAIT);  // Block forever
rtos_mutex_lock(&mutex, 100);             // 100 tick timeout
rtos_mutex_unlock(&mutex);
```

**Error codes** ([include/mutex.h](include/mutex.h)):

```text
RTOS_MUTEX_OK            ok
RTOS_MUTEX_ERR_INVALID   NULL handle or no current task
RTOS_MUTEX_ERR_TIMEOUT   wait expired without acquisition
RTOS_MUTEX_ERR_DEADLOCK  this lock would close a cycle (see app hook)
RTOS_MUTEX_ERR_RECURSION uint8_t lock_count saturated at 255
RTOS_MUTEX_ERR_GENERAL   misc kernel failure
```

### Counting Semaphores

**Features**:

- Binary and counting semaphore support
- Priority-ordered wait queue
- Timeout support (0 = try-once, RTOS_MAX_WAIT = forever)
- Thread-safe operations with critical sections
- ISR-safe `rtos_semaphore_signal_from_isr()` for wake-from-interrupt

**API**:

```c
rtos_semaphore_t sem;
rtos_semaphore_init(&sem, 0, 5);  // Initial=0, Max=5
rtos_semaphore_wait(&sem, RTOS_SEM_MAX_WAIT);
rtos_semaphore_signal(&sem);                  // task context
rtos_semaphore_signal_from_isr(&sem);         // interrupt context
uint32_t count = rtos_semaphore_get_count(&sem);
```

### Message Queues

**Features**:

- Fixed-size circular buffer implementation
- Blocking send/receive with timeout
- Priority-ordered sender and receiver wait lists
- Separate wait lists for full/empty conditions
- Thread-safe with proper critical sections
- ISR-safe non-blocking `rtos_queue_send_from_isr()` / `rtos_queue_receive_from_isr()` variants

**API**:

```c
rtos_queue_handle_t queue;
rtos_queue_create(&queue, 10, sizeof(sensor_data_t));
rtos_queue_send(queue, &data, 100);              // task context, block up to 100 ticks
rtos_queue_receive(queue, &buffer, RTOS_MAX_DELAY);

/* ISR context: never blocks; returns RTOS_ERROR_FULL / RTOS_ERROR_EMPTY
   instead of waiting. */
rtos_queue_send_from_isr(queue, &data);
rtos_queue_receive_from_isr(queue, &buffer);

uint32_t items = rtos_queue_messages_waiting(queue);
```

## Software Timers

**Features**:

- One-shot and auto-reload modes
- Sorted active list for O(n) tick processing
- Wraparound-safe time comparison
- Two callback dispatch modes (see below) — direct-from-ISR by default; task-context after `rtos_timer_task_init()`
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

### Callback dispatch mode

| Mode | When | Callback context | Allowed APIs |
| --- | --- | --- | --- |
| **Default (ISR-dispatch)** | App never calls `rtos_timer_task_init()` | SysTick ISR | ISR-safe variants only (`rtos_semaphore_signal_from_isr`, `rtos_queue_send_from_isr`, `rtos_task_notify_from_isr`, `rtos_event_group_set_bits_from_isr`). Must NOT call blocking APIs. Keep short to avoid tick jitter. |
| **Task-dispatch** | App calls `rtos_timer_task_init()` before `rtos_start_scheduler()` | Dedicated `TimerSvc` task at `RTOS_CONFIG_TIMER_TASK_PRIORITY` (default MAX-2) | Anything — including `rtos_mutex_lock`, `rtos_delay_ms`, blocking queue/semaphore. A slow callback only delays subsequent dispatches from the same task; SysTick keeps running. |

Activation is purely runtime: the dispatcher always lives in the kernel and routes via `rtos_timer_dispatch_from_isr()`. Before init it calls the callback synchronously in the ISR; after init it enqueues a (callback, handle, param) snapshot onto a single-producer/single-consumer ring of `RTOS_CONFIG_TIMER_TASK_QUEUE_LENGTH` entries (default 8) and signals the timer task. Ring overflow is counted in `g_timer_dispatch_overflow_count` and the dispatch is dropped — size the queue for your worst-case burst.

```c
/* App opt-in to task-context callbacks: */
int main(void) {
    rtos_init();
    rtos_timer_task_init();         /* NEW — spawns TimerSvc task */
    rtos_task_create(...);
    rtos_start_scheduler();
}
```

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
- Separate ISR-safe variants (`_from_isr`) — the task-context APIs are NOT ISR-safe

**API**:

```c
// Task-context send: rtos_task_notify / rtos_task_notify_give
rtos_task_notify(target_task, 0x01, RTOS_NOTIFY_ACTION_SET_BITS);

// ISR-context send: rtos_task_notify_from_isr / rtos_task_notify_give_from_isr
//   Uses an ISR-local critical section (does not touch g_critical_nesting)
//   and pends PendSV directly so a higher-priority wake fires after the ISR
//   tail-chains out.
rtos_task_notify_from_isr(target_task, 0x01, RTOS_NOTIFY_ACTION_SET_BITS);
rtos_task_notify_give_from_isr(target_task);

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

## Scheduler Control (Suspend / Resume)

`rtos_scheduler_suspend()` / `rtos_scheduler_resume()` form a nested pair for
atomic multi-step updates that must not be interrupted by a context switch
— building a list of task handles, mass-prioritising a worker pool, etc.
While suspended:

- **Tick advance, timer expiry, and delayed-task wake still run.** Only the
  context-switch side-effect is deferred — the running task keeps the CPU.
- Wake paths (semaphore/queue/notify/mutex unlock) still mark target tasks
  READY but skip the would-be `rtos_yield()`. The deferred switch is fired
  on the `rtos_scheduler_resume()` call that drops the nesting count to zero.

```c
rtos_scheduler_suspend();
/* atomic multi-step update — no preemption can swap us out here */
build_consistent_state();
/* if resume drained a deferred switch, the higher-prio task runs right now */
bool fired = rtos_scheduler_resume();
```

> **Do not call blocking RTOS APIs while suspended.** `rtos_delay_ms`,
> `rtos_mutex_lock` with a non-zero timeout, blocking semaphore/queue waits
> — nothing else can run to wake the caller, so the system hangs. See
> [tests/integration/test_scheduler_suspend_suite.c](tests/integration/test_scheduler_suspend_suite.c)
> for the behavioural contract.

## Application Hooks

Weak-symbol callbacks declared in [include/rtos_hooks.h](include/rtos_hooks.h)
let the application observe or override key kernel events without modifying
the kernel. Defaults are no-ops; provide a same-named strong definition in
application code and the linker prefers it. Kernel-level `KLOG` still fires
at every hook site, so overrides are additive — turning a hook off does not
suppress kernel diagnostics.

| Hook | Fired from | Default | Typical override |
| --- | --- | --- | --- |
| `rtos_application_idle_hook()` | Idle task loop, once per iteration | no-op | feed watchdog, low-power tuning |
| `rtos_application_tick_hook(tick)` | SysTick ISR, after `tick_count++` | no-op | scheduler telemetry, soft timekeeping |
| `rtos_application_stack_overflow_hook(task)` | `rtos_task_check_stack()` AND every context switch when `RTOS_CONFIG_AUTO_STACK_CHECK_ON_SWITCH=1` | no-op | halt, log, kill the offender |
| `rtos_application_malloc_failed_hook(size, heap)` | `rtos_malloc_from()` just before returning NULL | no-op | record OOM telemetry; caller still sees NULL |
| `rtos_application_deadlock_hook(waiter, mutex)` | `rtos_mutex_lock()` when the lock would close a wait-for cycle | no-op | log graph, halt; the caller receives `RTOS_MUTEX_ERR_DEADLOCK` |

```c
/* Application TU — strong overrides shadow the weak kernel defaults. */
void rtos_application_stack_overflow_hook(rtos_task_handle_t task) {
    KLOGE("App", "Stack overflow in task %s", task->name);
    RTOS_ASSERT(0);
}
```

Validation: [test_hooks_suite](tests/integration/test_hooks_suite.c) exercises
the four event-driven hooks; the deadlock hook is covered by
`mutex:lock_detects_deadlock_cycle` in [test_mutex_suite](tests/integration/test_mutex_suite.c).

## ISR-Safe Sync APIs

Every wake-from-interrupt path has a `_from_isr` sibling that uses an
ISR-local critical section (`rtos_port_enter_critical_from_isr`) — it never
touches the task-context `g_critical_nesting` counter and pends PendSV via
`rtos_port_yield()` so the waking task runs after the ISR tail-chains out.

| Task-context API | ISR-context sibling |
| --- | --- |
| `rtos_semaphore_signal` | `rtos_semaphore_signal_from_isr` |
| `rtos_queue_send` (blocking) | `rtos_queue_send_from_isr` (never blocks → `RTOS_ERROR_FULL`) |
| `rtos_queue_receive` (blocking) | `rtos_queue_receive_from_isr` (never blocks → `RTOS_ERROR_EMPTY`) |
| `rtos_task_notify` | `rtos_task_notify_from_isr` |
| `rtos_task_notify_give` | `rtos_task_notify_give_from_isr` |
| `rtos_event_group_set_bits` | `rtos_event_group_set_bits_from_isr` |

> **Hard rules**: never call a task-context API from an ISR (the
> `g_critical_nesting` counter would underflow on return) and never call a
> `_from_isr` API from a task (the wake will use the wrong critical-section
> style and may miss BASEPRI restoration). The [test_isr_safe_suite](tests/integration/test_isr_safe_suite.c)
> uses a 1-tick one-shot timer as a deterministic ISR source and verifies
> wake delivery + payload integrity for every primitive above.

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
│   ├── KARTOS.h           # Main RTOS header (init, start, delay, yield, scheduler suspend/resume)
│   ├── config.h           # Configuration defaults
│   ├── task.h             # Task management API + notify (task and *_from_isr variants)
│   ├── scheduler.h        # Scheduler interface
│   ├── mutex.h            # Mutex API (incl. ERR_DEADLOCK / ERR_RECURSION)
│   ├── semaphore.h        # Semaphore API (incl. *_from_isr)
│   ├── queue.h            # Queue API (incl. *_from_isr)
│   ├── event_group.h      # Event group API
│   ├── timer.h            # Software timer API + rtos_timer_task_init() opt-in
│   ├── memory.h           # Memory API
│   ├── profiling.h        # Profiling API
│   ├── rtos_hooks.h       # Weak application hooks: idle/tick/stack/malloc/deadlock
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
│   │   ├── kernel.c       # Kernel init, tick handler, scheduler suspend/resume, task unblock (task + ISR)
│   │   ├── hooks.c        # Weak default no-op application hooks
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
│   │   ├── timer_list.c   # Active timer list + tick-driven dispatch
│   │   └── timer_task.c   # Optional task-context dispatcher (rtos_timer_task_init)
│   ├── logging/           # Logging subsystem
│   │   ├── log.c/h        # Shared log_level_t enum + unified log_init / log_flush_drain dispatch
│   │   ├── klog.c/h       # High-performance deferred kernel logger (defer-the-format)
│   │   ├── ulog.c/h       # User-facing printf-style logger (format-up-front)
│   │   ├── log_flush_task.c/h  # Idle-priority task; calls log_flush_drain() each period
│   │   ├── uart_tx.h      # UART transport interface used by both backends (.c lives in BSP)
│   │   └── log_common.h   # Shared path utilities: LOG_BASENAME (compile-time) and log_basename (runtime)
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
│   │   ├── test_heap_allocator_suite.c
│   │   ├── test_hooks_suite.c
│   │   ├── test_scheduler_suspend_suite.c
│   │   ├── test_isr_safe_suite.c
│   │   ├── test_timer_task_suite.c
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

/* Software timer service task (only consumed after rtos_timer_task_init()) */
#define RTOS_CONFIG_TIMER_TASK_PRIORITY     (RTOS_MAX_TASK_PRIORITIES - 2U)
#define RTOS_CONFIG_TIMER_TASK_STACK_SIZE   RTOS_DEFAULT_TASK_STACK_SIZE
#define RTOS_CONFIG_TIMER_TASK_QUEUE_LENGTH (8U)    // pending dispatches

/* Logging */
#define RTOS_UART_BAUD_RATE        (921600U)  // UART baud rate for log output
#define RTOS_KLOG_ENABLED          (1U)       // kernel logger; rtos_init() auto-inits the backend
#define RTOS_KLOG_MIN_LEVEL        (3U)       // compile-time floor: 0=FAULT … 5=TRACE (seeds klog_verbosity)
#define RTOS_ULOG_ENABLED          (1U)       // user logger;   rtos_init() auto-inits the backend
#define RTOS_ULOG_MIN_LEVEL        (3U)       // compile-time floor: 0=FAULT … 5=TRACE (seeds ulog_verbosity)
#define LOG_FLUSH_TASK_STACK_SIZE  (2048U)    // shared stack for the auto-created log_flush_task

/* Debug */
#define RTOS_ASSERT_ENABLED                     (1U)
#define RTOS_ENABLE_STACK_OVERFLOW_CHECK        (1U)
#define RTOS_CONFIG_AUTO_STACK_CHECK_ON_SWITCH  (1U)  // defaults to RTOS_ENABLE_STACK_OVERFLOW_CHECK
```

> The `RTOS_ASSERT_CRITICAL` alias was removed — it was a no-op when
> `RTOS_ASSERT_ENABLED=0`, which silently lost the "critical" promise. Use
> `RTOS_ASSERT()` directly and gate calls with the build's assertion-enabled
> macro if you need a different policy.
>
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

Each task stack is sealed at its base with the canary value `0xC0DEC0DE`.
A mismatch means the task wrote past its lowest address — i.e. it overran.
Detection runs in two places:

- **Automatic (every context switch)**, gated by `RTOS_CONFIG_AUTO_STACK_CHECK_ON_SWITCH` (defaults to `RTOS_ENABLE_STACK_OVERFLOW_CHECK`). The kernel checks the outgoing task's canary inside `rtos_kernel_switch_context()` and fires `rtos_application_stack_overflow_hook(task)` on mismatch.
- **Manual** via `rtos_task_check_stack()` for on-demand polling. Pass `NULL` to sweep every task in the pool, or a specific handle to check one.

```c
// Check a specific task
if (rtos_task_check_stack(my_task_handle)) {
    ulog_error("Task stack overflow!");
}

// Sweep all tasks
if (rtos_task_check_stack(NULL)) {
    ulog_error("Stack overflow detected somewhere!");
}

// Application-level reaction (weak default is no-op):
void rtos_application_stack_overflow_hook(rtos_task_handle_t task) {
    KLOGE("App", "STACK OVERFLOW id=%u name=%s", task->task_id, task->name);
    RTOS_ASSERT(0);
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

KARTOS features a dual-backend logging architecture: a fast ISR-safe kernel
logger (KLog) and a printf-style application logger (ULog). Both share the
same level enum, the same `.noinit` runtime-verbosity pattern, and the same
flush task — only the producer strategy differs.

### Unified Core (`log.h`)

[src/logging/log.h](src/logging/log.h) defines the shared layer:

- **One `log_level_t` enum** — `LOG_LEVEL_FAULT … LOG_LEVEL_TRACE` (0–5). Both
  backends use this enum; the old `KLOG_LEVEL_*` and `ULOG_LEVEL_*` spellings
  remain as aliases for source compatibility.
- **`log_init()`** — single entry point called from `rtos_init()`. Brings up
  the UART transport (`log_uart_init()`) and initialises whichever backends
  are enabled via `RTOS_KLOG_ENABLED` / `RTOS_ULOG_ENABLED`. Application code
  never calls `klog_init()`, `ulog_init()`, or `log_uart_init()` directly —
  if logging is on in config, `rtos_init()` wires it up end-to-end.
- **`log_flush_drain()`** — single drain entry point. The auto-created
  `log_flush_task` (`LOG_FLUSH_TASK_STACK_SIZE` bytes, idle priority) wakes
  every 100 ms, processes incoming UART commands, then calls
  `log_flush_drain()` to emit pending records from every enabled backend.
- **Shared path helpers** — [log_common.h](src/logging/log_common.h) provides
  `LOG_BASENAME(literal)` (compile-time, zero cost) and `log_basename(path)`
  (runtime, handles `/` and `\`) used by both backends and the test runner.

Two ring buffers and two verbosity globals are kept independent on purpose:
you can silence ULog while keeping `KLOGT` traces flowing during kernel
debugging, or vice versa.

### Kernel Logger (KLog)

[src/logging/klog.c](src/logging/klog.c) targets high-frequency tracing from
any context, including interrupts.

- **Defer-the-format** — `KLOG*()` writes a 24-byte `log_packet_t` (timestamp,
  module tag, file/line, format-string pointer, four raw `uint32_t` args)
  into a ring buffer. The producer never calls `snprintf`.
- **ISR-safe** — guarded with `__disable_irq`/`__set_PRIMASK`; never blocks,
  never allocates. Drops silently on full buffer.
- **Background formatting** — `klog_drain_and_emit()` runs in the flush task
  and produces lines like `00001204 [IdleTask ] [Kernel ] I kernel.c:45 |
  Entering low power mode` before handing them to UART.
- **Runtime verbosity** — `klog_verbosity` lives in `.noinit` and survives
  `NVIC_SystemReset()`. The compile-time floor `RTOS_KLOG_MIN_LEVEL` seeds it
  on first boot or after corruption. Live-patch it on a running board with
  `python -m kartos verbosity DEBUG` (default `--target=klog`).

**API**:

```c
// Shorthand macros automatically capture file/line context.
// Level filtering happens at the macro site against klog_verbosity —
// filtered calls compile to nothing in the hot path.
KLOGI("Queue", "Created queue %s with %d items", q_name, q_size);
KLOGE("Scheduler", "Failed to start task %d", task_id);
KLOGT("Kernel", "CtxSwitch from=%s to=%s", from_name, to_name);
```

### User Logger (ULog)

[src/logging/ulog.c](src/logging/ulog.c) is the printf-style counterpart for
application code where the string/float ergonomics matter.

- **Format up front** — `ulog_*()` runs `vsnprintf` into a stack scratch
  buffer, then copies raw bytes into the ULog ring buffer.
- **Not ISR-safe** — uses a critical section around the ring-buffer write;
  intended for task context only.
- **Symmetric runtime control** — `ulog_verbosity` lives in `.noinit` (same
  reset-survival pattern as `klog_verbosity`). Compile-time floor:
  `RTOS_ULOG_MIN_LEVEL`. Live-patch with
  `python -m kartos verbosity DEBUG -t ulog`, or `-t both` to patch both
  backends in one shot.
- **Macro-site filtering** — every `ulog_*()` macro early-outs against
  `ulog_verbosity` before pushing varargs, so filtered calls have zero
  call-site cost.

**API** — six per-level macros mirroring `log_level_t`:

```c
ulog_fault("Watchdog trip — last task: %s", last_task_name);
ulog_error("Failed to read sensor: %d", error_code);
ulog_warn ("Battery low: %u mV", millivolts);
ulog_info ("Connection established to %s", ip_addr);
ulog_debug("Sensor sample %u: raw=%d cal=%d", n, raw, calibrated);
ulog_trace("Frame parsed: type=0x%02x len=%u", frame_type, len);
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
