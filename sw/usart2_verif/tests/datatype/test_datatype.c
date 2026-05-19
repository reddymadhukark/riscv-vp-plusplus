/*
 * test_datatype.c — Data pattern integrity verification
 *
 * Objective:
 *   Verify that the VP USART2 model correctly transfers all 256 byte values
 *   (full data type coverage) and that bit patterns are not corrupted by the
 *   sc_fifo or the VP model's rx_thread/b_transport path.
 *
 * Test IDs: DT-001 … DT-003
 *
 * DT-001  All 256 byte values transmitted A→B, each verified in RBUF_B
 * DT-002  Bit-pattern tests: 0x00, 0xFF, 0x55, 0xAA, 0x0F, 0xF0 (6 patterns)
 * DT-003  B→A with same 6 critical bit patterns
 */

#include "../../bench/common/platform.h"
#include "../../bench/env/env.h"
#include "../../bench/scoreboard/scoreboard.h"
#include "../../bench/driver/usart_driver.h"
#include "../../bench/monitor/monitor.h"

/* ── DT-001: Full 256-value sweep A→B ───────────────────────────────────── */
static void dt_001_full_sweep(void)
{
    uint32_t v, base, errors = 0u;
    uint8_t  data;

    sc_put_str("DT-001: All 256 byte values A→B (0x00..0xFF)\r\n");
    env_reset();

    for (v = 0u; v <= 0xFFu; ++v) {
        base = g_log_count;
        UA_TBUF = (uint8_t)v;

        if (!wait_rir(base, IRQ_USART_B)) {
            ++errors;
            if (errors <= 3u) {
                sc_put_str("  timeout val="); sc_put_hex(v); sc_put_str("\r\n");
            }
            usart_drain(USART_B);
            continue;
        }

        data = usart_rbuf_read(USART_B);
        if (data != (uint8_t)v) {
            ++errors;
            if (errors <= 5u) {
                sc_put_str("  mismatch val="); sc_put_hex(v);
                sc_put_str(" got="); sc_put_hex((uint32_t)data); sc_put_str("\r\n");
            }
        }
    }

    sc_put_str("  errors="); sc_put_hex(errors); sc_put_str("/256\r\n");

    if (errors == 0u) sc_pass("DT-001 All 256 byte values transferred correctly A→B");
    else sc_fail("DT-001", "0 errors over 256 values", "errors present",
                 "VP model or sc_fifo corrupted byte values in full sweep");
}

/* ── DT-002: Critical bit patterns A→B ──────────────────────────────────── */
static void dt_002_bit_patterns_a_to_b(void)
{
    static const uint8_t pats[6] = { 0x00u, 0xFFu, 0x55u, 0xAAu, 0x0Fu, 0xF0u };
    static const char *names[6]  = { "0x00", "0xFF", "0x55", "0xAA", "0x0F", "0xF0" };
    uint32_t i, base;
    uint8_t  data;
    int      ok = 1;

    sc_put_str("DT-002: Critical bit patterns A→B\r\n");
    env_reset();

    for (i = 0u; i < 6u; ++i) {
        base = g_log_count;
        UA_TBUF = pats[i];

        if (!wait_rir(base, IRQ_USART_B)) {
            sc_put_str("  timeout pattern "); sc_put_str(names[i]); sc_put_str("\r\n");
            ok = 0; usart_drain(USART_B); continue;
        }

        data = usart_rbuf_read(USART_B);
        sc_put_str("  pattern "); sc_put_str(names[i]);
        sc_put_str(" got="); sc_put_hex((uint32_t)data);
        if (data == pats[i]) { sc_put_str(" OK\r\n"); }
        else { sc_put_str(" FAIL\r\n"); ok = 0; }
    }

    if (ok) sc_pass("DT-002 All critical bit patterns A→B correct");
    else    sc_fail("DT-002", "all 6 patterns match", "mismatch",
                    "VP model or sc_fifo distorted bit pattern during transfer");
}

/* ── DT-003: Critical bit patterns B→A ──────────────────────────────────── */
static void dt_003_bit_patterns_b_to_a(void)
{
    static const uint8_t pats[6] = { 0x00u, 0xFFu, 0x55u, 0xAAu, 0x0Fu, 0xF0u };
    static const char *names[6]  = { "0x00", "0xFF", "0x55", "0xAA", "0x0F", "0xF0" };
    uint32_t i, base;
    uint8_t  data;
    int      ok = 1;

    sc_put_str("DT-003: Critical bit patterns B→A\r\n");
    env_reset();

    for (i = 0u; i < 6u; ++i) {
        base = g_log_count;
        UB_TBUF = pats[i];

        if (!wait_rir(base, IRQ_USART_A)) {
            sc_put_str("  timeout pattern "); sc_put_str(names[i]); sc_put_str("\r\n");
            ok = 0; usart_drain(USART_A); continue;
        }

        data = usart_rbuf_read(USART_A);
        sc_put_str("  pattern "); sc_put_str(names[i]);
        sc_put_str(" got="); sc_put_hex((uint32_t)data);
        if (data == pats[i]) { sc_put_str(" OK\r\n"); }
        else { sc_put_str(" FAIL\r\n"); ok = 0; }
    }

    if (ok) sc_pass("DT-003 All critical bit patterns B→A correct");
    else    sc_fail("DT-003", "all 6 patterns match", "mismatch",
                    "B→A path corrupts bit patterns — check sc_fifo_b2a wiring");
}

/* ── main entry point ────────────────────────────────────────────────────── */
void isr_main(void)
{
    sc_banner("USART2 DATA TYPE TESTS (DT-001..003)");
    env_init();

    dt_001_full_sweep();
    dt_002_bit_patterns_a_to_b();
    dt_003_bit_patterns_b_to_a();

    sc_summary("datatype");
}
