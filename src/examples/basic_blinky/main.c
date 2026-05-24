#include "KARTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "task.h"

#define BLINK_TASK_PRIORITY (1U)
#define BLINK_DELAY_MS      (500U)

static void blink_task(void *param)
{
    (void) param;

    while (1)
    {
        led_toggle();
        rtos_delay_ms(BLINK_DELAY_MS);
    }
}

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t      status;
    rtos_task_handle_t blink_task_handle;

    hardware_env_config();

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(blink_task, "BLINK", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, BLINK_TASK_PRIORITY,
                              &blink_task_handle);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    (void) rtos_start_scheduler();

    while (1)
    {
    }
}
