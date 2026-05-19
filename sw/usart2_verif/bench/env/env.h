/*
 * env.h — Test environment: ISR infrastructure, PLIC setup, interrupt enable
 *
 * Provides:
 *   - Global ISR event log (g_log, g_log_count)
 *   - trap_handler() — machine-mode C ISR, called from bootstrap trap_entry
 *   - env_init()     — configure trap vector, PLIC, enable M-mode interrupts
 *   - env_reset()    — flush STATUS registers and log for next test
 *   - wait_events()  — WFI loop until N events logged (with timeout)
 */

#ifndef ENV_H
#define ENV_H

#include "../common/platform.h"

/* ── Global event log (written by trap_handler, read by tests) ────────────── */
volatile IrqEvent g_log[LOG_SIZE];
volatile uint32_t g_log_count = 0u;
volatile uint32_t g_irq_seq   = 0u;   /* monotonic sequence across all ISRs */

/* ── Forward declaration (symbol provided by bootstrap.S) ────────────────── */
extern void trap_entry(void);

/* ══════════════════════════════════════════════════════════════════════════
 * trap_handler — machine-mode C handler, called from bootstrap.S trap_entry
 *
 * Flow:
 *   1. Verify machine external interrupt (mcause = 0x8000000B)
 *   2. PLIC claim → get IRQ ID
 *   3. Read + W1C STATUS of the claiming USART
 *   4. Log one entry per set STATUS bit (with sequence number)
 *   5. PLIC complete
 * ══════════════════════════════════════════════════════════════════════════ */
void trap_handler(void)
{
    uint32_t mcause, irq_id, status, i;
    static const uint32_t BITS[4] = {
        STATUS_TBIR, STATUS_TIR, STATUS_RIR, STATUS_EIR
    };

    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));
    if (mcause != 0x8000000Bu) return;

    irq_id = PLIC_CLAIM_HART0;
    if (irq_id == 0u) return;

    status = 0u;
    if (irq_id == IRQ_USART_A) {
        status    = UA_STATUS;
        UA_STATUS = status;
    } else if (irq_id == IRQ_USART_B) {
        status    = UB_STATUS;
        UB_STATUS = status;
    }

    for (i = 0u; i < 4u; ++i) {
        if (status & BITS[i]) {
            uint32_t idx = g_log_count;
            if (idx < LOG_SIZE) {
                g_log[idx].irq_id     = irq_id;
                g_log[idx].status_bit = BITS[i];
                g_log[idx].seq        = g_irq_seq++;
                __asm__ volatile("fence" ::: "memory");
                g_log_count = idx + 1u;
            }
        }
    }

    PLIC_CLAIM_HART0 = irq_id;
}

/* ── setup_plic — enable USART_A (IRQ1) and USART_B (IRQ2) ──────────────── */
static void setup_plic(void)
{
    PLIC_PRIO(IRQ_USART_A) = 1u;
    PLIC_PRIO(IRQ_USART_B) = 1u;
    PLIC_ENABLE_HART0      = (1u << IRQ_USART_A) | (1u << IRQ_USART_B);
    PLIC_THRESHOLD_HART0   = 0u;
}

/* ── enable_irq — global M-mode interrupt enable ─────────────────────────── */
static void enable_irq(void)
{
    __asm__ volatile("csrs mie,     %0" :: "r"(1u << 11));   /* MEIE */
    __asm__ volatile("csrs mstatus, %0" :: "r"(1u << 3));    /* MIE  */
}

/* ── env_init — call once at the top of every test binary's main() ────────── */
static void env_init(void)
{
    uintptr_t addr = (uintptr_t)(void *)trap_entry;
    __asm__ volatile("csrw mtvec, %0" :: "r"(addr));

    UA_CON    = CON_INIT;
    UB_CON    = CON_INIT;
    UA_STATUS = 0xFFFFFFFFu;
    UB_STATUS = 0xFFFFFFFFu;

    setup_plic();
    enable_irq();
}

/* ── env_reset — clear STATUS and log index before each individual test ──── */
static uint32_t env_reset(void)
{
    UA_STATUS = 0xFFFFFFFFu;
    UB_STATUS = 0xFFFFFFFFu;
    __asm__ volatile("fence" ::: "memory");
    return g_log_count;   /* caller saves this as the test's event base */
}

/* ── find_event — search log[from..count) for matching (irq_id, status_bit) */
static int find_event(uint32_t from, uint32_t irq_id, uint32_t status_bit)
{
    uint32_t i, count = g_log_count;
    for (i = from; i < count; ++i)
        if (g_log[i].irq_id == irq_id && g_log[i].status_bit == status_bit)
            return (int)i;
    return -1;
}

/* ── wait_events — WFI loop until log_count >= base+target (80 tries) ────── */
static int wait_events(uint32_t base, uint32_t target)
{
    uint32_t i;
    for (i = 0u; i < 80u; ++i) {
        if (g_log_count >= base + target) return 1;
        __asm__ volatile("wfi" ::: "memory");
    }
    return 0;
}

/*
 * wait_rir — poll until STATUS_RIR appears in the event log for the given
 * receiver IRQ ID.  Use instead of wait_events(base, 2) when the test needs
 * to read RBUF: guarantees m_rbuf is valid because RIR is only logged AFTER
 * rx_thread has written the byte to m_rbuf.
 */
static int wait_rir(uint32_t from, uint32_t rx_irq_id)
{
    uint32_t i;
    for (i = 0u; i < 80u; ++i) {
        if (find_event(from, rx_irq_id, STATUS_RIR) >= 0) return 1;
        __asm__ volatile("wfi" ::: "memory");
    }
    return 0;
}

#endif /* ENV_H */
