#include "VRTOS.h"
#include "log.h"
#include "mutex.h"
#include "task.h"

/* Priorities */
#define PRIORITY_LOW    1
#define PRIORITY_MEDIUM 2
#define PRIORITY_HIGH   3

/* Tasks */
rtos_task_handle_t task_l_handle;
rtos_task_handle_t task_m_handle;
rtos_task_handle_t task_h_handle;

/* Mutexes */
rtos_mutex_t mutex_1; /* Held by L, needed by H */
rtos_mutex_t mutex_2; /* Held by M, needed by L */

/* Flags */
volatile bool test_complete = false;

/**
 * SCENARIO:
 * 1. Low runs, locks Mutex 1.
 * 2. Medium runs, locks Mutex 2.
 * 3. Low runs, tries to lock Mutex 2 (owned by M). Blocks.
 *    Chain: L -> M2(M).
 * 4. High runs, tries to lock Mutex 1 (owned by L). Blocks.
 *    Chain: H -> M1(L) -> M2(M).
 *
 * EXPECTED:
 * - L boosted to H's priority.
 * - M boosted to H's priority (Transitive!).
 */

void TaskLow(void *param)
{
    log_info("LOW: Started. Locking Mutex 1...");
    rtos_mutex_lock(&mutex_1, RTOS_MAX_WAIT);
    log_info("LOW: Locked Mutex 1. Working...");

    /* Simulate work then try to lock Mutex 2 */
    rtos_delay_ms(100);

    log_info("LOW: Attempting to lock Mutex 2 (owned by MEDIUM)...");
    rtos_mutex_lock(&mutex_2, RTOS_MAX_WAIT); /* Should block here */

    log_info("LOW: Locked Mutex 2! (Should happen after M releases)");
    rtos_mutex_unlock(&mutex_2);
    rtos_mutex_unlock(&mutex_1);

    while (1)
        rtos_delay_ms(1000);
}

void TaskMedium(void *param)
{
    /* Wait for L to run and block */
    rtos_delay_ms(200);

    log_info("MEDIUM: Started. Locking Mutex 2...");
    rtos_mutex_lock(&mutex_2, RTOS_MAX_WAIT);
    log_info("MEDIUM: Locked Mutex 2. simulating hold...");

    /* Wait enough for L to block on us, and H to block on L */
    rtos_delay_ms(500);

    /* VERIFICATION POINT */
    rtos_priority_t my_prio = rtos_task_get_priority(NULL);
    log_info("MEDIUM: Checking priority. Current=%d, High=%d", my_prio, PRIORITY_HIGH);

    if (my_prio == PRIORITY_HIGH)
    {
        log_info("TEST PASSED: Transitive Priority Inheritance worked!");
    }
    else
    {
        log_error("TEST FAILED: Priority not boosted! Expected %d, got %d", PRIORITY_HIGH, my_prio);
    }

    log_info("MEDIUM: Unlocking Mutex 2...");
    rtos_mutex_unlock(&mutex_2);

    while (1)
        rtos_delay_ms(1000);
}

void TaskHigh(void *param)
{
    /* Wait for L and M to setup the chain */
    rtos_delay_ms(400);

    log_info("HIGH: Started. Attempting to lock Mutex 1 (owned by LOW)...");

    /* This will block immediately, causing the priority chain to update */
    rtos_mutex_lock(&mutex_1, RTOS_MAX_WAIT);

    log_info("HIGH: Locked Mutex 1! (Test ending)");
    rtos_mutex_unlock(&mutex_1);

    test_complete = true;
    while (1)
        rtos_delay_ms(1000);
}

int main(void)
{
    rtos_init();
    rtos_mutex_init(&mutex_1);
    rtos_mutex_init(&mutex_2);

    /**
     * Create tasks - Start order: M, L, H (staggered by delays or priority?)
     * Actually, if we start them with delays it's easier to sequence.
     * BUT 'main' runs before scheduler.
     * Let's use priorities and initial delays inside tasks? No, they run in priority order.
     *
     * Strategy:
     * 1. Start L (Low). It runs. Locks M1.
     * 2. Start M (Medium). It preempts L? Yes. Locks M2.
     * 3. Start H (High). It preempts M. Tries M1 (blocks).
     *
     * Wait, if we create all 3, then H runs first!
     * We need to carefully sequence them using delays or staggered creation?
     * Can't stagger creation easily without a coordinator task.
     *
     * Alternative: Use delays at start.
     * L: Delay 0.
     * M: Delay 10ms.
     * H: Delay 20ms.
     *
     * But if all created, scheduler picks H first. H executes "rtos_delay". Blocks.
     * M runs. "rtos_delay". Blocks.
     * L runs. Locks M1.
     * ...
     * This works.
    */

    rtos_task_create(TaskLow, "LOW", 512, NULL, PRIORITY_LOW, &task_l_handle);
    rtos_task_create(TaskMedium, "MEDIUM", 512, NULL, PRIORITY_MEDIUM, &task_m_handle);
    rtos_task_create(TaskHigh, "HIGH", 512, NULL, PRIORITY_HIGH, &task_h_handle);

    /**
     * To enforce sequence H->M->L via delays:
     * TaskLow:    No delay (or small).
     * TaskMedium: delay(100).
     * TaskHigh:   delay(200).
     *
     * Since H is highest, it runs first. Call rtos_delay(200). Blocks.
     * M runs. rtos_delay(100). Blocks.
     * L runs. Locks M1. Then waits for M2...?
     *
     * Refined Sequence:
     * L: Runs (because M/H delayed). Locks M1. Then delays(short) to let M run?
     *    No, L is lowest. It only runs if M/H blocked.
     *    L needs to Lock M1. Then it needs to request M2 Block.
     *
     * Correct sequence plan:
     * 1. H starts, delays 500ms.
     * 2. M starts, delays 300ms.
     * 3. L starts (no delay). Locks M1. Then does work (delays/yields? No, if it yields M runs).
     *    L Locks M1. Then delays 600ms (to hold it).
     *    Actually, if L delays, it blocks.
     *
     * Let's use a Coordinator task? Or just carefully tuned checks?
     *
     * Simpler Sequence:
     * 1. L starts. Locks M1. Yields/Blocks on M2?
     *    To get M to run, L must block/yield.
     *    If L blocks (delay), M runs. M locks M2. M blocks (delay).
     *    L wakes up. L tries lock M2. Blocks on M2.
     *    Chain L->M2(M).
     *    H wakes up. Tries M1. Blocks on M1.
     *    Chain H->M1(L)->M2(M).
     *
     * This works!
     *
     * Timings:
     * H: Delay 400.
     * M: Delay 200. Lock M2. Delay 1000 (hold).
     * L: Delay 0. Lock M1. Delay 300 (wait for M to lock M2). Lock M2.
     *
     * Tracing:
     * T=0: L runs. Locks M1. Enters Delay(300).
     * T=0+: M runs (was delayed 200). Wait, H(400), M(200).
     *       At T=0: H blocks(400), M blocks(200). L runs.
     *       L locks M1. L blocks(300).
     * T=200: M wakes. Locks M2. Enters Delay(1000).
     * T=300: L wakes. Tries lock M2. Blocks (on M).
     * T=400: H wakes. Tries lock M1. Blocks (on L).
     *        -> Boost L to H.
     *        -> Check L blocked on M2? Yes.
     *        -> Boost M to H.
     * VERIFICATION: M checks priority at T > 400.
     */

    /** 
     * Add delays to starts for sequencing logic above
     * H: will delay 400
     * M: will delay 200
     * L: will delay 0 (implicit) 
     */

    rtos_start_scheduler();
    return 0;
}
