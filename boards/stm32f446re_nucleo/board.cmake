# Board descriptor for STM32F446RE Nucleo (64-pin).
#
# Sets KARTOS_* CMake variables consumed by the top-level CMakeLists and the
# cmake/ helper modules, then creates the kartos::board INTERFACE library that
# propagates board-specific compile definitions and include dirs to all targets.

# ── Portability axes ────────────────────────────────────────────────────────
set(KARTOS_ARCH       cortex_m4)
set(KARTOS_MCU_FAMILY stm32f4)

# ── Board identity ───────────────────────────────────────────────────────────
set(KARTOS_MCU_PART    STM32F446xx)
set(KARTOS_BOARD_NAME  stm32f446re_nucleo)
string(TOUPPER "${KARTOS_BOARD_NAME}" KARTOS_BOARD_NAME_UPPER)

# ── File paths ───────────────────────────────────────────────────────────────
# Startup file from the STM32CubeF4 submodule.
set(KARTOS_STARTUP_FILE
    "${CMAKE_SOURCE_DIR}/vendor/stm32cubef4/Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f446xx.s")

# Linker script template (preprocessed into ${CMAKE_BINARY_DIR}/linker.ld).
set(KARTOS_LINKER_SCRIPT_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/linker.ld.in")

# Directory that holds memory_map.h, clock_config.h, rtos_config.h,
# stm32f4xx_hal_conf.h — all resolved via a single -I flag.
set(KARTOS_BOARD_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# OpenOCD config used by flash-<variant> targets.
set(KARTOS_OPENOCD_CFG "${CMAKE_CURRENT_LIST_DIR}/openocd.cfg")

# ── INTERFACE library ────────────────────────────────────────────────────────
# Created here (before mcus/ and arch/ are processed) so that family.cmake and
# arch/cortex_m4/CMakeLists.txt can link against it immediately.
add_library(kartos_board INTERFACE)
add_library(kartos::board ALIAS kartos_board)

target_include_directories(kartos_board INTERFACE
    "${KARTOS_BOARD_INCLUDE_DIR}")

target_compile_definitions(kartos_board INTERFACE
    "${KARTOS_MCU_PART}"
    "RTOS_TARGET_${KARTOS_BOARD_NAME_UPPER}")

# ── Board Support Package (BSP) ──────────────────────────────────────────────
# Provides hardware_env (clock/LED bootstrap) and uart_tx (UART transport
# implementation). All variants link this; pure-kernel builds can opt out.
add_library(kartos_board_support STATIC
    ${CMAKE_CURRENT_LIST_DIR}/hardware_env.c
    ${CMAKE_CURRENT_LIST_DIR}/uart_tx.c)

target_include_directories(kartos_board_support
    PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(kartos_board_support
    PUBLIC
        kartos::board
        kartos::mcu_${KARTOS_MCU_FAMILY}
    PRIVATE
        kartos::kernel)

add_library(kartos::board_support ALIAS kartos_board_support)
