# STM32F446RE RTOS Implementation

A custom Real-Time Operating System (RTOS) implementation for the STM32F446RE Nucleo board, built from scratch as an educational and production-grade project.

## Project Overview

This RTOS provides essential real-time scheduling capabilities with a clean, modular architecture. The current MVP includes:

- **Rate Monotonic Scheduler (RMS)** with strict priority-based preemption
- **Task Management** with creation and state management
- **System Tick** with configurable frequency (default 1ms)
- **Delay Functions** for task timing control
- **Context Switching** optimized for Cortex-M4 architecture
- **Static Memory Management** with stack allocation
- **Porting Layer** for Cortex-M4
- **Comprehensive Debugging** with logging system

## Architecture

The RTOS follows a layered architecture with clear separation of concerns:

```
Application Layer   (User Tasks)
    ↓
RTOS API Layer      (Public Interface)
    ↓
RTOS Core Layer     (Scheduler, Tasks, Memory)
    ↓
Kernel Core Layer   (Tick, Context Switch)
    ↓
Porting Layer       (Cortex-M4 Specific)
    ↓
Hardware Layer      (STM32F446RE HAL)
```

## Features

### Current (MVP)
- Task creation and management
- Strict Rate Monotonic Scheduling (RMS)
- Preemptive priority-based scheduling
- System tick and timing services
- Task delay functions (ms and ticks)
- Cortex-M4 context switching
- Critical section management
- Static stack allocation
- Extensive logging system
- HardFault diagnostics
- Priority inversion prevention

### Planned (Future Releases)
- Mutexes (binary and recursive)
- Semaphores (counting and binary)
- Message queues
- Software timers
- Event flags
- Memory pools
- Tickless idle mode
- Power management

## Getting Started

### Prerequisites
- PlatformIO IDE or PlatformIO Core
- STM32F446RE Nucleo board
- ARM GCC toolchain (automatically handled by PlatformIO)

### Building the Project

1. Clone the repository:
```bash
git clone <repository-url>
cd rtos-stm32f446re
```

2. Build the project:
```bash
pio build
```

3. Upload to the board:
```bash
pio upload
```

4. Monitor serial output:
```bash
pio device monitor
```

### Running the Example

The included blinky example demonstrates basic RTOS functionality:
- Creates a task that blinks the onboard LED (PA5)
- Uses RTOS delay functions for timing
- Demonstrates task scheduling and context switching

The LED should blink at 5Hz if the RTOS is working correctly.

## Configuration

RTOS behavior can be customized through configuration files:

- `include/VRTOS/config.h` - Generic RTOS configuration
- `config/stm32f446re/rtos_config.h` - STM32F446RE specific settings

Key configuration parameters:
```c
#define RTOS_SYSTEM_CLOCK_HZ       (84000000U)   // 84MHz
#define RTOS_TICK_RATE_HZ          (1000U)       // 1ms tick
#define RTOS_MAX_TASKS             (10U)         // Max tasks
#define RTOS_TIME_SLICE_MS         (10U)         // Round-robin slice
```

## API Reference

### Task Management
```c
// Create a new task
rtos_status_t rtos_task_create(rtos_task_function_t task_function,
                              const char *name,
                              rtos_stack_size_t stack_size,
                              void *parameter,
                              rtos_priority_t priority,
                              rtos_task_handle_t *task_handle);

// Get current task
rtos_task_handle_t rtos_task_get_current(void);
```

### Timing Services
```c
// Delay in milliseconds
void rtos_delay_ms(uint32_t ms);

// Delay in ticks
void rtos_delay_ticks(rtos_tick_t ticks);

// Get current tick count
rtos_tick_t rtos_get_tick_count(void);
```

### System Control
```c
// Initialize RTOS
rtos_status_t rtos_init(void);

// Start scheduler (never returns)
rtos_status_t rtos_start_scheduler(void);

// Force task yield
void rtos_yield(void);
```

## Memory Usage

Typical memory usage for the MVP:
- **Flash**: ~8-12KB (depending on optimization)
- **RAM**: ~2-4KB (plus task stacks)
- **Stack per task**: 256-768 bytes (configurable)

## Development Guidelines

### Adding New Features
1. Follow the existing modular structure
2. Add public APIs to appropriate header files
3. Implement private functions in corresponding .c files
4. Update configuration options if needed
5. Add documentation and examples

### Porting to Other Hardware
1. Create new port directory (e.g., `port/cortex_m3/`)
2. Implement the porting layer interface (`rtos_port.h`)
3. Update configuration for target hardware
4. Test thoroughly with target-specific examples

## Debugging

### Common Issues
1. **Hard Fault**: Check stack sizes and overflow protection
2. **Tasks not switching**: Verify SysTick and PendSV configuration  
3. **Timing issues**: Check system clock configuration
4. **Memory corruption**: Enable stack overflow checking

### Debug Features
- Detailed task state logging
- HardFault register dumps
- Stack initialization patterns
- Ready list monitoring
- Task transition tracing

## Common Issues & Solutions
1. **Priority Inversion**:
    - Verify idle task has lowest priority
    - Check preemption logic in tick handler
3. **Context Switch Failures**:
    - Confirm PendSV/SVC handler alignment
    - Validate stack pointer initialization
3. **Task Starvation**:
    - Ensure delayed tasks are readded to ready list
    - Verify RMS priority assignment

## Development Status

### Current Focus Areas:

- Robust context switching validation
- Scheduling edge case handling
- Memory protection enhancements
- System stability under load
### Next Steps:
- Add mutex/semaphore support
- Implement software timers
- Add power management features