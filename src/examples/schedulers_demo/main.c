#include "KARTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "task.h"
#include "ulog.h"

#define BLINK_TASK_PRIORITY   (1U)
#define PRINT_B_TASK_PRIORITY (2U)
#define PRINT_A_TASK_PRIORITY (3U)

#define BLINK_DELAY_MS  (500U)
#define PRINT_DELAY_MS  (200U)
#define BUSY_LOOP_ITERS (1000000U)

static void busy_wait(void)
{
    for (volatile uint32_t i = 0; i < BUSY_LOOP_ITERS; i++)
    {
        __asm volatile("nop");
    }
}

static void print_a_task(void *param)
{
    (void) param;

    while (1)
    {
        busy_wait();
        ulog_info("PRINT | X");
        rtos_delay_ms(PRINT_DELAY_MS);
    }
}

static void print_b_task(void *param)
{
    (void) param;

    while (1)
    {
        busy_wait();
        ulog_info("PRINT | O");
        rtos_delay_ms(PRINT_DELAY_MS);
    }
}

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t      status;
    rtos_task_handle_t print_x_task_handle;
    rtos_task_handle_t print_o_task_handle;

    hardware_env_config();

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(print_a_task, "PRINT_X", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, PRINT_A_TASK_PRIORITY,
                              &print_x_task_handle);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(print_b_task, "PRINT_O", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, PRINT_B_TASK_PRIORITY,
                              &print_o_task_handle);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    (void) rtos_start_scheduler();

    while (1)
    {
    }
}
