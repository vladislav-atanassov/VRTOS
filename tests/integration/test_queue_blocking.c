#include "VRTOS.h"
#include "log.h"
#include "mutex.h"
#include "queue.h"
#include "task.h"

rtos_queue_handle_t queue;

/* Task that tries to send to full queue */
void SenderTask(void *param)
{
    rtos_status_t status;
    int           item = 1;

    /* Fill queue (size 2) */
    log_info("SENDER: Filling queue...");
    rtos_queue_send(queue, &item, RTOS_NO_WAIT); /* 1 */
    rtos_queue_send(queue, &item, RTOS_NO_WAIT); /* 2 */

    log_info("SENDER: Attempting 3rd send (Should block)...");

    /* This should block until receiver runs */
    status = rtos_queue_send(queue, &item, RTOS_MAX_WAIT);

    if (status == RTOS_SUCCESS)
    {
        log_info("SENDER: Unblocked and sent item! (Success)");
    }
    else
    {
        log_error("SENDER: Failed to send item or timed out!");
    }

    /* Test Timeout */
    log_info("SENDER: Filling queue again to test timeout...");
    rtos_queue_send(queue, &item, RTOS_NO_WAIT);
    rtos_queue_send(queue, &item, RTOS_NO_WAIT); /* Ensure full */

    rtos_tick_t start = rtos_get_tick_count();
    status            = rtos_queue_send(queue, &item, 100); /* 100 ticks timeout */
    rtos_tick_t end   = rtos_get_tick_count();

    if (status == RTOS_ERROR_TIMEOUT)
    {
        log_info("SENDER: Correctly timed out after %lu ticks", (unsigned long) (end - start));
    }
    else
    {
        log_error("SENDER: Did not timeout as expected! Status=%d", status);
    }

    while (1)
        rtos_delay_ms(1000);
}

void ReceiverTask(void *param)
{
    int rx_item;

    /* Let sender fill queue and block */
    rtos_delay_ms(500);

    log_info("RECEIVER: Reading item to unblock sender...");
    rtos_queue_receive(queue, &rx_item, RTOS_MAX_WAIT);

    log_info("RECEIVER: Read item. Sender should resume.");

    /* Wait to verification log */
    rtos_delay_ms(200);

    /* Consume rest to clear queue */
    rtos_queue_reset(queue);
    /* Verify reset woke senders? Wait, we need another test for that. */

    while (1)
        rtos_delay_ms(1000);
}

int main(void)
{
    rtos_init();

    /* Queue of integer (size 4 bytes), length 2 */
    rtos_queue_create(&queue, 2, sizeof(int));

    rtos_task_create(SenderTask, "SENDER", 512, NULL, 2, NULL);
    rtos_task_create(ReceiverTask, "RECEIVER", 512, NULL, 3, NULL); /* Higher prio than sender */

    rtos_start_scheduler();
    return 0;
}
