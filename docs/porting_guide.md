# VRTOS Porting Guide

How to add support for a new chip / architecture.

## Directory Structure

```
src/port/
├── common/
│   ├── port_common.h       ← required-macro contract (shared)
│   └── port_utils.c        ← optional helpers (shared)
└── <arch>/                  ← one directory per architecture
    ├── port_priv.h          ← chip-specific constants
    └── port.c               ← rtos_port.h implementation
```

```
config/
├── rtos_config_template.h   ← skeleton for new boards
└── <board>/
    ├── rtos_config.h         ← board-specific RTOS overrides
    ├── memory_map.h          ← flash / RAM layout
    └── clock_config.h        ← clock frequencies
```

## Step-by-Step

### 1. Create the port directory

```
src/port/<arch>/
├── port_priv.h
└── port.c
```

### 2. Define required macros in `port_priv.h`

`port_common.h` enforces these at compile time — a missing macro triggers `#error`:

| Macro | Description | Example (Cortex-M4F) |
|---|---|---|
| `PORT_STACK_ALIGNMENT` | Stack byte alignment | `8` |
| `PORT_INITIAL_EXC_RETURN` | Initial LR / return-to-thread value | `0xFFFFFFFD` |
| `PORT_HAS_FPU` | Hardware FPU present (0 or 1) | `1` |
| `PORT_MAX_INTERRUPT_PRIORITY` | BASEPRI threshold for critical sections | `PORT_IRQ_PRIORITY_KERNEL` |
| `PORT_INITIAL_XPSR` | Initial xPSR value | `0x01000000` |

Interrupt priority constants should also be defined in `port_priv.h`:

| Macro | Description | Example (Cortex-M4F) |
|---|---|---|
| `PORT_IRQ_PRIORITY_CRITICAL` | Highest, never masked by RTOS | `0x00` |
| `PORT_IRQ_PRIORITY_HIGH` | Can preempt RTOS (UART, SPI) | `0x40` |
| `PORT_IRQ_PRIORITY_KERNEL` | SysTick level | `0x80` |
| `PORT_IRQ_PRIORITY_LOW` | Non-critical peripherals | `0xC0` |
| `PORT_IRQ_PRIORITY_PENDSV` | Lowest — context switch | `0xF0` |

### 3. Implement `rtos_port.h` functions in `port.c`

Every port must implement these functions (declared in `include/VRTOS/rtos_port.h`):

| Function | Purpose |
|---|---|
| `rtos_port_init()` | Configure interrupt priorities, enable FPU stacking, initialise critical-section state |
| `rtos_port_start_systick()` | Start the system tick timer at `RTOS_TICK_RATE_HZ` |
| `rtos_port_start_first_task()` | Set PSP, trigger the first context restore (never returns) |
| `rtos_port_init_task_stack()` | Build the initial exception + register frame on a task's stack |
| `rtos_port_enter_critical()` | Mask kernel-level interrupts (nestable) |
| `rtos_port_exit_critical()` | Unmask on final exit (nestable) |
| `rtos_port_enter_critical_from_isr()` | ISR-safe critical section entry |
| `rtos_port_exit_critical_from_isr()` | ISR-safe critical section exit |
| `rtos_port_yield()` | Trigger a context switch (e.g. pend PendSV) |
| `rtos_port_systick_handler()` | Called from the tick ISR — forwards to `rtos_kernel_tick_handler()` |

Your `port.c` must also provide the ISR entry points for context switching (e.g. `PendSV_Handler`, `SVC_Handler` on ARM).

### 4. Create board config

Copy `config/rtos_config_template.h` to `config/<board>/rtos_config.h` and uncomment the values you need to override. `config.h` wraps every default in `#ifndef` guards, so your overrides take priority:

```c
#ifndef RTOS_CONFIG_BOARD_H
#define RTOS_CONFIG_BOARD_H

#include "memory_map.h"
#include "clock_config.h"

#define RTOS_SYSTEM_CLOCK_HZ (84000000U)
#define RTOS_MAX_TASKS       (10U)
/* ... only override what differs from defaults ... */

#endif /* RTOS_CONFIG_BOARD_H */
```

Add `memory_map.h` (flash/SRAM bounds) and `clock_config.h` (clock aliases) as needed.

### 5. Update `platformio.ini`

Add a new port section with `build_flags` and `port_src_filter`, then create a board environment:

```ini
; --- Port Layer ---
[<arch>]
build_flags =
    -I src/port/<arch>/
    ; add arch-specific compiler flags (e.g. -mfpu, -mfloat-abi)
port_src_filter =
    -<port/>
    +<port/common/>
    +<port/<arch>/>

; --- Board Environment ---
[env:<board>]
platform = ...
board = ...
framework = ...
build_src_filter = +<*> -<examples/> +<examples/basic_blinky/> ${<arch>.port_src_filter}
build_flags =
    ${<arch>.build_flags}
    -I config/<board>/
    -D <BOARD_DEFINE>
    ; ...remaining flags...
```

The `port_src_filter` ensures only the selected port's source files are compiled.
Adding a second port directory (e.g. `src/port/cortex_m0/`) will not cause
duplicate symbol errors, because each environment explicitly selects its port.

### 6. Build and verify

```bash
pio run -e <board>
```

At minimum, build the `fpu_context_test` example (if the chip has an FPU) or `basic_blinky` to confirm the port links and runs.

## FPU Notes

- If `PORT_HAS_FPU` is `1`, the PendSV handler conditionally saves/restores S16-S31 and the port init enables lazy stacking.
- If `PORT_HAS_FPU` is `0`, FPU code is compiled out via `#if PORT_HAS_FPU` guards in `port.c`. No FPU flags are needed in the build.
- Use `softfp` ABI when the framework libraries were compiled without hard-float calling convention.

## Reference

The Cortex-M4F port in `src/port/cortex_m4/` is the reference implementation.
