#ifndef TEST_INVARIANTS_H
#define TEST_INVARIANTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @file test_invariants.h
 * @brief Per-case invariant table and verdict emitter.
 *
 * Each test case declares up front the invariants it intends to check using
 * @ref TEST_INV_DECLARE. During the case body, every observed pass is recorded
 * with @ref test_inv_pass and every observed failure with @ref test_inv_fail.
 *
 * A case PASSES iff:
 *   - every declared invariant has `passed == checked`
 *   - every invariant has `checked >= min_checks`
 *   - no `TEST_ASSERT` aborted the case
 *   - the case was not skipped by `TEST_ASSUME`
 *
 * Verdict lines are emitted directly to the UART (bypassing the log flush
 * task) to ensure the verdict reaches the host even if scheduling is broken.
 */

/** Maximum number of invariants a single case may declare. */
#define TEST_INV_MAX_PER_CASE 16

/** Maximum length of an invariant ID (NUL-terminated). */
#define TEST_INV_ID_MAX_LEN 40

typedef struct
{
    const char *id;          /**< Stable invariant identifier, e.g. "INV-MUTEX-PIP-BOOST". */
    uint16_t    min_checks;  /**< Minimum number of @ref test_inv_pass / @ref test_inv_fail observations required. */
    uint16_t    passed;      /**< Number of recorded passes. */
    uint16_t    checked;     /**< Total number of recorded observations (pass + fail). */
} test_invariant_t;

typedef enum
{
    TEST_CASE_VERDICT_PASS = 0,
    TEST_CASE_VERDICT_FAIL,
    TEST_CASE_VERDICT_SKIP
} test_case_verdict_t;

typedef struct
{
    const char         *suite_name;
    const char         *case_name;
    const char         *source_file;       /**< Source file declaring the suite; used for verdict line. */
    test_invariant_t    invariants[TEST_INV_MAX_PER_CASE];
    uint8_t             invariant_count;
    bool                hard_failed;       /**< Set when a TEST_ASSERT records a failure and aborts the case. */
    bool                skipped;           /**< Set when a TEST_ASSUME precondition is false. */
    const char         *skip_reason;       /**< Optional reason captured from the failing TEST_ASSUME. */
} test_case_state_t;

/** Per-suite running totals, populated by the runner. */
typedef struct
{
    const char *name;
    uint16_t    total;
    uint16_t    pass;
    uint16_t    fail;
    uint16_t    skip;
} test_suite_state_t;

/* ---- Per-case lifecycle (called by the runner, not by tests) ---- */

void test_inv_case_begin(const char *suite_name, const char *case_name, const char *source_file);
test_case_verdict_t test_inv_case_end(void);

test_case_state_t *test_inv_current_case(void);

/* ---- Invariant API used by test cases ---- */

/**
 * @brief Declare an invariant the case intends to check.
 *
 * @param id           Stable identifier (string literal pointer is stored, not copied).
 * @param min_checks   Minimum number of pass/fail observations expected. 0 means
 *                     "no minimum"; values >= 1 turn unexercised invariants into
 *                     failures.
 *
 * If the same id is declared twice in one case, the second call is a no-op
 * (idempotent). If the invariant table overflows, the case is marked as
 * hard-failed via an internal `INV-FRAMEWORK-OVERFLOW` record.
 */
void test_inv_declare(const char *id, uint16_t min_checks);

/** Record one passing observation against @p id. Creates the invariant if not yet declared. */
void test_inv_pass(const char *id);

/**
 * @brief Record one failing observation against @p id.
 *
 * @param id      Invariant identifier.
 * @param detail  Optional short detail line (file/line/expr); may be NULL.
 */
void test_inv_fail(const char *id, const char *detail);

/* ---- Suite-level verdict (CASE_RESULT + SUITE_RESULT lines) ---- */

/**
 * @brief Emit the CASE_RESULT line for the just-finished case.
 *
 * Called by the runner after @ref test_inv_case_end.
 *
 * Format:
 *   <tick>\tTEST\t<file>\t0\ttest_suite_run\tCASE_RESULT\t<suite>:<case>:PASS|FAIL|SKIP\t<inv summary>\tfailed=<csv>
 */
void test_inv_emit_case_verdict(const test_case_state_t *state, test_case_verdict_t verdict);

/**
 * @brief Emit the SUITE_RESULT line summarising the run.
 *
 * Format:
 *   <tick>\tTEST\t<file>\t0\ttest_suite_run\tSUITE_RESULT\t<suite>:PASS|FAIL\tcases=N;pass=P;fail=F;skip=S
 */
void test_inv_emit_suite_verdict(const test_suite_state_t *suite_state, const char *source_file);

/**
 * @brief Mark the running case as hard-failed and capture the location.
 *
 * Called by the TEST_ASSERT macro before it longjmps back to the runner.
 */
void test_inv_mark_hard_fail(const char *id, const char *file, int line, const char *expr);

/**
 * @brief Mark the running case as skipped and capture the precondition that failed.
 */
void test_inv_mark_skip(const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* TEST_INVARIANTS_H */
