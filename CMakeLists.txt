cmake_minimum_required(VERSION 3.21)

# KARTOS_BOARD selects boards/<name>/board.cmake at configure time.
# Override via CMakePresets or: cmake -DKARTOS_BOARD=stm32f401re_nucleo ...
set(KARTOS_BOARD "stm32f446re_nucleo" CACHE STRING "Target board name")

project(kartos LANGUAGES C ASM)

# ── Optional link-time optimization ─────────────────────────────────────────
option(KARTOS_LTO "Enable link-time optimization (-flto) across all targets" OFF)
if(KARTOS_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    message(STATUS "KARTOS_LTO=ON — building all targets with -flto")
endif()

# ── 1. Board — sets KARTOS_* variables, creates kartos::board INTERFACE ─────
include("${CMAKE_SOURCE_DIR}/boards/${KARTOS_BOARD}/board.cmake")

# ── 2. MCU family — creates kartos::mcu_<family> STATIC (HAL + CMSIS) ───────
#    Must come BEFORE arch so that kartos::mcu_* is defined when arch links it.
include("${CMAKE_SOURCE_DIR}/mcus/${KARTOS_MCU_FAMILY}/family.cmake")

# ── 3. Arch — creates kartos::arch_<arch> STATIC (port.c) ───────────────────
add_subdirectory("arch/${KARTOS_ARCH}")

# Retroactively give the MCU library the arch's CPU/FPU compile options so
# HAL sources are compiled for the correct core (must come after arch/ is added).
target_link_libraries(kartos_mcu_${KARTOS_MCU_FAMILY} PRIVATE
    kartos::arch_${KARTOS_ARCH})

# ── 4. Kernel — creates kartos::kernel STATIC ────────────────────────────────
add_subdirectory(src)

# ── 4b. Test framework — creates kartos::test_framework STATIC ──────────────
add_subdirectory(tests/framework)

# ── 5. Linker script generation ──────────────────────────────────────────────
include(cmake/kartos_linker_script.cmake)

# ── 6. build_info.h generation ───────────────────────────────────────────────
include(cmake/kartos_build_info.cmake)

# ── 7. Variant helper function ───────────────────────────────────────────────
include(cmake/kartos_add_variant.cmake)

# ── 8. Register all build variants ───────────────────────────────────────────
include(cmake/variants.cmake)
