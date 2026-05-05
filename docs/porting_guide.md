# KARTOS Porting Guide

How to add support for a new chip / architecture.

## Directory Structure

```
arch/
├── common/
│   └── port_common.h       ← required-macro contract (shared)
└── <arch>/                  ← one directory per architecture
    ├── port_priv.h          ← chip-specific constants
    └── port.c               ← rtos_port.h implementation
```

```
boards/
├── templates/               ← skeleton files for new boards
│   ├── rtos_config_template.h
│   ├── clock_config_template.h
│   └── memory_map_template.h
└── <board>/
    ├── board.cmake          ← CMake target/toolchain settings
    ├── rtos_config.h        ← board-specific RTOS overrides
    ├── memory_map.h         ← flash / RAM layout
    ├── clock_config.h       ← clock frequencies
    ├── linker.ld.in         ← preprocessable linker script template
    └── openocd.cfg          ← OpenOCD flash/debug configuration
```

## Step-by-Step

### 1. Create the port directory

```
arch/<arch>/
├── port_priv.h
└── port.c
```

### 2. Define required macros in `port_priv.h`

`arch/common/port_common.h` enforces these at compile time — a missing macro triggers `#error`:

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

Every port must implement these functions (declared in `include/rtos_port.h`):

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

Copy `boards/templates/rtos_config_template.h` to `boards/<board>/rtos_config.h` and uncomment the values you need to override. `config.h` wraps every default in `#ifndef` guards, so your overrides take priority:

```c
#ifndef RTOS_CONFIG_BOARD_H
#define RTOS_CONFIG_BOARD_H

#include "clock_config.h"
#include "memory_map.h"

#define RTOS_SYSTEM_CLOCK_HZ (84000000U)
#define RTOS_MAX_TASKS       (10U)
/* ... only override what differs from defaults ... */

#endif /* RTOS_CONFIG_BOARD_H */
```

Also copy `boards/templates/clock_config_template.h` → `boards/<board>/clock_config.h` and `boards/templates/memory_map_template.h` → `boards/<board>/memory_map.h`, filling in your MCU's clock and memory sizes.

`memory_map.h` must wrap integer literals in the `_UL(x)` macro so the same file can be consumed by both C source and the linker preprocessor. See `boards/stm32f446re_nucleo/memory_map.h` for the reference pattern.

### 4a. Create the linker script template

Copy `boards/stm32f446re_nucleo/linker.ld.in` to `boards/<board>/linker.ld.in` and update the `#include` path and MEMORY region sizes to match your MCU. The template is preprocessed by `arm-none-eabi-gcc -E -P -x c-header -D LINKER_SCRIPT -I boards/<board>/` at configure time via `cmake/kartos_linker_script.cmake`.

### 5. Create the CMake board file and register a preset

**`boards/<board>/board.cmake`** — set the five required variables:

```cmake
set(KARTOS_ARCH        cortex_m4)
set(KARTOS_MCU_FAMILY  stm32f4)
set(KARTOS_MCU_PART    STM32F4xxYY)       # e.g. STM32F446xE
set(KARTOS_BOARD_NAME  <board>)
set(KARTOS_STARTUP_FILE "${CMAKE_SOURCE_DIR}/vendor/stm32cubef4/Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f4xxyy.s")
```

**`CMakePresets.json`** — add a configure preset and at least one build preset:

```json
{
  "name": "<board>",
  "inherits": "base",
  "binaryDir": "${sourceDir}/build/<board>",
  "cacheVariables": {
    "KARTOS_BOARD_FILE": "${sourceDir}/boards/<board>/board.cmake"
  }
}
```

Then add a build preset that inherits `"<board>"` (or the appropriate configure preset) in the `buildPresets` array:

```json
{
  "name": "<board>-basic_blinky",
  "configurePreset": "<board>",
  "targets": ["basic_blinky"]
}
```

Add variants for each test or example you want to expose for this board in `cmake/variants.cmake` using `kartos_add_variant()`.

### 6. Build and verify

```bash
# Configure
cmake --preset <board>

# Build
cmake --build --preset <board>-basic_blinky

# Flash
cmake --build --preset flash-<board>-basic_blinky
```

At minimum, build and flash `basic_blinky` to confirm the port links and runs. If the chip has an FPU, also verify `fpu_context_test`.

## FPU Notes

- If `PORT_HAS_FPU` is `1`, the PendSV handler conditionally saves/restores S16-S31 and the port init enables lazy stacking.
- If `PORT_HAS_FPU` is `0`, FPU code is compiled out via `#if PORT_HAS_FPU` guards in `port.c`. No FPU flags are needed in the build.
- Use `softfp` ABI when the framework libraries were compiled without hard-float calling convention.

## Reference

The Cortex-M4F port in `arch/cortex_m4/` is the reference implementation. The STM32F401RE Nucleo board (`boards/stm32f401re_nucleo/`) is a minimal portability example — same arch and MCU family as the F446RE but with 96 KB SRAM instead of 128 KB, demonstrating how little changes between closely related boards.
