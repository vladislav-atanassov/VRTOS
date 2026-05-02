#include "VRTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "rtos_port.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "test_common.h"
#include "test_log.h"
#include "timer.h"
#include "uart_tx.h"

#include <stdio.h>

/*
 * Sweep a range of sleep durations to exercise short, medium, and long
 * tickless idle windows. Tolerances scale with duration to absorb tick
 * granularity (+-1 tick) plus UART/log jitter.
 */
static const uint32_t k_sleep_durations_ms[] = {
    1U,    2U,    5U,    10U,   25U,   50U,   100U, 200U,
    250U,  333U,  500U,  750U,  1000U, 1500U, 2000U,
};
#define SLEEP_DURATION_COUNT (sizeof(k_sleep_durations_ms) / sizeof(k_sleep_durations_ms[0]))

/* Per-iteration tolerance: 2 ticks of slop + 1% of the requested delay. */
#define TICK_TOLERANCE(ms) (2U + ((ms) / 100U))
#define US_TOLERANCE(ms)   ((TICK_TOLERANCE(ms)) * 1000U + 500U)

#define TEST_DURATION_MS 30000U

static volatile bool       g_test_started = false;
static rtos_timer_handle_t g_test_timer;

static void task_func(void *param)
{
    (void) param;
    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Tickless");

    char ctx[80];

    for (uint32_t i = 0; i < SLEEP_DURATION_COUNT; i++)
    {
        uint32_t requested_ms = k_sleep_durations_ms[i];

        uint32_t start_us   = rtos_port_get_uptime_us();
        uint32_t start_tick = rtos_get_tick_count();

        rtos_delay_ms(requested_ms);

        uint32_t end_us   = rtos_port_get_uptime_us();
        uint32_t end_tick = rtos_get_tick_count();

        uint32_t elapsed_us    = end_us - start_us;
        uint32_t elapsed_ticks = end_tick - start_tick;

        uint32_t tick_tol = TICK_TOLERANCE(requested_ms);
        uint32_t us_tol   = US_TOLERANCE(requested_ms);

        snprintf(ctx, sizeof(ctx), "req=%lums elapsed_us=%lu elapsed_ticks=%lu tol=+-%lu",
                 (unsigned long) requested_ms, (unsigned long) elapsed_us,
                 (unsigned long) elapsed_ticks, (unsigned long) tick_tol);
        test_log_task("TICKLESS", ctx);

        /* Tick count must advance by requested_ms +- tick_tol */
        TEST_ASSERT(elapsed_ticks + tick_tol >= requested_ms &&
                        elapsed_ticks <= requested_ms + tick_tol,
                    "Ticks advanced correctly");

        /* Wall-clock uptime must match requested duration within us_tol */
        uint32_t requested_us = requested_ms * 1000U;
        TEST_ASSERT(elapsed_us + us_tol >= requested_us &&
                        elapsed_us <= requested_us + us_tol,
                    "Uptime advanced correctly");

        /* Tick-derived and uptime-derived elapsed must agree within 2 ticks. */
        uint32_t ticks_from_us = elapsed_us / 1000U;
        uint32_t diff = (ticks_from_us > elapsed_ticks) ? (ticks_from_us - elapsed_ticks)
                                                        : (elapsed_ticks - ticks_from_us);
        TEST_ASSERT(diff <= 2U, "Tick and uptime sources agree");
    }

    TEST_EMIT_VERDICT();
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

static void startup_timer_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    g_test_started = true;
    test_log_framework("BEGIN", "TicklessIdle");
    rtos_timer_handle_t *p = (rtos_timer_handle_t *) param;
    rtos_timer_start(*p);
}

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    test_log_framework("TIMEOUT", "TicklessIdle");
}

int main(void)
{
    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);
    rtos_init();

    rtos_timer_handle_t startup_timer;
    test_create_startup_timer(startup_timer_callback, &g_test_timer, &startup_timer);
    rtos_timer_create("TestTimer", TEST_DURATION_MS, RTOS_TIMER_ONE_SHOT, test_timeout_callback, NULL, &g_test_timer);

    rtos_task_handle_t task_handle;
    rtos_task_create(task_func, "Task", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 2, &task_handle);

    rtos_task_handle_t flush_handle;
    test_create_log_flush_task(&flush_handle);

    rtos_start_scheduler();
    while(1){}
}
