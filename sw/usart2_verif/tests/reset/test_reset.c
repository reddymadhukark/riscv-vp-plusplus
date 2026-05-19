/*
 * test_reset.c — Hardware Reset Pin (RES) verification
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * SPECIFICATION REFERENCE  (USART_Specification_v4.pdf — Table 1, Port List)
 *
 *   Port  Dir    Description
 *   RES   Input  Reset — clears ALL registers and internal state.
 *
 * The spec mandates a hardware RES input pin. In the VP, this pin must be
 * reachable by firmware via a Software Reset Register (SWRST) at offset 0x14.
 * Writing 0x1 to SWRST must:
 *   • Clear CON     → 0x00000000
 *   • Clear STATUS  → 0x00000000
 *   • Clear BG      → 0x00000000
 *   • Clear FDR     → 0x00000000
 *   • Clear rbuf_full / m_rbuf  (RBUF returns to idle)
 *   • Abort any in-progress frame; hold TXD high (idle)
 * The register is self-clearing (reads back 0 after reset completes).
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * TEST STRUCTURE
 *
 *   RST-001  Power-on reset values (read BEFORE env_init)
 *            → Verifies the VP model powers up in spec-correct state.
 *            → Expected: PASS.
 *
 *   RST-002  HW reset clears CON
 *   RST-003  HW reset clears STATUS
 *   RST-004  HW reset clears RBUF / rbuf_full state
 *   RST-005  HW reset during an in-progress transfer
 *            → All four trigger SWRST then verify registers = reset values.
 *            → Expected: ALL FAIL.
 *            → Captured as: BUG-007 RESET_HW_PIN_NOT_IMPLEMENTED
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * WHY THE TESTS FAIL
 *
 *   The Usart2 SystemC model (usart2.h) has no RES port and no SWRST register.
 *   Offset 0x14 falls into the default: case of b_transport, which is a no-op.
 *   Writing to SWRST has zero effect — registers retain their written values.
 *
 *   Bug entry: BUG-007
 *   Severity : High
 *   Fix      : Add sc_in<bool> rst port to Usart2; or expose a SWRST register
 *              at OFF_SWRST = 0x14. On write of 1: m_con=0, m_status=0,
 *              m_rbuf=0, m_rbuf_full=false, cancel m_frame_done_ev.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "../../bench/common/platform.h"
#include "../../bench/env/env.h"
#include "../../bench/scoreboard/scoreboard.h"
#include "../../bench/driver/usart_driver.h"
#include "../../bench/monitor/monitor.h"

/*
 * SWRST — Software Reset Register
 * Offset : 0x14  (between FDR@0x10 and STATUS@0x20)
 * Access : Write-only  (write 0x1 → assert RES pin → reset all state)
 * Spec   : Spec Table 1 defines RES as a hardware input pin; SWRST is the
 *          firmware-accessible equivalent required in VP.
 * VP     : NOT IMPLEMENTED — falls into default: no-op in b_transport.
 */
#define OFF_SWRST   0x14UL
#define SWRST_TRIG  0x1u          /* value that asserts reset                */

#define UA_SWRST    MMIO32(UA_BASE + OFF_SWRST)
#define UB_SWRST    MMIO32(UB_BASE + OFF_SWRST)

/* Spec-defined reset values */
#define RST_CON     0x00000000u
#define RST_STATUS  0x00000000u
#define RST_BG      0x00000000u
#define RST_FDR     0x00000000u

/* Helper: trigger hardware reset on one USART instance */
static void trigger_hw_reset(UsartInst u)
{
    /*
     * Write to SWRST register — in a correct implementation this synchronously
     * clears all register state inside b_transport (same as RES pin assertion).
     * NO WFI here: SWRST is not implemented (no-op), so there is no interrupt
     * to wait for.  A WFI would block forever (no pending interrupt → SystemC
     * runs out of events → sc_start returns silently).
     */
    if (u == USART_A) UA_SWRST = SWRST_TRIG;
    else              UB_SWRST = SWRST_TRIG;
    /* fence ensures the write is committed before the following register reads */
    __asm__ volatile("fence" ::: "memory");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RST-001 — Power-on reset values
 *
 * Reads all spec-defined registers on BOTH instances BEFORE env_init()
 * (i.e. before any firmware write has touched the peripheral).
 *
 * This test is expected to PASS — it confirms the VP initialises registers
 * to their spec-defined reset values at simulation start.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void rst_001_power_on_reset(void)
{
    uint32_t con_a, con_b;
    uint32_t sta_a, sta_b;
    uint32_t bg_a,  bg_b;
    uint32_t fdr_a, fdr_b;
    int pass = 1;

    sc_put_str("RST-001: Power-on reset values (read before env_init)\r\n");

    /*
     * Read registers in their untouched state.
     * No env_init(), no CON write, no TBUF write has happened yet.
     */
    con_a  = usart_con_read(USART_A);
    con_b  = usart_con_read(USART_B);
    sta_a  = usart_status_read(USART_A);
    sta_b  = usart_status_read(USART_B);
    bg_a   = usart_bg_read(USART_A);
    bg_b   = usart_bg_read(USART_B);
    fdr_a  = usart_fdr_read(USART_A);
    fdr_b  = usart_fdr_read(USART_B);

    sc_put_str("  UA_CON=");    sc_put_hex(con_a);
    sc_put_str("  UB_CON=");    sc_put_hex(con_b);    sc_put_str("\r\n");
    sc_put_str("  UA_STATUS="); sc_put_hex(sta_a);
    sc_put_str("  UB_STATUS="); sc_put_hex(sta_b);    sc_put_str("\r\n");
    sc_put_str("  UA_BG=");     sc_put_hex(bg_a);
    sc_put_str("  UB_BG=");     sc_put_hex(bg_b);     sc_put_str("\r\n");
    sc_put_str("  UA_FDR=");    sc_put_hex(fdr_a);
    sc_put_str("  UB_FDR=");    sc_put_hex(fdr_b);    sc_put_str("\r\n");

    /* CON */
    if (con_a != RST_CON) {
        sc_fail_hex("RST-001 UA_CON at power-on", RST_CON, con_a,
                    "CON must reset to 0x00000000 per spec");
        pass = 0;
    }
    if (con_b != RST_CON) {
        sc_fail_hex("RST-001 UB_CON at power-on", RST_CON, con_b,
                    "CON must reset to 0x00000000 per spec");
        pass = 0;
    }

    /* STATUS */
    if (sta_a != RST_STATUS) {
        sc_fail_hex("RST-001 UA_STATUS at power-on", RST_STATUS, sta_a,
                    "STATUS must reset to 0x00000000 per spec");
        pass = 0;
    }
    if (sta_b != RST_STATUS) {
        sc_fail_hex("RST-001 UB_STATUS at power-on", RST_STATUS, sta_b,
                    "STATUS must reset to 0x00000000 per spec");
        pass = 0;
    }

    /* BG — note VP no-op; read will be 0 but for different reason */
    if (bg_a != RST_BG || bg_b != RST_BG) {
        sc_log_finding("RST-001-BG",
            "BG reset value must be 0x00000000 per spec",
            "BG register not implemented in VP (no-op); returns 0 by default",
            "Implement OFF_BG case in b_transport; ensure reset path clears it");
        /* Not a hard fail — VP returns 0 coincidentally */
    }

    /* FDR */
    if (fdr_a != RST_FDR || fdr_b != RST_FDR) {
        sc_log_finding("RST-001-FDR",
            "FDR reset value must be 0x00000000 per spec",
            "FDR register not implemented in VP (no-op); returns 0 by default",
            "Implement OFF_FDR case in b_transport; ensure reset path clears it");
    }

    if (pass)
        sc_pass("RST-001 All registers read spec-correct reset values at power-on");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RST-002 — HW reset must clear CON
 *
 * Stimulus:
 *   1. Write a non-zero value to CON (CON_INIT = 0x8049)
 *   2. Verify CON stores it (confirms write worked)
 *   3. Assert SWRST on the instance
 *   4. Read CON — spec requires it to return to 0x00000000
 *
 * Expected: FAIL — SWRST write is a no-op; CON retains 0x8049.
 * Bug: BUG-007 RESET_HW_PIN_NOT_IMPLEMENTED
 * ═══════════════════════════════════════════════════════════════════════════ */
static void rst_002_hw_reset_clears_con(void)
{
    uint32_t con_before, con_after;

    sc_put_str("RST-002: HW reset must clear CON → 0x00000000\r\n");

    /* Step 1: put CON in a non-reset state */
    usart_con_write(USART_A, CON_INIT);        /* write 0x8049 */
    con_before = usart_con_read(USART_A);
    sc_put_str("  CON before SWRST="); sc_put_hex(con_before); sc_put_str("\r\n");

    /* Step 2: assert hardware reset via SWRST register */
    sc_put_str("  Asserting SWRST (offset 0x14) ...\r\n");
    trigger_hw_reset(USART_A);

    /* Step 3: read CON — must be 0x00000000 per spec */
    con_after = usart_con_read(USART_A);
    sc_put_str("  CON after  SWRST="); sc_put_hex(con_after);  sc_put_str("\r\n");

    if (con_after == RST_CON) {
        sc_pass("RST-002 CON cleared to 0x00000000 by HW reset");
    } else {
        sc_fail_hex("RST-002 CON after HW reset", RST_CON, con_after,
                    "BUG-007: RESET_HW_PIN_NOT_IMPLEMENTED — "
                    "SWRST (offset 0x14) is a no-op in VP model; "
                    "CON must be cleared by asserting the RES pin");
    }

    /* Restore for subsequent tests */
    usart_con_write(USART_A, CON_INIT);
    usart_con_write(USART_B, CON_INIT);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RST-003 — HW reset must clear STATUS (including in-flight interrupt bits)
 *
 * Stimulus:
 *   1. Write TBUF_A → TBIR fires → STATUS_A.TBIR = 1
 *   2. Assert SWRST on USART_A
 *   3. Read STATUS_A — spec requires 0x00000000 (all flags cleared)
 *
 * Expected: FAIL — SWRST is a no-op; STATUS retains TBIR (and possibly TIR).
 * Bug: BUG-007 RESET_HW_PIN_NOT_IMPLEMENTED
 * ═══════════════════════════════════════════════════════════════════════════ */
static void rst_003_hw_reset_clears_status(void)
{
    uint32_t base, status_after;

    sc_put_str("RST-003: HW reset must clear STATUS → 0x00000000\r\n");

    uint32_t pre_base = g_log_count;   /* captured before TBUF write */

    /*
     * Keep MIE DISABLED throughout the STATUS check so the ISR cannot clear
     * STATUS_TBIR before we read it.  TBIR fires synchronously in b_transport,
     * so STATUS_TBIR is set the moment UA_TBUF is written.
     */
    __asm__ volatile("csrc mstatus, %0" :: "r"(1u << 3));   /* MIE = 0 */

    UA_STATUS = STATUS_ALL;   /* clean slate (W1C all bits) */
    UA_TBUF   = 0xBBu;        /* TBIR fires — ISR blocked */

    /* Read STATUS while ISR is still blocked — TBIR must be set */
    sc_put_str("  STATUS before SWRST="); sc_put_hex(usart_status_read(USART_A));
    sc_put_str("\r\n");

    /* Assert SWRST — spec: must clear all STATUS bits */
    sc_put_str("  Asserting SWRST (offset 0x14) ...\r\n");
    trigger_hw_reset(USART_A);

    /* Read STATUS immediately after SWRST (ISR still blocked) */
    status_after = usart_status_read(USART_A);
    sc_put_str("  STATUS after  SWRST="); sc_put_hex(status_after); sc_put_str("\r\n");

    __asm__ volatile("csrs mstatus, %0" :: "r"(1u << 3));   /* MIE = 1 */

    if (status_after == RST_STATUS) {
        sc_pass("RST-003 STATUS cleared to 0x00000000 by HW reset");
    } else {
        sc_fail_hex("RST-003 STATUS after HW reset", RST_STATUS, status_after,
                    "BUG-007: RESET_HW_PIN_NOT_IMPLEMENTED — "
                    "SWRST (offset 0x14) is a no-op; STATUS_TBIR survives; "
                    "RES pin must clear m_status = 0 in VP model");
    }

    /* Drain: use pre_base captured before TBUF write */
    wait_rir(pre_base, IRQ_USART_B);
    UA_STATUS = STATUS_ALL;
    usart_drain(USART_B);
    (void)base;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RST-004 — HW reset must clear RBUF / rbuf_full state
 *
 * Stimulus:
 *   1. Send byte A→B so RBUF_B is filled (m_rbuf_full = true)
 *   2. Assert SWRST on USART_B (the receiver)
 *   3. Send another byte A→B
 *   4. Check whether RIR_B fires (it fires iff rbuf_full was cleared by reset)
 *
 * If reset works:  rbuf_full=false after SWRST → new byte raises RIR_B  → PASS
 * If reset absent: rbuf_full=true  after SWRST → new byte raises EIR_B  → FAIL
 *
 * Expected: FAIL — SWRST is a no-op; rbuf_full remains true; EIR fires instead.
 * Bug: BUG-007 RESET_HW_PIN_NOT_IMPLEMENTED
 * ═══════════════════════════════════════════════════════════════════════════ */
static void rst_004_hw_reset_clears_rbuf_state(void)
{
    uint32_t base;

    sc_put_str("RST-004: HW reset must clear rbuf_full state (RBUF returns to idle)\r\n");

    /* Ensure OEN=1 so EIR fires if overrun occurs (proves rbuf_full was NOT cleared) */
    usart_oen_set(USART_B);

    base = env_reset();

    /* Step 1: fill RBUF_B — wait for RIR_B specifically so m_rbuf is valid */
    UA_TBUF = 0xC1u;
    if (!wait_rir(base, IRQ_USART_B)) {
        sc_fail("RST-004", "RIR_B from first byte", "timeout",
                "Could not fill RBUF_B before reset test");
        return;
    }
    sc_put_str("  RBUF_B filled (rbuf_full=true)\r\n");
    /* Do NOT drain — leave rbuf_full = true */

    /* Step 2: assert SWRST on USART_B — should clear rbuf_full */
    sc_put_str("  Asserting SWRST on USART_B (offset 0x14) ...\r\n");
    trigger_hw_reset(USART_B);

    /* Step 3: send a new byte A→B */
    base = g_log_count;
    UA_TBUF = 0xC2u;
    if (!wait_events(base, 2u)) {
        /*
         * Neither RIR nor EIR arrived. Inconclusive — may indicate
         * SWRST corrupted the module state in an unexpected way.
         */
        sc_fail("RST-004", "RIR_B or EIR_B after SWRST", "no interrupt",
                "BUG-007: RESET_HW_PIN_NOT_IMPLEMENTED — "
                "No interrupt after SWRST + new byte; "
                "module may be in undefined state");
        usart_drain(USART_B);
        usart_con_write(USART_B, CON_INIT);
        return;
    }

    if (find_event(base, IRQ_USART_B, STATUS_RIR) >= 0) {
        /* RIR fired → rbuf_full was cleared by reset → correct behaviour */
        sc_pass("RST-004 RIR_B after SWRST: rbuf_full correctly cleared by HW reset");
        usart_drain(USART_B);
    } else if (find_event(base, IRQ_USART_B, STATUS_EIR) >= 0) {
        /* EIR fired → rbuf_full was NOT cleared → reset did not work */
        sc_fail("RST-004 RBUF state after HW reset",
                "RIR_B (rbuf_full cleared by reset)",
                "EIR_B (rbuf_full still true — reset had no effect)",
                "BUG-007: RESET_HW_PIN_NOT_IMPLEMENTED — "
                "SWRST (offset 0x14) is a no-op; "
                "m_rbuf_full must be set to false in reset handler");
        usart_drain(USART_B);
    } else {
        sc_fail("RST-004", "RIR_B or EIR_B", "neither present",
                "BUG-007: RESET_HW_PIN_NOT_IMPLEMENTED — "
                "Unexpected interrupt state after SWRST");
    }

    usart_con_write(USART_B, CON_INIT);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RST-005 — HW reset during an in-progress transfer must abort cleanly
 *
 * Stimulus:
 *   1. Write TBUF_A  → TBIR fires immediately; byte delivery scheduled 2µs later
 *   2. Immediately assert SWRST on USART_A  (before 2µs delivery fires)
 *   3. After reset, read STATUS_A — must be 0x00000000 (no stale TBIR/TIR)
 *   4. Check USART_B receives NO stale byte  (delivery must be aborted by reset)
 *
 * Expected: FAIL — SWRST is a no-op; TBIR and TIR remain set; byte is delivered.
 * Bug: BUG-007 RESET_HW_PIN_NOT_IMPLEMENTED
 * ═══════════════════════════════════════════════════════════════════════════ */
static void rst_005_hw_reset_mid_transfer(void)
{
    uint32_t base, status_after;
    int      stale_rir;

    sc_put_str("RST-005: HW reset mid-transfer must abort frame and clear state\r\n");

    /* Disable global interrupt so we can sample STATUS before ISR clears it */
    __asm__ volatile("csrc mstatus, %0" :: "r"(1u << 3));

    UA_STATUS = STATUS_ALL;   /* clean slate */
    UB_STATUS = STATUS_ALL;

    /* Step 1: start a transfer (TBIR fires synchronously in b_transport) */
    UA_TBUF = 0xD5u;

    /* Step 2: assert SWRST immediately — before the 2µs delivery event fires */
    sc_put_str("  Asserting SWRST on USART_A mid-transfer ...\r\n");
    UA_SWRST = SWRST_TRIG;   /* no WFI here — want to catch before delivery */

    /* Sample STATUS before re-enabling interrupts */
    status_after = usart_status_read(USART_A);

    /* Re-enable interrupts; capture log base BEFORE any ISR fires */
    base = g_log_count;
    __asm__ volatile("csrs mstatus, %0" :: "r"(1u << 3));

    /*
     * Wait for TBIR_A + TIR_A + RIR_B (3 events).
     * TBIR fires immediately, TIR + byte delivery fire 2µs later.
     * wait_events(base,3) is robust: it counts log entries rather than
     * using bare WFIs that could hang if the event already fired.
     */
    wait_events(base, 3u);

    stale_rir = find_event(base, IRQ_USART_B, STATUS_RIR);

    sc_put_str("  STATUS_A after SWRST="); sc_put_hex(status_after); sc_put_str("\r\n");
    sc_put_str("  Stale RIR_B delivered=");
    sc_put_str(stale_rir >= 0 ? "YES (byte leaked)\r\n" : "NO\r\n");

    if (status_after == RST_STATUS && stale_rir < 0) {
        sc_pass("RST-005 STATUS cleared and byte aborted by HW reset mid-transfer");
    } else {
        /* Build a meaningful failure description */
        if (status_after != RST_STATUS) {
            sc_fail_hex("RST-005 STATUS not cleared mid-transfer reset",
                        RST_STATUS, status_after,
                        "BUG-007: RESET_HW_PIN_NOT_IMPLEMENTED — "
                        "SWRST (offset 0x14) is a no-op; "
                        "in-progress TBIR/TIR bits survive; "
                        "reset must cancel m_frame_done_ev and zero m_status");
        }
        if (stale_rir >= 0) {
            sc_fail("RST-005 Stale byte delivered after mid-transfer reset",
                    "No byte delivered to USART_B after reset",
                    "Stale byte arrived at USART_B (frame not aborted)",
                    "BUG-007: RESET_HW_PIN_NOT_IMPLEMENTED — "
                    "m_frame_done_ev not cancelled on reset; "
                    "tx_deliver_method must check reset state before forwarding");
        }
    }

    /* Drain any stale byte that leaked through */
    if (stale_rir >= 0) usart_drain(USART_B);
    UA_STATUS = STATUS_ALL;
    UB_STATUS = STATUS_ALL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main entry point
 *
 * NOTE: RST-001 runs BEFORE env_init() to capture raw power-on state.
 *       RST-002..005 run AFTER env_init() to have interrupt infrastructure.
 * ═══════════════════════════════════════════════════════════════════════════ */
void isr_main(void)
{
    sc_banner("USART2 HARDWARE RESET TESTS (RST-001..005)");

    /*
     * RST-001: read registers before ANY write.
     * Must be first — env_init() writes CON_INIT which would destroy reset state.
     */
    rst_001_power_on_reset();

    /* Now set up the full interrupt environment for RST-002..005 */
    env_init();

    rst_002_hw_reset_clears_con();
    rst_003_hw_reset_clears_status();
    rst_004_hw_reset_clears_rbuf_state();
    rst_005_hw_reset_mid_transfer();

    sc_summary("reset");
}
