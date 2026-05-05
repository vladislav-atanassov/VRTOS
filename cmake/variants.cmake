# ── Examples ─────────────────────────────────────────────────────────────────

add_kartos_variant(basic_blinky
    SOURCE "${CMAKE_SOURCE_DIR}/src/examples/basic_blinky/main.c")

add_kartos_variant(producer_consumer
    SOURCE "${CMAKE_SOURCE_DIR}/src/examples/producer_consumer/main.c")

add_kartos_variant(profiling_demo
    SOURCE "${CMAKE_SOURCE_DIR}/src/examples/profiling_demo/main.c")

add_kartos_variant(fpu_context_test
    SOURCE "${CMAKE_SOURCE_DIR}/src/examples/fpu_context_test/main.c")

# ── Scheduler invariant tests ─────────────────────────────────────────────────

add_kartos_variant(test_scheduler_rr_state
    SOURCE "${CMAKE_SOURCE_DIR}/tests/scheduler/round_robin/test_scheduler_rr_state.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_ROUND_ROBIN)

add_kartos_variant(test_scheduler_cooperative_state
    SOURCE "${CMAKE_SOURCE_DIR}/tests/scheduler/cooperative/test_scheduler_cooperative_state.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_COOPERATIVE)

add_kartos_variant(test_scheduler_preemptive_state
    SOURCE "${CMAKE_SOURCE_DIR}/tests/scheduler/preemptive/test_scheduler_preemptive_states.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP)

# ── Integration tests ─────────────────────────────────────────────────────────

add_kartos_variant(test_mutex_state
    SOURCE "${CMAKE_SOURCE_DIR}/tests/integration/test_mutex_state.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP)

add_kartos_variant(test_semaphore_state
    SOURCE "${CMAKE_SOURCE_DIR}/tests/integration/test_semaphore_state.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP)

add_kartos_variant(test_queue_state
    SOURCE "${CMAKE_SOURCE_DIR}/tests/integration/test_queue_state.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP)

add_kartos_variant(test_notification_state
    SOURCE "${CMAKE_SOURCE_DIR}/tests/integration/test_notification_state.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP)

add_kartos_variant(test_event_group_state
    SOURCE "${CMAKE_SOURCE_DIR}/tests/integration/test_event_group_state.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP)

add_kartos_variant(test_task_state_transitions
    SOURCE "${CMAKE_SOURCE_DIR}/tests/integration/test_task_state_transitions.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP)

add_kartos_variant(test_tickless_idle
    SOURCE "${CMAKE_SOURCE_DIR}/tests/integration/test_tickless_idle.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP)

# ── Benchmarks ────────────────────────────────────────────────────────────────

add_kartos_variant(bench_context_switch
    SOURCE "${CMAKE_SOURCE_DIR}/tests/benchmarks/bench_context_switch/bench_context_switch.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/benchmarks"
                   "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP)

add_kartos_variant(bench_mutex
    SOURCE "${CMAKE_SOURCE_DIR}/tests/benchmarks/bench_mutex/bench_mutex.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/benchmarks"
                   "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP
                   BENCH_ITERATIONS=100U
                   BENCH_WARMUP=10U)

add_kartos_variant(bench_queue
    SOURCE "${CMAKE_SOURCE_DIR}/tests/benchmarks/bench_queue/bench_queue.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/benchmarks"
                   "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP)

add_kartos_variant(bench_semaphore
    SOURCE "${CMAKE_SOURCE_DIR}/tests/benchmarks/bench_semaphore/bench_semaphore.c"
    EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/tests/benchmarks"
                   "${CMAKE_SOURCE_DIR}/tests/scheduler"
    EXTRA_DEFINES  RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP
                   BENCH_ITERATIONS=5U
                   BENCH_WARMUP=2U)
