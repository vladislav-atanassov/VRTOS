# kartos::kernel — all RTOS sources except examples (those are per-variant executables).
#
# Include directories are deliberately WIDE (matching the PlatformIO -I flags)
# because examples and tests reach into internal headers such as task_priv.h,
# uart_tx.h, and ulog.h. Tightening access is a future refactor concern.

set(_K ${CMAKE_CURRENT_SOURCE_DIR})

add_library(kartos_kernel STATIC
    ${_K}/core/kernel.c
    ${_K}/core/memory.c
    ${_K}/logging/klog.c
    ${_K}/logging/log_flush_task.c
    ${_K}/logging/uart_tx.c
    ${_K}/logging/ulog.c
    ${_K}/profiling/prof_trace.c
    ${_K}/profiling/profiling.c
    ${_K}/scheduler/scheduler.c
    ${_K}/scheduler/scheduler_types/cooperative.c
    ${_K}/scheduler/scheduler_types/preemptive_sp.c
    ${_K}/scheduler/scheduler_types/round_robin.c
    ${_K}/sync/event_group/event_group.c
    ${_K}/sync/mutex/mutex.c
    ${_K}/sync/queue/queue.c
    ${_K}/sync/semaphore/semaphore.c
    ${_K}/task/task.c
    ${_K}/task/task_notify.c
    ${_K}/timer/timer.c
    ${_K}/timer/timer_list.c
    ${_K}/utils/hardware_env.c
    ${_K}/utils/ring_buffer.c
    ${_K}/utils/rtos_assert.c)
add_library(kartos::kernel ALIAS kartos_kernel)

target_include_directories(kartos_kernel
    PUBLIC
        # Public API consumed by examples and tests
        ${CMAKE_SOURCE_DIR}/include
        # Semi-public internals accessed by examples/tests
        ${_K}/task
        ${_K}/logging
        ${_K}/utils
        ${_K}/profiling
        ${CMAKE_BINARY_DIR}/generated    # build_info.h
    PRIVATE
        ${_K}/core
        ${_K}/scheduler
        ${_K}/scheduler/scheduler_types)

target_compile_options(kartos_kernel
    PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -O2 -g3)

# Transitive: arch brings CPU flags + arch includes; mcu brings HAL includes +
# USE_HAL_DRIVER; board brings MCU part define + board header dir.
target_link_libraries(kartos_kernel
    PUBLIC
        kartos::arch_${KARTOS_ARCH}
        kartos::mcu_${KARTOS_MCU_FAMILY}
        kartos::board)
