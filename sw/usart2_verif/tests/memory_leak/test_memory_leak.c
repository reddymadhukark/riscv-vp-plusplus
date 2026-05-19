/*
 * test_memory_leak.c — Repeated operation stability test (Valgrind target)
 *
 * Objective:
 *   Exercise the VP model through many repeated cycles of write/read/clear
 *   operations to expose any memory leaks, use-after-free, or state
 *   accumulation inside the SystemC model.
 *
 *   This firmware is designed to run cleanly through the VP binary under
 *   Valgrind:
 *     valgrind --leak-check=full ./usart2test-vp ./test_memory_leak.elf
 *
 * Test IDs: MEM-001 … MEM-003
 *
 * MEM-001  200 TBUF-write / STATUS-clear cycles (TBIR path)
 * MEM-002  100 send+receive cycles with RBUF drain (RIR path)
 * MEM-003  50 overrun + EIR + drain cycles (EIR path)
 */

#include "../../bench/common/platform.h"
#include "../../bench/env/env.h"
#include "../../bench/scoreboard/scoreboard.h"
#include "../../bench/driver/usart_driver.h"
#include "../../bench/monitor/monitor.h"

#define MEM_N_TBIR    200u
#define MEM_N_RIR     100u
#define MEM_N_EIR      50u

/* ── MEM-001: 200 TBUF-write / STATUS W1C cycles ────────────────────────── */
static void mem_001_tbir_cycles(void)
{
    uint32_t i, base, errors = 0u;

    sc_put_str("MEM-001: 200 TBUF-write + STATUS-clear cycles\r\n");
    env_reset();

    for (i = 0u; i < MEM_N_TBIR; ++i) {
        base = g_log_count;
        UA_TBUF = (uint8_t)(i & 0xFFu);

        if (!wait_events(base, 1u)) { ++errors; continue; }

        UA_STATUS = STATUS_TBIR;   /* W1C TBIR */
        /* Wait for RIR_B (byte delivery) before draining */
        wait_rir(base, IRQ_USART_B);
        usart_drain(USART_B);
        UB_STATUS = STATUS_ALL;
    }

    sc_put_str("  errors="); sc_put_hex(errors); sc_put_str("/200\r\n");
    if (errors == 0u) sc_pass("MEM-001 200 TBIR cycles: no errors");
    else sc_fail("MEM-001", "0 errors", "errors found",
                 "TBIR cycle failure — possible state accumulation in VP model");
}

/* ── MEM-002: 100 send+receive cycles ────────────────────────────────────── */
static void mem_002_rir_cycles(void)
{
    uint32_t i, base, errors = 0u;
    uint8_t  data;

    sc_put_str("MEM-002: 100 send+receive+drain cycles\r\n");
    env_reset();

    for (i = 0u; i < MEM_N_RIR; ++i) {
        uint8_t exp = (uint8_t)(0x30u + (i & 0xFFu));
        base = g_log_count;
        UA_TBUF = exp;

        if (!wait_rir(base, IRQ_USART_B)) { ++errors; usart_drain(USART_B); continue; }

        data = usart_rbuf_read(USART_B);
        if (data != exp) ++errors;
        UA_STATUS = STATUS_ALL;
        UB_STATUS = STATUS_ALL;
    }

    sc_put_str("  errors="); sc_put_hex(errors); sc_put_str("/100\r\n");
    if (errors == 0u) sc_pass("MEM-002 100 RIR cycles: no errors");
    else sc_fail("MEM-002", "0 errors", "errors found",
                 "RIR cycle failure — possible RBUF or rbuf_full state leak");
}

/* ── MEM-003: 50 overrun + EIR + drain cycles ────────────────────────────── */
static void mem_003_eir_cycles(void)
{
    uint32_t i, base, errors = 0u;

    sc_put_str("MEM-003: 50 overrun+EIR+drain cycles\r\n");
    usart_oen_set(USART_B);
    env_reset();

    for (i = 0u; i < MEM_N_EIR; ++i) {
        /* Fill RBUF_B */
        base = g_log_count;
        UA_TBUF = 0xB0u;
        if (!wait_rir(base, IRQ_USART_B)) { ++errors; usart_drain(USART_B); continue; }

        /* Overrun */
        base = g_log_count;
        UA_TBUF = 0xB1u;
        wait_events(base, 2u);   /* TBIR_A + EIR_B */

        if (find_event(base, IRQ_USART_B, STATUS_EIR) < 0) ++errors;

        /* Drain and clear */
        usart_drain(USART_B);
        UA_STATUS = STATUS_ALL;
        UB_STATUS = STATUS_ALL;
    }

    sc_put_str("  errors="); sc_put_hex(errors); sc_put_str("/50\r\n");
    if (errors == 0u) sc_pass("MEM-003 50 EIR cycles: no errors");
    else sc_fail("MEM-003", "0 errors", "errors found",
                 "EIR cycle failure — possible overrun state leak in VP model");
}

/* ── main entry point ────────────────────────────────────────────────────── */
void isr_main(void)
{
    sc_banner("USART2 MEMORY LEAK / STABILITY TESTS (MEM-001..003)");
    env_init();

    mem_001_tbir_cycles();
    mem_002_rir_cycles();
    mem_003_eir_cycles();

    sc_summary("memory_leak");
}
