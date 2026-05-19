#include "test_suite.h"

#include "test_invariants.h"
#include "test_watchdog.h"

#include <setjmp.h>
#include <stdbool.h>

/* Suite-runner-owned longjmp state. Visible to TEST_ASSERT / TEST_ASSUME via test_assert.h. */
jmp_buf g_test_case_jmp_buf;
bool    g_test_case_jmp_active = false;

int test_suite_run(const test_suite_t *suite)
{
    if (suite == NULL || suite->cases == NULL || suite->case_count == 0)
    {
        return -1;
    }

    test_suite_state_t totals = {
        .name  = suite->name,
        .total = (uint16_t) suite->case_count,
        .pass  = 0,
        .fail  = 0,
        .skip  = 0,
    };

    if (suite->setup != NULL)
    {
        suite->setup();
    }

    for (size_t i = 0; i < suite->case_count; i++)
    {
        const test_case_t *tc = suite->cases[i];
        if (tc == NULL || tc->body == NULL)
        {
            totals.fail++;
            continue;
        }

        if (suite->before != NULL)
        {
            suite->before();
        }

        test_inv_case_begin(suite->name, tc->name, suite->source_file);

        /*
         * setjmp returns 0 on the initial call and the longjmp `val` on a hard
         * fail / skip. Either way we fall through to the verdict computation;
         * the markers in test_case_state_t tell us which branch we took.
         */
        if (setjmp(g_test_case_jmp_buf) == 0)
        {
            g_test_case_jmp_active = true;
            tc->body();
        }
        g_test_case_jmp_active = false;

        /* Stop any watchdog left running by a TEST_ASSERT longjmp and record
         * its failure into this case's invariant table before the verdict is
         * computed. Idempotent — harmless if no watchdog was armed. */
        test_watchdog_disarm();

        const test_case_state_t *state   = test_inv_current_case();
        test_case_verdict_t      verdict = test_inv_case_end();
        test_inv_emit_case_verdict(state, verdict);

        switch (verdict)
        {
            case TEST_CASE_VERDICT_PASS: totals.pass++; break;
            case TEST_CASE_VERDICT_FAIL: totals.fail++; break;
            case TEST_CASE_VERDICT_SKIP: totals.skip++; break;
        }

        if (suite->after != NULL)
        {
            suite->after();
        }
    }

    if (suite->teardown != NULL)
    {
        suite->teardown();
    }

    test_inv_emit_suite_verdict(&totals, suite->source_file);

    return totals.fail == 0 ? 0 : 1;
}
