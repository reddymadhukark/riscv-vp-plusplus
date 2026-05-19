/*
 * test_performance.c — Throughput and latency measurement
 *
 * Objective:
 *   Measure: bytes-per-interrupt, sustained transfer rate, TBIR latency,
 *   and RIR latency relative to TBIR.
 *
 * Test IDs: PERF-001 … PERF-004
 *
 * PERF-001  Throughput: send 32 bytes, verify no loss (2 interrupts/byte)
 * PERF-002  TBIR latency: TBIR arrives within 1 WFI cycle of TBUF write
 * PERF-003  RIR latency: RIR always follows TBIR (frame delivery ordering)
 * PERF-004  Interrupt rate: 32 bytes → 64+ interrupt events (TBIR+RIR each)
 */

#include "../../bench/common/platform.h"
#include "../../bench/env/env.h"
#include "../../bench/scoreboard/scoreboard.h"
#include "../../bench/driver/usart_driver.h"
#include "../../bench/monitor/monitor.h"

#define PERF_N  32u    /* number of bytes for throughput test */

/* ── PERF-001: 32-byte transfer with no data loss ────────────────────────── */
static void perf_001_throughput(void)
{
    uint32_t i, base, start_cnt;
    uint8_t  data;
    int      ok = 1;

    sc_put_str("PERF-001: 32-byte A→B throughput (no loss)\r\n");
    env_reset();
    start_cnt = g_log_count;

    for (i = 0u; i < PERF_N; ++i) {
        base = g_log_count;
        UA_TBUF = (uint8_t)(i & 0xFFu);

        if (!wait_rir(base, IRQ_USART_B)) {
            sc_put_str("  timeout at byte "); sc_put_hex(i); sc_put_str("\r\n");
            ok = 0; break;
        }

        data = usart_rbuf_read(USART_B);
        if (data != (uint8_t)(i & 0xFFu)) {
            sc_put_str("  data loss at byte "); sc_put_hex(i);
            sc_put_str(" exp="); sc_put_hex((uint32_t)(i & 0xFFu));
            sc_put_str(" got="); sc_put_hex((uint32_t)data); sc_put_str("\r\n");
            ok = 0; break;
        }
    }

    sc_put_str("  total events="); sc_put_hex(g_log_count - start_cnt); sc_put_str("\r\n");

    if (ok) sc_pass("PERF-001 32-byte throughput: no data loss");
    else    sc_fail("PERF-001", "32 bytes received correctly", "loss or timeout",
                    "sc_fifo dropped bytes or interrupt not delivered in time");
}

/* ── PERF-002: TBIR latency — must arrive within 1 WFI of TBUF write ─────── */
static void perf_002_tbir_latency(void)
{
    uint32_t base, count_before, count_after;

    sc_put_str("PERF-002: TBIR arrives within first WFI after TBUF write\r\n");

    base = env_reset();
    count_before = g_log_count;

    UA_TBUF = 0xA5u;
    __asm__ volatile("wfi" ::: "memory");   /* one WFI */

    count_after = g_log_count;

    if (count_after > count_before &&
        find_event(base, IRQ_USART_A, STATUS_TBIR) >= 0) {
        sc_pass("PERF-002 TBIR arrives within 1 WFI of TBUF write");
    } else {
        sc_fail("PERF-002", "TBIR within 1 WFI", "not yet arrived",
                "VP ISS TLM quantum too large — TBIR delayed beyond 1 WFI");
    }
    wait_rir(base, IRQ_USART_B);
    usart_drain(USART_B);
}

/* ── PERF-003: RIR always follows TBIR across 16 transfers ──────────────── */
static void perf_003_rir_ordering(void)
{
    uint32_t i, base;
    int      ok = 1;

    sc_put_str("PERF-003: RIR follows TBIR on all 16 transfers\r\n");
    env_reset();

    for (i = 0u; i < 16u; ++i) {
        int tbir_idx, rir_idx;
        base = g_log_count;
        UA_TBUF = (uint8_t)(0x20u + i);

        /*
         * Wait for RIR_B specifically, not for a count of 2.
         * wait_events(base,2) can satisfy with TBIR_A + TIR_A before RIR_B
         * is logged, or can pick up a stale RIR from the previous iteration
         * if it fires slightly after the new base is recorded.
         */
        if (!wait_rir(base, IRQ_USART_B)) { ok = 0; break; }

        tbir_idx = find_event(base, IRQ_USART_A, STATUS_TBIR);
        rir_idx  = find_event(base, IRQ_USART_B, STATUS_RIR);

        if (tbir_idx < 0 || rir_idx < 0 || rir_idx < tbir_idx) {
            sc_put_str("  ordering violation byte "); sc_put_hex(i); sc_put_str("\r\n");
            ok = 0;
        }
        usart_drain(USART_B);
    }

    if (ok) sc_pass("PERF-003 RIR after TBIR on all 16 transfers");
    else    sc_fail("PERF-003", "RIR after TBIR always", "ordering violated",
                    "Frame delivery ordering not guaranteed by VP model");
}

/* ── PERF-004: Interrupt count — 32 bytes → ≥64 events (TBIR+RIR per byte)  */
static void perf_004_irq_count(void)
{
    uint32_t i, base, start_cnt, total_events;
    uint32_t tbir_cnt = 0u, rir_cnt = 0u;

    sc_put_str("PERF-004: 32 bytes → ≥64 interrupt events (TBIR+RIR each)\r\n");
    env_reset();
    start_cnt = g_log_count;

    for (i = 0u; i < PERF_N; ++i) {
        base = g_log_count;
        UA_TBUF = (uint8_t)(0x40u + i);
        wait_rir(base, IRQ_USART_B);
        usart_drain(USART_B);
    }

    total_events = g_log_count - start_cnt;
    tbir_cnt = mon_count_events(start_cnt, g_log_count, IRQ_USART_A, STATUS_TBIR);
    rir_cnt  = mon_count_events(start_cnt, g_log_count, IRQ_USART_B, STATUS_RIR);

    sc_put_str("  total="); sc_put_hex(total_events);
    sc_put_str("  TBIR_A="); sc_put_hex(tbir_cnt);
    sc_put_str("  RIR_B="); sc_put_hex(rir_cnt); sc_put_str("\r\n");

    if (tbir_cnt == PERF_N && rir_cnt == PERF_N)
        sc_pass("PERF-004 Interrupt count correct: 32 TBIR_A + 32 RIR_B");
    else {
        sc_put_str("  Expected each=32, got TBIR="); sc_put_hex(tbir_cnt);
        sc_put_str(" RIR="); sc_put_hex(rir_cnt); sc_put_str("\r\n");
        sc_fail("PERF-004", "32 TBIR + 32 RIR", "count mismatch",
                "Interrupt count does not match byte count — possible lost events");
    }
}

/* ── main entry point ────────────────────────────────────────────────────── */
void isr_main(void)
{
    sc_banner("USART2 PERFORMANCE TESTS (PERF-001..004)");
    env_init();

    perf_001_throughput();
    perf_002_tbir_latency();
    perf_003_rir_ordering();
    perf_004_irq_count();

    sc_summary("performance");
}
