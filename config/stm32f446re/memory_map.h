/*******************************************************************************
 * File: config/stm32f446re/memory_map.h
 * Description: STM32F446RE Memory Layout Definitions
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdint.h>

/**
 * @file memory_map.h
 * @brief STM32F446RE Memory Layout Definitions
 *
 * This file defines the memory layout and addresses specific to the
 * STM32F446RE microcontroller.
 */

/* Flash Memory Layout */
#define FLASH_BASE_ADDR (0x08000000UL)
#define FLASH_SIZE      (512UL * 1024UL) /* 512KB */
#define FLASH_END_ADDR  (FLASH_BASE_ADDR + FLASH_SIZE - 1)

/* SRAM Memory Layout */
#define SRAM_BASE_ADDR (0x20000000UL)
#define SRAM_SIZE      (128UL * 1024UL) /* 128KB */
#define SRAM_END_ADDR  (SRAM_BASE_ADDR + SRAM_SIZE - 1)

/* Stack and Heap Configuration */
#define MAIN_STACK_SIZE  (2048UL) /* 2KB main stack */
#define MAIN_STACK_START (SRAM_END_ADDR + 1)
#define MAIN_STACK_END   (MAIN_STACK_START - MAIN_STACK_SIZE)

/* RTOS Memory Allocation */
#define RTOS_MEMORY_START (SRAM_BASE_ADDR + 0x1000) /* After globals */
#define RTOS_MEMORY_SIZE  (SRAM_SIZE - 0x2000)      /* Reserve space */

#endif /* MEMORY_MAP_H */
