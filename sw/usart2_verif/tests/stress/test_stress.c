/*
 * test_stress.c — Long-duration stability and sustained transfer verification
 *
 * Objective:
 *   Verify the VP model remains stable over many consecutive transfers,
 *   alternating directions, and mixed overrun+recovery scenarios.
 *
 * Test IDs: STR-001 … STR-003
 *
 * STR-001  100 sequential A→B bytes (walking data pattern), verify no loss
 * STR-002  50 alternating A→B / B→A transfers, verify bidirectional integrity
 * STR-003  10-cycle overrun + recovery sequence, verify EIR and RIR correct
 */

#include "../../bench/common/platform.h"
#include "../../bench/env/env.h"
#include "../../bench/scoreboard/scoreboard.h"
#include "../../bench/driver/usart_driver.h"
#include "../../bench/monitor/monitor.h"

#define STR_N_SEQ   100u
#define STR_N_ALT    50u
#define STR_N_OVR    10u

/* ── STR-001: 100 sequential A→B bytes ──────────────────────────────────── */
static void str_001_sequential(void)
{
    uint32_t i, base, errors = 0u;
    uint8_t  data;

    sc_put_str("STR-001: 100 sequential A→B bytes (pattern 0x00..0x63)\r\n");
    env_reset();

    for (i = 0u; i < STR_N_SEQ; ++i) {
        base = g_log_count;
        UA_TBUF = (uint8_t)(i & 0xFFu);

        if (!wait_rir(base, IRQ_USART_B)) {
            sc_put_str("  timeout at byte "); sc_put_hex(i); sc_put_str("\r\n");
            ++errors;
            if (errors >= 3u) { sc_put_str("  aborting after 3 timeouts\r\n"); break; }
            continue;
        }

        data = usart_rbuf_read(USART_B);
        if (data != (uint8_t)(i & 0xFFu)) {
            ++errors;
            if (errors <= 5u) {
                sc_put_str("  mismatch byte "); sc_put_hex(i);
                sc_put_str(" exp="); sc_put_hex((uint32_t)(i & 0xFFu));
                sc_put_str(" got="); sc_put_hex((uint32_t)data); sc_put_str("\r\n");
            }
        }
    }

    sc_put_str("  total errors="); sc_put_hex(errors); sc_put_str("\r\n");

    if (errors == 0u) sc_pass("STR-001 100-byte sequential: no errors");
    else sc_fail("STR-001", "0 errors over 100 bytes", "errors detected",
                 "VP model dropped bytes or produced wrong data in sustained transfer");
}

/* ── STR-002: 50 alternating A→B / B→A ─────────────────────────────────── */
static void str_002_alternating(void)
{
    uint32_t i, base, errors = 0u;
    uint8_t  data;

    sc_put_str("STR-002: 50 alternating A→B / B→A transfers\r\n");
    env_reset();

    for (i = 0u; i < STR_N_ALT; ++i) {
        base = g_log_count;

        if ((i & 1u) == 0u) {
            UA_TBUF = (uint8_t)(0x80u + i);
            if (!wait_rir(base, IRQ_USART_B)) { ++errors; continue; }
            data = usart_rbuf_read(USART_B);
            if (data != (uint8_t)(0x80u + i)) ++errors;
        } else {
            UB_TBUF = (uint8_t)(0x80u + i);
            if (!wait_rir(base, IRQ_USART_A)) { ++errors; continue; }
            data = usart_rbuf_read(USART_A);
            if (data != (uint8_t)(0x80u + i)) ++errors;
        }
    }

    sc_put_str("  total errors="); sc_put_hex(errors); sc_put_str("\r\n");

    if (errors == 0u) sc_pass("STR-002 50-pair alternating transfers: no errors");
    else sc_fail("STR-002", "0 errors over 50 alternating", "errors detected",
                 "VP model or sc_fifo lost bytes in alternating direction stress");
}

/* ── STR-003: 10-cycle overrun + recovery ────────────────────────────────── */
static void str_003_overrun_recovery_cycle(void)
{
    uint32_t i, base, eir_cnt = 0u, rir_cnt = 0u, start_cnt;

    sc_put_str("STR-003: 10-cycle overrun → drain → normal receive\r\n");
    usart_oen_set(USART_B);
    env_reset();
    start_cnt = g_log_count;

    for (i = 0u; i < STR_N_OVR; ++i) {
        /* Step A: fill RBUF_B (get RIR_B) */
        base = g_log_count;
        UA_TBUF = (uint8_t)(0xC0u + i);
        if (!wait_rir(base, IRQ_USART_B)) {
            sc_put_str("  timeout fill cycle "); sc_put_hex(i); sc_put_str("\r\n");
            usart_drain(USART_B);
            continue;
        }

        /* Step B: overrun — wait for EIR_B specifically */
        base = g_log_count;
        UA_TBUF = 0xFFu;
        wait_events(base, 2u);   /* TBIR_A + EIR_B (EIR not RIR — overrun path) */

        /* Step C: drain RBUF_B to reset overrun state */
        usart_drain(USART_B);
    }

    eir_cnt = mon_count_events(start_cnt, g_log_count, IRQ_USART_B, STATUS_EIR);
    rir_cnt = mon_count_events(start_cnt, g_log_count, IRQ_USART_B, STATUS_RIR);

    sc_put_str("  EIR_B count="); sc_put_hex(eir_cnt);
    sc_put_str("  RIR_B count="); sc_put_hex(rir_cnt); sc_put_str("\r\n");

    if (eir_cnt == STR_N_OVR && rir_cnt == STR_N_OVR)
        sc_pass("STR-003 Overrun+recovery 10 cycles: EIR and RIR counts correct");
    else
        sc_fail("STR-003", "EIR=10 RIR=10", "count mismatch",
                "VP model overrun/recovery cycle counts do not match expected");
}

/* ── main entry point ────────────────────────────────────────────────────── */
void isr_main(void)
{
    sc_banner("USART2 STRESS TESTS (STR-001..003)");
    env_init();

    str_001_sequential();
    str_002_alternating();
    str_003_overrun_recovery_cycle();

    sc_summary("stress");
}
