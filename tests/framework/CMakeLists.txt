# kartos::test_framework — suite runner, three-tier assertions, invariant
# table, and direct-UART verdict emitter shared by every on-board test binary.
#
# Linked PUBLICly against kartos::kernel so consumers transitively get the
# kernel + board + arch flag set without having to repeat them per variant.

add_library(kartos_test_framework STATIC
    test_suite.c
    test_invariants.c
    test_verdict_io.c
    test_runtime.c
    test_watchdog.c
    test_sync.c
    test_rand.c
    test_kassert_catcher.c)
add_library(kartos::test_framework ALIAS kartos_test_framework)

target_include_directories(kartos_test_framework
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(kartos_test_framework
    PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -O2 -g3)

target_compile_definitions(kartos_test_framework
    PUBLIC
        RTOS_KLOG_ENABLED=1U
        RTOS_ULOG_ENABLED=1U)

target_link_libraries(kartos_test_framework
    PUBLIC
        kartos::kernel
        kartos::board_support
        kartos::arch_${KARTOS_ARCH}
        kartos::mcu_${KARTOS_MCU_FAMILY}
        kartos::board)
