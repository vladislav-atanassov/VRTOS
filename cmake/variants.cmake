# ── Examples ─────────────────────────────────────────────────────────────────
# No SCHEDULER arg = default RTOS_SCHEDULER_PREEMPTIVE_SP (see kartos_add_variant.cmake).

add_kartos_variant(basic_blinky
    SOURCE "${CMAKE_SOURCE_DIR}/src/examples/basic_blinky/main.c")

add_kartos_variant(producer_consumer
    SOURCE "${CMAKE_SOURCE_DIR}/src/examples/producer_consumer/main.c")

add_kartos_variant(profiling_demo
    SOURCE "${CMAKE_SOURCE_DIR}/src/examples/profiling_demo/main.c")

add_kartos_variant(fpu_context_test
    SOURCE "${CMAKE_SOURCE_DIR}/src/examples/fpu_context_test/main.c")

# ── Framework-based test suites ──────────────────────────────────────────────
# One binary = one suite. Each links kartos::test_framework which provides the
# suite runner, three-tier assertions, invariant table, watchdog, and sync
# primitives. RTOS_MAX_TASKS=24 prevents TCB-slot exhaustion across cases.

add_kartos_variant(test_mutex_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_mutex_suite.c"
    SCHEDULER     RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_semaphore_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_semaphore_suite.c"
    SCHEDULER     RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_queue_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_queue_suite.c"
    SCHEDULER     RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_event_group_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_event_group_suite.c"
    SCHEDULER     RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_notify_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_notify_suite.c"
    SCHEDULER     RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_task_state_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_task_state_suite.c"
    SCHEDULER     RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_scheduler_cooperative_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_scheduler_cooperative_suite.c"
    SCHEDULER     RTOS_SCHEDULER_COOPERATIVE
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_scheduler_rr_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_scheduler_rr_suite.c"
    SCHEDULER     RTOS_SCHEDULER_ROUND_ROBIN
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_scheduler_preemptive_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_scheduler_preemptive_suite.c"
    SCHEDULER     RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_tickless_idle_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_tickless_idle_suite.c"
    SCHEDULER     RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24
                  RTOS_CONFIG_USE_TICKLESS_IDLE=1U)

add_kartos_variant(test_heap_allocator_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_heap_allocator_suite.c"
    SCHEDULER     RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_hooks_suite
    SOURCE        "${CMAKE_SOURCE_DIR}/tests/integration/test_hooks_suite.c"
    SCHEDULER     RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS    kartos::test_framework
    EXTRA_DEFINES RTOS_MAX_TASKS=24)

add_kartos_variant(test_scheduler_suspend_suite
    SOURCE         "${CMAKE_SOURCE_DIR}/tests/integration/test_scheduler_suspend_suite.c"
    SCHEDULER      RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_LIBS     kartos::test_framework
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/src/core" # kernel_priv.h — observe g_kernel.scheduler_suspended
    EXTRA_DEFINES  RTOS_MAX_TASKS=24)

# ── Benchmarks ────────────────────────────────────────────────────────────────

add_kartos_variant(bench_context_switch
    SOURCE         "${CMAKE_SOURCE_DIR}/tests/benchmarks/bench_context_switch/bench_context_switch.c"
    SCHEDULER      RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/benchmarks")

add_kartos_variant(bench_mutex
    SOURCE         "${CMAKE_SOURCE_DIR}/tests/benchmarks/bench_mutex/bench_mutex.c"
    SCHEDULER      RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/benchmarks"
    EXTRA_DEFINES  BENCH_ITERATIONS=100U
                   BENCH_WARMUP=10U)

add_kartos_variant(bench_queue
    SOURCE         "${CMAKE_SOURCE_DIR}/tests/benchmarks/bench_queue/bench_queue.c"
    SCHEDULER      RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/benchmarks")

add_kartos_variant(bench_semaphore
    SOURCE         "${CMAKE_SOURCE_DIR}/tests/benchmarks/bench_semaphore/bench_semaphore.c"
    SCHEDULER      RTOS_SCHEDULER_PREEMPTIVE_SP
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/benchmarks"
    EXTRA_DEFINES  BENCH_ITERATIONS=5U
                   BENCH_WARMUP=2U)
