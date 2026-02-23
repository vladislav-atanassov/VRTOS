/*******************************************************************************
 * File: examples/fpu_context_test/main.c
 * Description: FPU Context Switch Verification Example
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "log.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"

/**
 * @file main.c
 * @brief FPU Context Switch Verification
 *
 * Verifies that the FPU state (S16-S31) is correctly saved and restored across
 * context switches. Each task uses a different floating-point operation and
 * checks the result against the expected value every iteration. If the FPU
 * context is corrupted by another task, the values will diverge and the error
 * will be logged.
 *
 * Task A: multiplicative accumulator  fpu_a = fpu_a * 1.01 + 0.5
 * Task B: convergent accumulator      fpu_b = fpu_b * 0.99 + 1.0
 * Task C: sine-like oscillator        fpu_c = fpu_c - fpu_c^3 / 6.0
 *
 * All three tasks run at the same priority to maximise the number of context
 * switches between FPU-using tasks.
 */

/* All tasks share the same priority so context switches happen frequently */
#define FPU_TASK_PRIORITY (2U)
#define FPU_TASK_DELAY_MS (50U)

/* Tolerance for floating-point comparison */
#define FPU_EPSILON (0.001f)

static float float_abs(float x)
{
    return x < 0.0f ? -x : x;
}

/**
 * @brief Task A — multiplicative accumulator
 */
static void fpu_task_a(void *param)
{
    (void) param;

    volatile float fpu_a     = 1.0f;
    float          expected  = 1.0f;
    uint32_t       iteration = 0;
    uint32_t       errors    = 0;

    log_info("[FPU-A] Started (init=1.0, op: x*1.01+0.5)");

    while (1)
    {
        /* FPU operation */
        fpu_a    = fpu_a * 1.01f + 0.5f;
        expected = expected * 1.01f + 0.5f;
        iteration++;

        /* Verify FPU state was preserved */
        if (float_abs((float) fpu_a - expected) > FPU_EPSILON)
        {
            errors++;
            log_error("[FPU-A] CORRUPTION at iter %lu: got %.4f, expected %.4f (errors=%lu)", (unsigned long) iteration,
                      (double) fpu_a, (double) expected, (unsigned long) errors);
            /* Resync so we keep checking */
            expected = (float) fpu_a;
        }

        /* Periodic status */
        if ((iteration % 100) == 0)
        {
            log_info("[FPU-A] iter=%lu val=%.4f errors=%lu", (unsigned long) iteration, (double) fpu_a,
                     (unsigned long) errors);
            /* Reset to prevent overflow */
            fpu_a    = 1.0f;
            expected = 1.0f;
        }

        rtos_delay_ms(FPU_TASK_DELAY_MS);
    }
}

/**
 * @brief Task B — convergent accumulator
 */
static void fpu_task_b(void *param)
{
    (void) param;

    volatile float fpu_b     = 100.0f;
    float          expected  = 100.0f;
    uint32_t       iteration = 0;
    uint32_t       errors    = 0;

    log_info("[FPU-B] Started (init=100.0, op: x*0.99+1.0)");

    while (1)
    {
        fpu_b    = fpu_b * 0.99f + 1.0f;
        expected = expected * 0.99f + 1.0f;
        iteration++;

        if (float_abs((float) fpu_b - expected) > FPU_EPSILON)
        {
            errors++;
            log_error("[FPU-B] CORRUPTION at iter %lu: got %.4f, expected %.4f (errors=%lu)", (unsigned long) iteration,
                      (double) fpu_b, (double) expected, (unsigned long) errors);
            expected = (float) fpu_b;
        }

        if ((iteration % 100) == 0)
        {
            log_info("[FPU-B] iter=%lu val=%.4f errors=%lu", (unsigned long) iteration, (double) fpu_b,
                     (unsigned long) errors);
        }

        rtos_delay_ms(FPU_TASK_DELAY_MS);
    }
}

/**
 * @brief Task C — power-series oscillator (exercises multiply and divide)
 */
static void fpu_task_c(void *param)
{
    (void) param;

    volatile float fpu_c     = 0.5f;
    float          expected  = 0.5f;
    uint32_t       iteration = 0;
    uint32_t       errors    = 0;

    log_info("[FPU-C] Started (init=0.5, op: x - x^3/6)");

    while (1)
    {
        /* Sine-like Taylor term: keeps value bounded */
        float x = (float) fpu_c;
        fpu_c   = x - (x * x * x) / 6.0f;

        float ex = expected;
        expected = ex - (ex * ex * ex) / 6.0f;
        iteration++;

        if (float_abs((float) fpu_c - expected) > FPU_EPSILON)
        {
            errors++;
            log_error("[FPU-C] CORRUPTION at iter %lu: got %.4f, expected %.4f (errors=%lu)", (unsigned long) iteration,
                      (double) fpu_c, (double) expected, (unsigned long) errors);
            expected = (float) fpu_c;
        }

        if ((iteration % 100) == 0)
        {
            log_info("[FPU-C] iter=%lu val=%.6f errors=%lu", (unsigned long) iteration, (double) fpu_c,
                     (unsigned long) errors);
        }

        rtos_delay_ms(FPU_TASK_DELAY_MS);
    }
}

/**
 * @brief Heartbeat task — visual feedback that the system is alive
 */
static void heartbeat_task(void *param)
{
    (void) param;

    while (1)
    {
        led_toggle();
        rtos_delay_ms(500);
    }
}

/**
 * @brief Main function
 */
__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t      status;
    rtos_task_handle_t task_handle;

    hardware_env_config();
    log_uart_init(LOG_LEVEL_INFO);

    log_info("\n\n");
    log_info("====================================");
    log_info("  FPU Context Switch Verification");
    log_info("====================================");
    log_info("3 tasks with different FPU operations");
    log_info("Each verifies its FPU state survives");
    log_info("context switches. Errors = corruption.");
    log_info("====================================\n");

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
        indicate_system_failure();
    }

    /* Create FPU tasks at the same priority for maximum context switching */
    status = rtos_task_create(fpu_task_a, "FPU-A", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, FPU_TASK_PRIORITY, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(fpu_task_b, "FPU-B", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, FPU_TASK_PRIORITY, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(fpu_task_c, "FPU-C", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, FPU_TASK_PRIORITY, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(heartbeat_task, "HEART", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1U, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    log_info("All FPU test tasks created. Starting scheduler...\n");

    status = rtos_start_scheduler();

    /* Should never reach here */
    log_error("Scheduler returned unexpectedly!");
    indicate_system_failure();

    while (1)
    {
    }
}
