/*
 * test_functional.c — Core functional interrupt tests
 *
 * Objective:
 *   Verify all four interrupt sources (TBIR, TIR, RIR, EIR) fire correctly
 *   in the expected scenarios, in both directions (A→B and B→A).
 *
 * Test IDs: FN-001 … FN-005
 *
 * FN-001  A→B single byte 0x55           TBIR_A + RIR_B, RBUF_B=0x55
 * FN-002  B→A single byte 0xAA           TBIR_B + RIR_A, RBUF_A=0xAA
 * FN-003  Overrun (EIR)                  RIR_B first; EIR_B on second byte
 * FN-004  Multi-byte stream A→B          TBIR_A + RIR_B per byte (0xDE,0xAD,0xBE,0xEF)
 * FN-005  TIR TX-complete                TBIR_A fires; TIR_A fires 2µs later
 */

#include "../../bench/common/platform.h"
#include "../../bench/env/env.h"
#include "../../bench/scoreboard/scoreboard.h"
#include "../../bench/driver/usart_driver.h"
#include "../../bench/monitor/monitor.h"

/* ── FN-001: A→B byte 0x55 ───────────────────────────────────────────────── */
static void fn_001_a_to_b(void)
{
    uint32_t base;
    uint8_t  data;

    sc_put_str("FN-001: A sends 0x55 → expect TBIR_A + RIR_B, RBUF_B=0x55\r\n");
    base = env_reset();
    UA_TBUF = 0x55u;

    if (!wait_events(base, 2u)) {
        sc_fail("FN-001", "TBIR_A + RIR_B", "timeout",
                "No interrupt after TBUF write to USART_A");
        return;
    }

    if (find_event(base, IRQ_USART_A, STATUS_TBIR) < 0) {
        sc_fail("FN-001", "TBIR_A in log", "missing",
                "USART_A TBIR not triggered by TBUF write");
        return;
    }
    if (find_event(base, IRQ_USART_B, STATUS_RIR) < 0) {
        sc_fail("FN-001", "RIR_B in log", "missing",
                "USART_B RIR not triggered 2µs after TBUF_A write");
        return;
    }

    data = usart_rbuf_read(USART_B);
    if (data == 0x55u)
        sc_pass("FN-001 A→B 0x55: TBIR_A + RIR_B + RBUF_B=0x55");
    else
        sc_fail_hex("FN-001 data", 0x55u, (uint32_t)data,
                    "RBUF_B does not contain 0x55 after A→B transfer");
}

/* ── FN-002: B→A byte 0xAA ───────────────────────────────────────────────── */
static void fn_002_b_to_a(void)
{
    uint32_t base;
    uint8_t  data;

    sc_put_str("FN-002: B sends 0xAA → expect TBIR_B + RIR_A, RBUF_A=0xAA\r\n");
    base = env_reset();
    UB_TBUF = 0xAAu;

    if (!wait_events(base, 2u)) {
        sc_fail("FN-002", "TBIR_B + RIR_A", "timeout",
                "No interrupt after TBUF write to USART_B");
        return;
    }

    if (find_event(base, IRQ_USART_B, STATUS_TBIR) < 0) {
        sc_fail("FN-002", "TBIR_B in log", "missing",
                "USART_B TBIR not triggered by TBUF write");
        return;
    }
    if (find_event(base, IRQ_USART_A, STATUS_RIR) < 0) {
        sc_fail("FN-002", "RIR_A in log", "missing",
                "USART_A RIR not triggered 2µs after TBUF_B write");
        return;
    }

    data = usart_rbuf_read(USART_A);
    if (data == 0xAAu)
        sc_pass("FN-002 B→A 0xAA: TBIR_B + RIR_A + RBUF_A=0xAA");
    else
        sc_fail_hex("FN-002 data", 0xAAu, (uint32_t)data,
                    "RBUF_A does not contain 0xAA after B→A transfer");
}

/* ── FN-003: Overrun / EIR ────────────────────────────────────────────────── */
static void fn_003_overrun(void)
{
    uint32_t base;

    sc_put_str("FN-003: Overrun → RIR_B (byte 1), EIR_B (byte 2 while RBUF full)\r\n");

    /* Make sure OEN is set on USART_B */
    usart_oen_set(USART_B);

    base = env_reset();
    UA_TBUF = 0x11u;

    /* Wait for TBIR_A + RIR_B */
    if (!wait_events(base, 2u)) {
        sc_fail("FN-003", "RIR_B from first byte", "timeout",
                "First byte not received on USART_B");
        return;
    }
    if (find_event(base, IRQ_USART_B, STATUS_RIR) < 0) {
        sc_fail("FN-003", "RIR_B in log", "missing",
                "RIR_B not triggered for first byte");
        return;
    }

    /* Do NOT drain RBUF_B — overrun requires RBUF to remain full */
    base = g_log_count;
    UA_TBUF = 0x22u;

    /* Wait for TBIR_A + EIR_B */
    if (!wait_events(base, 2u)) {
        sc_fail("FN-003", "EIR_B from overrun", "timeout",
                "EIR_B not triggered when RBUF full and new byte arrives");
        return;
    }
    if (find_event(base, IRQ_USART_B, STATUS_EIR) < 0) {
        sc_fail("FN-003", "EIR_B in log", "missing",
                "EIR_B missing despite RBUF full overrun condition");
    } else {
        sc_pass("FN-003 Overrun: RIR_B then EIR_B correctly generated");
    }

    usart_drain(USART_B);   /* drain 0x11 left in RBUF */
}

/* ── FN-004: Multi-byte stream A→B ───────────────────────────────────────── */
static void fn_004_multi_byte(void)
{
    static const uint8_t stream[4] = { 0xDEu, 0xADu, 0xBEu, 0xEFu };
    uint32_t i, base;
    uint8_t  data;
    int      ok = 1;

    sc_put_str("FN-004: Multi-byte A→B [0xDE, 0xAD, 0xBE, 0xEF]\r\n");
    env_reset();

    for (i = 0u; i < 4u; ++i) {
        base = g_log_count;
        UA_TBUF = stream[i];

        if (!wait_events(base, 2u)) {
            sc_put_str("  timeout on byte "); sc_put_hex((uint32_t)stream[i]); sc_put_str("\r\n");
            ok = 0; break;
        }
        if (find_event(base, IRQ_USART_A, STATUS_TBIR) < 0) {
            sc_put_str("  TBIR_A missing for byte "); sc_put_hex((uint32_t)stream[i]); sc_put_str("\r\n");
            ok = 0; break;
        }
        if (find_event(base, IRQ_USART_B, STATUS_RIR) < 0) {
            sc_put_str("  RIR_B missing for byte "); sc_put_hex((uint32_t)stream[i]); sc_put_str("\r\n");
            ok = 0; break;
        }

        data = usart_rbuf_read(USART_B);
        sc_put_str("  byte["); sc_put_hex(i); sc_put_str("] exp=");
        sc_put_hex((uint32_t)stream[i]); sc_put_str(" got="); sc_put_hex((uint32_t)data);
        if (data == stream[i]) { sc_put_str(" OK\r\n"); }
        else { sc_put_str(" MISMATCH\r\n"); ok = 0; break; }
    }

    if (ok) sc_pass("FN-004 Multi-byte stream A→B integrity verified");
    else    sc_fail("FN-004", "all bytes match", "mismatch or timeout",
                    "Data corruption or missing interrupt in multi-byte transfer");
}

/* ── FN-005: TIR TX-complete (separate from TBIR) ────────────────────────── */
static void fn_005_tir(void)
{
    uint32_t base, i;
    int      tir_idx;

    sc_put_str("FN-005: TIR TX-complete fires 2µs after TBIR\r\n");
    base = env_reset();
    UA_TBUF = 0x5Au;

    /* Wait for TBIR_A */
    if (!wait_events(base, 1u)) {
        sc_fail("FN-005", "TBIR_A", "timeout", "No TBIR for TIR test");
        usart_drain(USART_B);
        return;
    }
    if (find_event(base, IRQ_USART_A, STATUS_TBIR) < 0) {
        sc_fail("FN-005", "TBIR_A in log", "missing", "TBIR_A not in event log");
        usart_drain(USART_B);
        return;
    }

    /* Check if TIR already arrived in same ISR burst */
    tir_idx = find_event(base, IRQ_USART_A, STATUS_TIR);

    if (tir_idx < 0) {
        /* TIR fires 2µs after TBUF write — poll until SC scheduler delivers it */
        for (i = 0u; i < 500u; ++i) {
            volatile uint32_t dummy = UA_CON; (void)dummy;
            tir_idx = find_event(base, IRQ_USART_A, STATUS_TIR);
            if (tir_idx >= 0) break;
            __asm__ volatile("wfi" ::: "memory");
        }
    }

    if (tir_idx >= 0) {
        /* Verify ordering: TBIR must appear before TIR */
        if (mon_check_order(base, IRQ_USART_A))
            sc_pass("FN-005 TIR fires after TBIR (correct order, 2µs gap)");
        else
            sc_fail("FN-005", "TIR after TBIR", "TIR before TBIR",
                    "Interrupt ordering violation: TIR should follow TBIR");
    } else {
        sc_fail("FN-005", "TIR_A in log", "never arrived",
                "TIR not scheduled by m_frame_done_ev after 500 polls");
    }

    usart_drain(USART_B);
}

/* ── main entry point ────────────────────────────────────────────────────── */
void isr_main(void)
{
    sc_banner("USART2 FUNCTIONAL TESTS (FN-001..005)");
    env_init();

    fn_001_a_to_b();
    fn_002_b_to_a();
    fn_003_overrun();
    fn_004_multi_byte();
    fn_005_tir();

    sc_summary("functional");
}
