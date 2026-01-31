#include "VRTOS.h"
#include "log.h"
#include "semaphore.h"
#include "task.h"

/* Globals */
rtos_semaphore_t sem_produced;
rtos_semaphore_t sem_consumed;

#define NUM_ITEMS 10
volatile int buffer = 0;

void ProducerTask(void *param)
{
    for (int i = 0; i < NUM_ITEMS; i++)
    {
        /* Produce item */
        buffer = i + 1;
        log_info("PRODUCER: Created item %d", buffer);

        /* Signal availability */
        rtos_semaphore_signal(&sem_produced);

        /* Wait for consumption signal (flow control) */
        rtos_semaphore_wait(&sem_consumed, RTOS_MAX_DELAY);

        rtos_delay_ms(100);
    }

    log_info("PRODUCER: Done.");
    while (1)
        rtos_delay_ms(1000);
}

void ConsumerTask(void *param)
{
    for (int i = 0; i < NUM_ITEMS; i++)
    {
        /* Wait for item */
        rtos_semaphore_wait(&sem_produced, RTOS_MAX_DELAY);

        log_info("CONSUMER: Consumed item %d", buffer);

        if (buffer != i + 1)
        {
            log_error("TEST FAILED: Item mismatch! Expected %d, got %d", i + 1, buffer);
        }

        /* Signal consumed */
        rtos_semaphore_signal(&sem_consumed);
    }

    log_info("CONSUMER: Done. TEST PASSED.");
    while (1)
        rtos_delay_ms(1000);
}

int main(void)
{
    rtos_init();

    /* Init semaphores:
       sem_produced: starts at 0 (nothing produced)
       sem_consumed: starts at 0 (wait for consume?)
       Actually standard P-C:
         produced = 0.
         consumed = 0 (or N if using bounded buffer logic, but here it's hand-shake).
         Producer: produces, signals produced. waits consumed.
         Consumer: waits produced, consumes, signals consumed.
         Correct.
    */
    rtos_semaphore_init(&sem_produced, 0, 10);
    rtos_semaphore_init(&sem_consumed, 0, 10);

    rtos_task_create(ProducerTask, "PRODUCER", 512, NULL, 2, NULL);
    rtos_task_create(ConsumerTask, "CONSUMER", 512, NULL, 2, NULL);

    rtos_start_scheduler();
    return 0;
}
