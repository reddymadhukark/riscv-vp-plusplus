/*
 * scoreboard.h — Pass/fail tracking, logging, and structured output
 *
 * Provides:
 *   - sc_pass() / sc_fail()  record results and print to console
 *   - sc_summary()           print final totals
 *   - sc_put_str / sc_put_hex / sc_put_dec  console I/O
 *   - sc_log_finding()       log a spec/model mismatch for errata
 */

#ifndef SCOREBOARD_H
#define SCOREBOARD_H

#include "../common/platform.h"

/* ── Internal counters ───────────────────────────────────────────────────── */
static int sc_pass_count = 0;
static int sc_fail_count = 0;

/* ── Console output ──────────────────────────────────────────────────────── */
static void sc_put_char(char c) { CONSOLE_TBUF = (uint32_t)(uint8_t)c; }

void sc_put_str(const char *s)  { while (*s) sc_put_char(*s++); }

void sc_put_hex(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    sc_put_char('0'); sc_put_char('x');
    sc_put_char(h[(v >> 28) & 0xFu]); sc_put_char(h[(v >> 24) & 0xFu]);
    sc_put_char(h[(v >> 20) & 0xFu]); sc_put_char(h[(v >> 16) & 0xFu]);
    sc_put_char(h[(v >> 12) & 0xFu]); sc_put_char(h[(v >>  8) & 0xFu]);
    sc_put_char(h[(v >>  4) & 0xFu]); sc_put_char(h[(v >>  0) & 0xFu]);
}

static void sc_put_dec(uint32_t v)
{
    char buf[12];
    int  i = 11;
    buf[i] = '\0';
    if (v == 0u) { sc_put_char('0'); return; }
    while (v > 0u) { buf[--i] = (char)('0' + (v % 10u)); v /= 10u; }
    sc_put_str(&buf[i]);
}

/* ── sc_pass — record a passing test ────────────────────────────────────── */
static void sc_pass(const char *test_name)
{
    sc_put_str("[PASS] ");
    sc_put_str(test_name);
    sc_put_str("\r\n");
    ++sc_pass_count;
}

/* ── sc_fail — record a failing test with expected vs actual detail ─────── */
static void sc_fail(const char *test_name, const char *expected,
                    const char *actual, const char *root_cause)
{
    sc_put_str("[FAIL] ");
    sc_put_str(test_name);
    sc_put_str("\r\n");
    sc_put_str("  Expected  : "); sc_put_str(expected);   sc_put_str("\r\n");
    sc_put_str("  Actual    : "); sc_put_str(actual);     sc_put_str("\r\n");
    sc_put_str("  RootCause : "); sc_put_str(root_cause); sc_put_str("\r\n");
    ++sc_fail_count;
}

/* ── sc_fail_hex — convenience variant that prints hex expected/actual ───── */
static void sc_fail_hex(const char *test_name, uint32_t exp, uint32_t got,
                         const char *root_cause)
{
    sc_put_str("[FAIL] ");
    sc_put_str(test_name);
    sc_put_str("\r\n");
    sc_put_str("  Expected  : "); sc_put_hex(exp); sc_put_str("\r\n");
    sc_put_str("  Actual    : "); sc_put_hex(got); sc_put_str("\r\n");
    sc_put_str("  RootCause : "); sc_put_str(root_cause); sc_put_str("\r\n");
    ++sc_fail_count;
}

/* ── sc_log_finding — log a spec-vs-model divergence (errata candidate) ──── */
static void sc_log_finding(const char *test_id, const char *spec_says,
                             const char *model_does, const char *suggested_fix)
{
    sc_put_str("[FINDING] ");
    sc_put_str(test_id);
    sc_put_str("\r\n");
    sc_put_str("  Spec     : "); sc_put_str(spec_says);     sc_put_str("\r\n");
    sc_put_str("  Model    : "); sc_put_str(model_does);    sc_put_str("\r\n");
    sc_put_str("  Fix      : "); sc_put_str(suggested_fix); sc_put_str("\r\n");
}

/* ── sc_banner — print a labelled section separator ─────────────────────── */
static void sc_banner(const char *label)
{
    sc_put_str("\r\n================================================\r\n  ");
    sc_put_str(label);
    sc_put_str("\r\n================================================\r\n");
}

/* ── sc_summary — print final pass/fail counts and exit VP ──────────────── */
static void sc_summary(const char *suite_name)
{
    sc_put_str("\r\n================================================\r\n");
    sc_put_str("  Suite    : "); sc_put_str(suite_name); sc_put_str("\r\n");
    sc_put_str("  Passed   : "); sc_put_dec((uint32_t)sc_pass_count); sc_put_str("\r\n");
    sc_put_str("  Failed   : "); sc_put_dec((uint32_t)sc_fail_count); sc_put_str("\r\n");
    sc_put_str(sc_fail_count == 0 ? "  RESULT   : ALL PASSED\r\n"
                                  : "  RESULT   : FAILURES DETECTED\r\n");
    sc_put_str("================================================\r\n");
    EXITER = 0u;
}

#endif /* SCOREBOARD_H */
