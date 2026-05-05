#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

/* Copy to boards/<your_board>/memory_map.h and fill in your MCU's sizes.
 * Shared between C source and linker.ld.in — the _UL() boilerplate below
 * is REQUIRED in every memory_map.h.
 * See boards/stm32f446re_nucleo/memory_map.h for a real example.
 *
 * Common STM32F4 Flash / SRAM sizes:
 *   STM32F401RE —  512 KB Flash,  96 KB SRAM
 *   STM32F411RE —  512 KB Flash, 128 KB SRAM
 *   STM32F446RE —  512 KB Flash, 128 KB SRAM
 *   STM32F407VG — 1024 KB Flash, 192 KB SRAM */

#ifndef LINKER_SCRIPT
#include <stdint.h>
#endif

/* Required boilerplate: strips UL suffixes for the GNU linker preprocessor. */
#ifdef LINKER_SCRIPT
#define _UL(x) (x)
#else
#define _UL(x) (x##UL)
#endif

/* ======================== Flash ========================================= */
// #define FLASH_BASE_ADDR _UL(0x08000000)          /* same on all STM32 */
// #define FLASH_SIZE      (_UL(512) * _UL(1024))   /* KB — set for your MCU */
// #define FLASH_END_ADDR  (FLASH_BASE_ADDR + FLASH_SIZE - _UL(1))

/* ======================== SRAM ========================================== */
// #define SRAM_BASE_ADDR  _UL(0x20000000)          /* same on all Cortex-M STM32 */
// #define SRAM_SIZE       (_UL(128) * _UL(1024))   /* KB — set for your MCU */
// #define SRAM_END_ADDR   (SRAM_BASE_ADDR + SRAM_SIZE - _UL(1))

/* ======================== Stack ========================================= */
// #define MAIN_STACK_SIZE  _UL(2048)               /* 2 KB; increase if HardFaulting */
// #define MAIN_STACK_START (SRAM_END_ADDR + _UL(1))
// #define MAIN_STACK_END   (MAIN_STACK_START - MAIN_STACK_SIZE)

/* ======================== Heap ========================================== */
/* Linker errors here mean RAM is exhausted after .bss + stack + heap. */
// #define RTOS_MIN_HEAP_SIZE _UL(0x200)

/* ======================== RTOS Memory Pool ============================== */
// #define RTOS_MEMORY_START (SRAM_BASE_ADDR + _UL(0x1000))
// #define RTOS_MEMORY_SIZE  (SRAM_SIZE - _UL(0x2000))

#endif /* MEMORY_MAP_H */
