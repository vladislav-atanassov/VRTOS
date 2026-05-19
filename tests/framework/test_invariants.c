#include "test_invariants.h"

#include "KARTOS.h"          /* rtos_get_tick_count */
#include "log_common.h"      /* LOG_BASENAME */
#include "test_verdict_io.h"

#include <stdio.h>
#include <string.h>

/* =================== Per-case mutable state =================== */

static test_case_state_t g_case;
static bool              g_case_active = false;

test_case_state_t *test_inv_current_case(void)
{
    return g_case_active ? &g_case : NULL;
}

/* =================== Case lifecycle =================== */

void test_inv_case_begin(const char *suite_name, const char *case_name, const char *source_file)
{
    memset(&g_case, 0, sizeof(g_case));
    g_case.suite_name  = suite_name;
    g_case.case_name   = case_name;
    g_case.source_file = source_file;
    g_case_active      = true;
}

test_case_verdict_t test_inv_case_end(void)
{
    test_case_verdict_t verdict;

    if (g_case.skipped)
    {
        verdict = TEST_CASE_VERDICT_SKIP;
    }
    else if (g_case.hard_failed)
    {
        verdict = TEST_CASE_VERDICT_FAIL;
    }
    else
    {
        verdict = TEST_CASE_VERDICT_PASS;
        for (uint8_t i = 0; i < g_case.invariant_count; i++)
        {
            const test_invariant_t *inv = &g_case.invariants[i];
            if (inv->passed != inv->checked || inv->checked < inv->min_checks)
            {
                verdict = TEST_CASE_VERDICT_FAIL;
                break;
            }
        }
    }

    g_case_active = false;
    return verdict;
}

/* =================== Invariant API =================== */

static test_invariant_t *find_or_add_invariant(const char *id)
{
    if (!g_case_active || id == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0; i < g_case.invariant_count; i++)
    {
        if (g_case.invariants[i].id != NULL && strcmp(g_case.invariants[i].id, id) == 0)
        {
            return &g_case.invariants[i];
        }
    }

    if (g_case.invariant_count >= TEST_INV_MAX_PER_CASE)
    {
        /* Overflow — mark the case as hard-failed via the reserved framework ID. */
        g_case.hard_failed = true;
        return NULL;
    }

    test_invariant_t *inv = &g_case.invariants[g_case.invariant_count++];
    inv->id               = id;
    inv->min_checks       = 0;
    inv->passed           = 0;
    inv->checked          = 0;
    return inv;
}

void test_inv_declare(const char *id, uint16_t min_checks)
{
    test_invariant_t *inv = find_or_add_invariant(id);
    if (inv == NULL)
    {
        return;
    }
    /* If declared twice with different min_checks, keep the stricter (larger) bound. */
    if (min_checks > inv->min_checks)
    {
        inv->min_checks = min_checks;
    }
}

void test_inv_pass(const char *id)
{
    test_invariant_t *inv = find_or_add_invariant(id);
    if (inv == NULL)
    {
        return;
    }
    inv->passed++;
    inv->checked++;
}

void test_inv_fail(const char *id, const char *detail)
{
    (void) detail; /* Reserved for future structured logging. */

    test_invariant_t *inv = find_or_add_invariant(id);
    if (inv == NULL)
    {
        return;
    }
    inv->checked++;
}

void test_inv_mark_hard_fail(const char *id, const char *file, int line, const char *expr)
{
    (void) id;
    (void) file;
    (void) line;
    (void) expr;
    if (g_case_active)
    {
        g_case.hard_failed = true;
    }
}

void test_inv_mark_skip(const char *reason)
{
    if (g_case_active)
    {
        g_case.skipped     = true;
        g_case.skip_reason = reason;
    }
}

/* =================== Verdict line emission =================== */

#define VERDICT_LINE_MAX 512

static const char *verdict_label(test_case_verdict_t v)
{
    switch (v)
    {
        case TEST_CASE_VERDICT_PASS: return "PASS";
        case TEST_CASE_VERDICT_FAIL: return "FAIL";
        case TEST_CASE_VERDICT_SKIP: return "SKIP";
    }
    return "?";
}

void test_inv_emit_case_verdict(const test_case_state_t *state, test_case_verdict_t verdict)
{
    if (state == NULL)
    {
        return;
    }

    static char buf[VERDICT_LINE_MAX];
    int  written;
    int  remaining = (int) sizeof(buf);
    int  used      = 0;

    /* Header: <tick>\tTEST\t<file>\t0\ttest_suite_run\tCASE_RESULT\t<suite>:<case>:<VERDICT>\t */
    written = snprintf(buf + used, (size_t) remaining,
                       "%08lu\tTEST\t%s\t0\ttest_suite_run\tCASE_RESULT\t%s:%s:%s\t",
                       (unsigned long) rtos_get_tick_count(),
                       state->source_file ? LOG_BASENAME(state->source_file) : "?",
                       state->suite_name ? state->suite_name : "?",
                       state->case_name ? state->case_name : "?",
                       verdict_label(verdict));
    if (written <= 0 || written >= remaining)
    {
        return;
    }
    used      += written;
    remaining -= written;

    /* Invariant summary: ID:passed/checked;... (or "-" if no invariants declared). */
    if (state->invariant_count == 0)
    {
        written = snprintf(buf + used, (size_t) remaining, "-");
    }
    else
    {
        for (uint8_t i = 0; i < state->invariant_count && remaining > 1; i++)
        {
            const test_invariant_t *inv = &state->invariants[i];
            written = snprintf(buf + used, (size_t) remaining, "%s%s:%u/%u",
                               i == 0 ? "" : ";",
                               inv->id ? inv->id : "?",
                               (unsigned) inv->passed,
                               (unsigned) inv->checked);
            if (written <= 0 || written >= remaining)
            {
                return;
            }
            used      += written;
            remaining -= written;
        }
        written = 0; /* already accumulated */
    }
    if (written > 0)
    {
        used      += written;
        remaining -= written;
    }

    /* Tab + failed=<csv>. Empty CSV on PASS; lists IDs whose checked!=passed otherwise.
     * Hard-fail and skip emit a single token instead of the per-invariant list. */
    written = snprintf(buf + used, (size_t) remaining, "\tfailed=");
    if (written <= 0 || written >= remaining)
    {
        return;
    }
    used      += written;
    remaining -= written;

    if (verdict == TEST_CASE_VERDICT_SKIP)
    {
        written = snprintf(buf + used, (size_t) remaining, "SKIP:%s",
                           state->skip_reason ? state->skip_reason : "");
    }
    else if (verdict == TEST_CASE_VERDICT_FAIL)
    {
        bool first = true;
        if (state->hard_failed)
        {
            written = snprintf(buf + used, (size_t) remaining, "HARD_FAIL");
            if (written <= 0 || written >= remaining)
            {
                return;
            }
            used      += written;
            remaining -= written;
            first = false;
        }
        for (uint8_t i = 0; i < state->invariant_count && remaining > 1; i++)
        {
            const test_invariant_t *inv = &state->invariants[i];
            bool                    bad = (inv->passed != inv->checked) || (inv->checked < inv->min_checks);
            if (!bad)
            {
                continue;
            }
            written = snprintf(buf + used, (size_t) remaining, "%s%s",
                               first ? "" : ",",
                               inv->id ? inv->id : "?");
            if (written <= 0 || written >= remaining)
            {
                return;
            }
            used      += written;
            remaining -= written;
            first = false;
        }
        written = 0;
    }
    else /* PASS */
    {
        written = 0;
    }

    if (written > 0)
    {
        used      += written;
        remaining -= written;
    }

    /* CRLF terminator matching the existing log format. */
    written = snprintf(buf + used, (size_t) remaining, "\r\n");
    if (written <= 0 || written >= remaining)
    {
        return;
    }
    used += written;

    test_verdict_emit_line(buf, (size_t) used);
}

void test_inv_emit_suite_verdict(const test_suite_state_t *suite_state, const char *source_file)
{
    if (suite_state == NULL)
    {
        return;
    }

    static char buf[VERDICT_LINE_MAX];
    int  written = snprintf(buf, sizeof(buf),
                            "%08lu\tTEST\t%s\t0\ttest_suite_run\tSUITE_RESULT\t%s:%s\t"
                            "cases=%u;pass=%u;fail=%u;skip=%u\r\n",
                            (unsigned long) rtos_get_tick_count(),
                            source_file ? LOG_BASENAME(source_file) : "?",
                            suite_state->name ? suite_state->name : "?",
                            suite_state->fail == 0 ? "PASS" : "FAIL",
                            (unsigned) suite_state->total,
                            (unsigned) suite_state->pass,
                            (unsigned) suite_state->fail,
                            (unsigned) suite_state->skip);
    if (written <= 0 || (size_t) written >= sizeof(buf))
    {
        return;
    }
    test_verdict_emit_line(buf, (size_t) written);
}
