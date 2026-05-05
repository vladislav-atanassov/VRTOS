# kartos::arch_cortex_m4 — Cortex-M4 port layer (port.c + assembly helpers).
#
# PUBLIC compile/link options propagate to every target that links this library
# so that CPU/FPU flags are applied consistently at compile AND link time.
#
# PRIVATE includes cover the kernel internals that port.c dips into (kernel_priv,
# task_priv, klog, profiling). These are NOT exposed to consumers.

add_library(kartos_arch_cortex_m4 STATIC
    port.c)
add_library(kartos::arch_cortex_m4 ALIAS kartos_arch_cortex_m4)

target_compile_options(kartos_arch_cortex_m4
    PUBLIC
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=softfp
    PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -O2
        -g3)

target_link_options(kartos_arch_cortex_m4
    PUBLIC
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=softfp)

target_include_directories(kartos_arch_cortex_m4
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/arch/common
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/task
        ${CMAKE_SOURCE_DIR}/src/logging
        ${CMAKE_SOURCE_DIR}/src/profiling
        ${CMAKE_SOURCE_DIR}/src/utils)

# Pulls in HAL includes + USE_HAL_DRIVER + MCU-part define via kartos::board.
target_link_libraries(kartos_arch_cortex_m4
    PUBLIC
        kartos::mcu_${KARTOS_MCU_FAMILY}
        kartos::board)
