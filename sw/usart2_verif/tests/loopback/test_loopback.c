/*
 * test_loopback.c — Back-to-back data integrity (A→B and B→A paths)
 *
 * Objective:
 *   Verify full round-trip data integrity through the sc_fifo back-to-back
 *   wiring in the VP.  The VP wires USART_A.tx → USART_B.rx and vice-versa.
 *
 * Note: The VP model does not implement CON.LB (loopback mode) functionally —
 *       CON.LB is stored but has no effect on sc_fifo routing.  This test
 *       verifies the physical back-to-back path instead, and logs CON.LB as
 *       a finding.
 *
 * Test IDs: LB-001 … LB-004
 *
 * LB-001  A→B: 8 boundary byte values, verify RBUF_B integrity
 * LB-002  B→A: 8 boundary byte values, verify RBUF_A integrity
 * LB-003  A→B→A round-trip echo: B reads and re-sends to A
 * LB-004  CON.LB bit stored/readback (VP model no-op finding)
 */

#include "../../bench/common/platform.h"
#include "../../bench/env/env.h"
#include "../../bench/scoreboard/scoreboard.h"
#include "../../bench/driver/usart_driver.h"
#include "../../bench/monitor/monitor.h"

static const uint8_t BOUNDARY[8] = {
    0x00u, 0x01u, 0x7Fu, 0x80u, 0x55u, 0xAAu, 0xFEu, 0xFFu
};

/* ── LB-001: A→B boundary values ────────────────────────────────────────── */
static void lb_001_a_to_b(void)
{
    uint32_t i, base;
    uint8_t  data;
    int      ok = 1;

    sc_put_str("LB-001: A→B boundary byte values\r\n");
    env_reset();

    for (i = 0u; i < 8u; ++i) {
        base = g_log_count;
        UA_TBUF = BOUNDARY[i];

        if (!wait_events(base, 2u)) {
            sc_put_str("  timeout on "); sc_put_hex((uint32_t)BOUNDARY[i]); sc_put_str("\r\n");
            ok = 0; break;
        }

        data = usart_rbuf_read(USART_B);
        if (data != BOUNDARY[i]) {
            sc_put_str("  mismatch exp="); sc_put_hex((uint32_t)BOUNDARY[i]);
            sc_put_str(" got="); sc_put_hex((uint32_t)data); sc_put_str("\r\n");
            ok = 0; break;
        }
    }

    if (ok) sc_pass("LB-001 A→B boundary bytes all correct");
    else    sc_fail("LB-001", "all 8 boundary bytes match", "mismatch or timeout",
                    "sc_fifo A→B data corruption or delivery failure");
}

/* ── LB-002: B→A boundary values ────────────────────────────────────────── */
static void lb_002_b_to_a(void)
{
    uint32_t i, base;
    uint8_t  data;
    int      ok = 1;

    sc_put_str("LB-002: B→A boundary byte values\r\n");
    env_reset();

    for (i = 0u; i < 8u; ++i) {
        base = g_log_count;
        UB_TBUF = BOUNDARY[i];

        if (!wait_events(base, 2u)) {
            sc_put_str("  timeout on "); sc_put_hex((uint32_t)BOUNDARY[i]); sc_put_str("\r\n");
            ok = 0; break;
        }

        data = usart_rbuf_read(USART_A);
        if (data != BOUNDARY[i]) {
            sc_put_str("  mismatch exp="); sc_put_hex((uint32_t)BOUNDARY[i]);
            sc_put_str(" got="); sc_put_hex((uint32_t)data); sc_put_str("\r\n");
            ok = 0; break;
        }
    }

    if (ok) sc_pass("LB-002 B→A boundary bytes all correct");
    else    sc_fail("LB-002", "all 8 boundary bytes match", "mismatch or timeout",
                    "sc_fifo B→A data corruption or delivery failure");
}

/* ── LB-003: Echo round-trip A→B→A ──────────────────────────────────────── */
static void lb_003_round_trip_echo(void)
{
    uint32_t base;
    uint8_t  echo;

    sc_put_str("LB-003: Round-trip echo A→B→A (0xE5)\r\n");

    /* Step 1: A sends 0xE5 → B receives */
    base = g_log_count;
    UA_TBUF = 0xE5u;
    if (!wait_events(base, 2u)) {
        sc_fail("LB-003", "RIR_B from A", "timeout", "A→B transfer timeout");
        return;
    }

    /* Step 2: B reads and echoes back to A */
    echo = usart_rbuf_read(USART_B);
    if (echo != 0xE5u) {
        sc_fail_hex("LB-003 B RBUF", 0xE5u, (uint32_t)echo,
                    "Echo byte corrupted in A→B leg");
        return;
    }

    /* Step 3: B sends echo → A receives */
    base = g_log_count;
    UB_TBUF = echo;
    if (!wait_events(base, 2u)) {
        sc_fail("LB-003", "RIR_A from B echo", "timeout", "B→A echo transfer timeout");
        return;
    }

    echo = usart_rbuf_read(USART_A);
    if (echo == 0xE5u)
        sc_pass("LB-003 Round-trip echo A→B→A: data integrity preserved");
    else
        sc_fail_hex("LB-003 A RBUF after echo", 0xE5u, (uint32_t)echo,
                    "Round-trip echo byte corrupted in B→A leg");
}

/* ── LB-004: CON.LB bit stored/read (no-op in VP model) ─────────────────── */
static void lb_004_con_lb_bit(void)
{
    uint32_t con_with_lb, con_readback;

    sc_put_str("LB-004: CON.LB bit stored and read back\r\n");

    con_with_lb = CON_INIT | CON_LB;
    usart_con_write(USART_A, con_with_lb);
    con_readback = usart_con_read(USART_A);

    if (con_readback == con_with_lb) {
        sc_log_finding("LB-004",
            "CON.LB=1 must connect TSR output to RSR input (internal loopback)",
            "VP model stores CON.LB but sc_fifo routing is hard-wired in sc_main; "
            "CON.LB has no functional effect on byte delivery path",
            "In Usart2::tx_deliver_method, check m_con & CON_LB and if set, "
            "write to rx_port of self instead of tx_port");
        sc_pass("LB-004 CON.LB bit stored (VP routing unchanged — finding LB-004 logged)");
    } else {
        sc_fail_hex("LB-004 CON.LB readback", con_with_lb, con_readback,
                    "CON.LB bit not stored correctly in VP model");
    }

    usart_con_write(USART_A, CON_INIT);
}

/* ── main entry point ────────────────────────────────────────────────────── */
void isr_main(void)
{
    sc_banner("USART2 LOOPBACK / PATH TESTS (LB-001..004)");
    env_init();

    lb_001_a_to_b();
    lb_002_b_to_a();
    lb_003_round_trip_echo();
    lb_004_con_lb_bit();

    sc_summary("loopback");
}
