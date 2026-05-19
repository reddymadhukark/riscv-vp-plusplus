/*
 * test_negative.c — Negative / error injection verification
 *
 * Objective:
 *   Verify that the VP model handles out-of-spec, reserved, and boundary
 *   conditions gracefully (no hang, no crash, no undefined state).
 *
 * Test IDs: NEG-001 … NEG-007
 *
 * NEG-001  Write to non-existent BG register (offset 0x0C) → no crash
 * NEG-002  Write to non-existent FDR register (offset 0x10) → no crash
 * NEG-003  Write STATUS = 0 → bits must NOT be cleared (W1C semantics)
 * NEG-004  Write STATUS = 0xFFFFFFFF → all bits cleared (correct W1C)
 * NEG-005  EIR without OEN=1 → EIR must not fire on overrun
 * NEG-006  Reserved CON bits — write all 1s, verify no side effects on transfers
 * NEG-007  Write to unknown offset (e.g. 0x30) → no crash, transfer still works
 */

#include "../../bench/common/platform.h"
#include "../../bench/env/env.h"
#include "../../bench/scoreboard/scoreboard.h"
#include "../../bench/driver/usart_driver.h"
#include "../../bench/monitor/monitor.h"

/* ── NEG-001: Write BG register (no-op, must not crash) ─────────────────── */
static void neg_001_bg_noop(void)
{
    uint32_t base;
    uint8_t  data;

    sc_put_str("NEG-001: Write BG (0x0C) — must not crash VP or break transfers\r\n");

    usart_bg_write(USART_A, 0x1FFFu);   /* write maximum 13-bit BG value */
    usart_bg_write(USART_B, 0x1FFFu);

    /* Verify transfer still works after BG write */
    base = env_reset();
    UA_TBUF = 0x91u;
    if (!wait_events(base, 2u)) {
        sc_fail("NEG-001", "transfer works after BG write", "timeout",
                "VP crashed or locked up after write to unimplemented BG register");
        return;
    }
    data = usart_rbuf_read(USART_B);
    if (data == 0x91u) sc_pass("NEG-001 BG write no-op: VP continues correctly");
    else sc_fail_hex("NEG-001", 0x91u, (uint32_t)data,
                     "Data corrupted after BG write — VP state affected unexpectedly");
}

/* ── NEG-002: Write FDR register (no-op, must not crash) ────────────────── */
static void neg_002_fdr_noop(void)
{
    uint32_t base;
    uint8_t  data;

    sc_put_str("NEG-002: Write FDR (0x10) — must not crash VP or break transfers\r\n");

    usart_fdr_write(USART_A, 0x1FFu);   /* STEP=0xFF, DM=1 */

    base = env_reset();
    UA_TBUF = 0x92u;
    if (!wait_events(base, 2u)) {
        sc_fail("NEG-002", "transfer after FDR write", "timeout",
                "VP locked after write to unimplemented FDR register");
        return;
    }
    data = usart_rbuf_read(USART_B);
    if (data == 0x92u) sc_pass("NEG-002 FDR write no-op: VP continues correctly");
    else sc_fail_hex("NEG-002", 0x92u, (uint32_t)data,
                     "Data corrupted after FDR write");
}

/* ── NEG-003: Write STATUS = 0 → bits preserved (no W0C) ────────────────── */
static void neg_003_status_write_zero(void)
{
    uint32_t status;
    uint32_t pre_base;   /* saved BEFORE TBUF write */

    sc_put_str("NEG-003: Write STATUS=0 must not clear any bits (W1C)\r\n");

    pre_base = g_log_count;   /* capture base before any event fires */

    /* Disable MIE so ISR cannot clear TBIR during the W0C check */
    __asm__ volatile("csrc mstatus, %0" :: "r"(1u << 3));

    UA_STATUS = STATUS_ALL;   /* clean slate */
    UA_TBUF   = 0x93u;        /* TBIR fires synchronously — ISR blocked (MIE=0) */

    UA_STATUS = 0u;           /* write 0 — must NOT clear TBIR */
    status = usart_status_read(USART_A);

    __asm__ volatile("csrs mstatus, %0" :: "r"(1u << 3));  /* re-enable MIE */

    if (status & STATUS_TBIR)
        sc_pass("NEG-003 Write-0 does not clear STATUS bits (correct W1C)");
    else
        sc_fail("NEG-003", "TBIR preserved after write-0", "TBIR cleared",
                "STATUS implements W0C instead of W1C — spec requires write-1-to-clear");

    /* Drain using pre_base — valid even if events fired during print */
    wait_rir(pre_base, IRQ_USART_B);
    UA_STATUS = STATUS_ALL;
    UB_STATUS = STATUS_ALL;
    usart_drain(USART_B);
}

/* ── NEG-004: Write STATUS = 0xFFFFFFFF → all STATUS bits cleared ─────────── */
static void neg_004_status_write_all_ones(void)
{
    uint32_t base, status;

    sc_put_str("NEG-004: Write STATUS=0xFFFFFFFF clears all bits\r\n");
    base = env_reset();
    UA_TBUF = 0x94u;
    wait_events(base, 1u);

    UA_STATUS = 0xFFFFFFFFu;
    status = usart_status_read(USART_A) & ~STATUS_TIR;   /* ignore TIR in-flight */

    if (status == 0u)
        sc_pass("NEG-004 STATUS=0 after writing all 1s (W1C correct)");
    else
        sc_fail_hex("NEG-004", 0u, status,
                    "STATUS not fully cleared by write of all 1s");
    usart_drain(USART_B);
}

/* ── NEG-005: Overrun without OEN — EIR must NOT fire ───────────────────── */
static void neg_005_overrun_no_oen(void)
{
    uint32_t base;

    sc_put_str("NEG-005: Overrun with OEN=0 — EIR must be suppressed\r\n");
    usart_oen_clear(USART_B);
    base = env_reset();

    UA_TBUF = 0x95u;   /* fill RBUF_B */
    if (!wait_events(base, 2u)) {
        sc_fail("NEG-005", "RIR_B from first byte", "timeout",
                "Failed to fill RBUF_B for overrun test");
        usart_con_write(USART_B, CON_INIT);
        return;
    }

    /* Trigger overrun */
    base = g_log_count;
    UA_TBUF = 0x96u;
    wait_events(base, 1u);   /* wait for at least TBIR_A */

    if (find_event(base, IRQ_USART_B, STATUS_EIR) >= 0)
        sc_fail("NEG-005", "EIR absent (OEN=0)", "EIR present",
                "VP model fires EIR even when CON.OEN=0 — spec requires OEN gate");
    else
        sc_pass("NEG-005 EIR correctly absent when OEN=0");

    usart_con_write(USART_B, CON_INIT);
    usart_drain(USART_B);
}

/* ── NEG-006: Reserved CON bits don't affect transfer ───────────────────── */
static void neg_006_con_reserved_bits(void)
{
    uint32_t base;
    uint8_t  data;

    sc_put_str("NEG-006: Reserved CON bits — no side effects on transfers\r\n");

    /* Write maximum value (all 32 bits set) including reserved [31:16] */
    usart_con_write(USART_A, 0xFFFFFFFFu);

    base = env_reset();
    UA_TBUF = 0x97u;
    if (!wait_events(base, 2u)) {
        sc_fail("NEG-006", "transfer with all CON bits set", "timeout",
                "VP locked or crashed when CON reserved bits were set");
        usart_con_write(USART_A, CON_INIT);
        return;
    }
    data = usart_rbuf_read(USART_B);

    usart_con_write(USART_A, CON_INIT);

    if (data == 0x97u) sc_pass("NEG-006 Reserved CON bits: transfer unaffected");
    else sc_fail_hex("NEG-006", 0x97u, (uint32_t)data,
                     "Transfer corrupted by reserved CON bit write");
}

/* ── NEG-007: Write to unknown offset 0x30 — no crash ───────────────────── */
static void neg_007_unknown_offset(void)
{
    uint32_t base;
    uint8_t  data;

    sc_put_str("NEG-007: Write to undefined offset 0x30 — must not crash\r\n");

    /* Write to undefined register offset */
    MMIO32(UA_BASE + 0x30UL) = 0xDEADBEEFu;
    MMIO32(UA_BASE + 0x40UL) = 0xDEADBEEFu;

    /* VP model default case in b_transport silently ignores unknown offsets */
    base = env_reset();
    UA_TBUF = 0x98u;
    if (!wait_events(base, 2u)) {
        sc_fail("NEG-007", "transfer after unknown offset write", "timeout",
                "VP locked after write to unknown register offset");
        return;
    }
    data = usart_rbuf_read(USART_B);
    if (data == 0x98u) sc_pass("NEG-007 Unknown offset write: VP unaffected");
    else sc_fail_hex("NEG-007", 0x98u, (uint32_t)data,
                     "Data corrupted after write to unknown register offset");
}

/* ── main entry point ────────────────────────────────────────────────────── */
void isr_main(void)
{
    sc_banner("USART2 NEGATIVE TESTS (NEG-001..007)");
    env_init();

    neg_001_bg_noop();
    neg_002_fdr_noop();
    neg_003_status_write_zero();
    neg_004_status_write_all_ones();
    neg_005_overrun_no_oen();
    neg_006_con_reserved_bits();
    neg_007_unknown_offset();

    sc_summary("negative");
}
