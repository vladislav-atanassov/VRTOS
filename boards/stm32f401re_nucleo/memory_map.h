#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#ifndef LINKER_SCRIPT
#include <stdint.h>
#endif

/* Strip UL/U suffixes when consumed by the GNU linker preprocessor. */
#ifdef LINKER_SCRIPT
#define _UL(x) (x)
#else
#define _UL(x) (x##UL)
#endif

/* Flash Memory Layout — STM32F401RE: 512 KB */
#define FLASH_BASE_ADDR _UL(0x08000000)
#define FLASH_SIZE      (_UL(512) * _UL(1024))
#define FLASH_END_ADDR  (FLASH_BASE_ADDR + FLASH_SIZE - _UL(1))

/* SRAM Memory Layout — STM32F401RE: 96 KB */
#define SRAM_BASE_ADDR _UL(0x20000000)
#define SRAM_SIZE      (_UL(96) * _UL(1024))
#define SRAM_END_ADDR  (SRAM_BASE_ADDR + SRAM_SIZE - _UL(1))

/* Stack and Heap Configuration */
#define MAIN_STACK_SIZE  _UL(2048)
#define MAIN_STACK_START (SRAM_END_ADDR + _UL(1))
#define MAIN_STACK_END   (MAIN_STACK_START - MAIN_STACK_SIZE)

/* Minimum linker-level heap reservation. */
#define RTOS_MIN_HEAP_SIZE _UL(0x200)

/* RTOS Memory Allocation */
#define RTOS_MEMORY_START (SRAM_BASE_ADDR + _UL(0x1000))
#define RTOS_MEMORY_SIZE  (SRAM_SIZE - _UL(0x2000))

#endif /* MEMORY_MAP_H */
