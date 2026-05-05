#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#ifndef LINKER_SCRIPT
#include <stdint.h>
#endif

/**
 * @file memory_map.h
 * @brief STM32F446RE Memory Layout Definitions
 *
 * This file defines the memory layout and addresses specific to the
 * STM32F446RE microcontroller. It is shared between C source and the
 * linker template (ldscripts/stm32f446re.ld.in). When included from the
 * linker template, define LINKER_SCRIPT before including to strip the UL
 * integer suffixes that the GNU linker cannot parse.
 */

/* Strip UL/U suffixes when consumed by the GNU linker preprocessor. */
#ifdef LINKER_SCRIPT
#define _UL(x) (x)
#else
#define _UL(x) (x##UL)
#endif

/* Flash Memory Layout */
#define FLASH_BASE_ADDR _UL(0x08000000)
#define FLASH_SIZE      (_UL(512) * _UL(1024)) /* 512KB */
#define FLASH_END_ADDR  (FLASH_BASE_ADDR + FLASH_SIZE - _UL(1))

/* SRAM Memory Layout */
#define SRAM_BASE_ADDR _UL(0x20000000)
#define SRAM_SIZE      (_UL(128) * _UL(1024)) /* 128KB */
#define SRAM_END_ADDR  (SRAM_BASE_ADDR + SRAM_SIZE - _UL(1))

/* Stack and Heap Configuration */
#define MAIN_STACK_SIZE  _UL(2048)                     /* 2KB main stack */
#define MAIN_STACK_START (SRAM_END_ADDR + _UL(1))      /* top of RAM */
#define MAIN_STACK_END   (MAIN_STACK_START - MAIN_STACK_SIZE)

/* Minimum linker-level heap reservation (validates free RAM after .bss). */
#define RTOS_MIN_HEAP_SIZE _UL(0x200)

/* RTOS Memory Allocation */
#define RTOS_MEMORY_START (SRAM_BASE_ADDR + _UL(0x1000)) /* After globals */
#define RTOS_MEMORY_SIZE  (SRAM_SIZE - _UL(0x2000))      /* Reserve space */

#endif /* MEMORY_MAP_H */
