/* main.c — RISC-V RV32 USART2 interrupt test (5 tests)
 *
 * Platform: usart2test-vp (riscv-vp-plusplus)
 *
 * Two USART2 instances (A and B) are bridged back-to-back via sc_fifo in the VP.
 * Each has a PLIC interrupt line:
 *   USART_A → PLIC IRQ 1
 *   USART_B → PLIC IRQ 2
 *
 * Interrupt chain:
 *   USART event (TBIR/TIR/RIR/EIR) → usart2.b_transport / usart2.rx_thread
 *   → plic->gateway_trigger_interrupt(irq_id) → mip.MEIP set
 *   → ISS wakes from WFI → machine external interrupt taken
 *   → mtvec → trap_entry (bootstrap.S) → trap_handler() here
 *   → claim PLIC, read+W1C STATUS, log events, complete PLIC → mret
 *
 * USART2 register map (same as the QBOX version):
 *   +0x00  CON    control (bit6 = OEN, overrun enable)
 *   +0x04  TBUF   TX buffer write
 *   +0x08  RBUF   RX buffer read
 *   +0x20  STATUS sticky W1C: [0]=TBIR [1]=TIR [2]=RIR [3]=EIR
 *
 * Tests:
 *   1. A→B 0x55 : TBIR_A fires, RIR_B fires, RBUF_B == 0x55
 *   2. B→A 0xAA : TBIR_B fires, RIR_A fires, RBUF_A == 0xAA
 *   3. Overrun  : RIR_B (first byte), EIR_B (second byte, RBUF full)
 *   4. Multi    : 0xDE,0xAD,0xBE,0xEF A→B, each byte TBIR_A + RIR_B
 *   5. TIR      : TX-complete fires 2 µs after TBIR (separate interrupt)
 */

/* Test selection mask — bit N-1 enables Test N (default 0x1F = all five).
 * Placed at a fixed address (0x80FFFF00) so the VP can patch it at runtime
 * via --test-mask without recompiling.  Compile-time override: -DTEST_MASK=N */
#ifndef TEST_MASK
#define TEST_MASK 0x1Fu
#endif
volatile unsigned int g_test_mask
    __attribute__((section(".test_cfg"))) = TEST_MASK;

/* Bare-metal type definitions (no glibc/stdint.h — Linux cross-toolchain) */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned int       uintptr_t;

/* ── MMIO helper ──────────────────────────────────────────────────────────── */
#define MMIO32(a)  (*(volatile uint32_t *)(uintptr_t)(a))

/* ── USART register addresses ────────────────────────────────────────────── */
#define UA_CON    MMIO32(0x09002000UL)
#define UA_TBUF   MMIO32(0x09002004UL)
#define UA_RBUF   MMIO32(0x09002008UL)
#define UA_STATUS MMIO32(0x09002020UL)

#define UB_CON    MMIO32(0x09003000UL)
#define UB_TBUF   MMIO32(0x09003004UL)
#define UB_RBUF   MMIO32(0x09003008UL)
#define UB_STATUS MMIO32(0x09003020UL)

/* Console USART (stdout via ConsoleUart in VP) */
#define UC_TBUF   MMIO32(0x09004004UL)

/* Exiter: write 0 → sc_stop() in VP */
#define EXITER    MMIO32(0x09010000UL)

/* CON_INIT: Mode1(bit15) | OEN(bit6) | REN(bit3) | Run(bit0) = 0x8049 */
#define CON_INIT  0x8049u

/* STATUS bit masks */
#define STATUS_TBIR  (1u << 0)
#define STATUS_TIR   (1u << 1)
#define STATUS_RIR   (1u << 2)
#define STATUS_EIR   (1u << 3)

/* ── PLIC register addresses ─────────────────────────────────────────────── */
#define PLIC_BASE            0x40000000UL
/* Priority[n] = PLIC_BASE + n*4 */
#define PLIC_PRIO(n)         MMIO32(PLIC_BASE + (n)*4UL)
/* Hart 0 enable word 0 (bits 0-31 → interrupts 0-31) */
#define PLIC_ENABLE_HART0    MMIO32(PLIC_BASE + 0x2000UL)
/* Hart 0 threshold and claim/complete */
#define PLIC_THRESHOLD_HART0 MMIO32(PLIC_BASE + 0x200000UL)
#define PLIC_CLAIM_HART0     MMIO32(PLIC_BASE + 0x200004UL)

/* PLIC IRQ IDs */
#define IRQ_USART_A   1u
#define IRQ_USART_B   2u

/* ── ISR event log ────────────────────────────────────────────────────────── */
#define LOG_SIZE 64u

typedef struct {
    uint32_t irq_id;      /* 1 = USART_A, 2 = USART_B */
    uint32_t status_bit;  /* exactly one of STATUS_TBIR/TIR/RIR/EIR */
} IrqEvent;

static volatile IrqEvent g_log[LOG_SIZE];
static volatile uint32_t g_log_count;

/* ── Pass / fail counters ─────────────────────────────────────────────────── */
static int g_pass;
static int g_fail;

/* ── Forward declarations ─────────────────────────────────────────────────── */
void trap_handler(void);   /* called from bootstrap.S trap_entry */
extern void trap_entry(void);

static void setup_trap_handler(void);
static void setup_plic(void);
static void enable_irq(void);
static int  wait_n(uint32_t target);
static int  find_event(uint32_t from, uint32_t irq_id, uint32_t status_bit);
static uint32_t test_begin(void);
static void pass_test(const char *name);
static void fail_test(const char *name, const char *why);
static void put_char(char c);
static void put_str(const char *s);
static void put_hex(uint32_t v);
void isr_main(void);
static void test1(void);
static void test2(void);
static void test3(void);
static void test4(void);
static void test5(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * trap_handler — machine-mode C-level handler (called from trap_entry)
 *
 * Handles machine external interrupts (mcause = 0x8000000B):
 *   1. Claim interrupt from PLIC (gets IRQ ID)
 *   2. Read + W1C STATUS register of the triggering USART
 *   3. Log one entry per set STATUS bit
 *   4. Complete interrupt in PLIC
 * ═══════════════════════════════════════════════════════════════════════════ */
void trap_handler(void)
{
    uint32_t mcause, irq_id, status, i;
    static const uint32_t BITS[4] = {
        STATUS_TBIR, STATUS_TIR, STATUS_RIR, STATUS_EIR
    };

    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));

    /* Only handle machine external interrupts */
    if (mcause != 0x8000000Bu) return;

    irq_id = PLIC_CLAIM_HART0;   /* claim: returns highest-priority pending IRQ */
    if (irq_id == 0u) return;    /* spurious */

    status = 0u;
    if (irq_id == IRQ_USART_A) {
        status    = UA_STATUS;
        UA_STATUS = status;      /* W1C: clear everything we just read */
    } else if (irq_id == IRQ_USART_B) {
        status    = UB_STATUS;
        UB_STATUS = status;
    }

    /* Log one entry per set STATUS bit */
    for (i = 0u; i < 4u; ++i) {
        if (status & BITS[i]) {
            uint32_t idx = g_log_count;
            if (idx < LOG_SIZE) {
                g_log[idx].irq_id     = irq_id;
                g_log[idx].status_bit = BITS[i];
                __asm__ volatile("fence" ::: "memory");
                g_log_count = idx + 1u;
            }
        }
    }

    PLIC_CLAIM_HART0 = irq_id;   /* complete: clear pending bit in PLIC */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * setup_trap_handler — point mtvec at trap_entry (direct mode)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void setup_trap_handler(void)
{
    uintptr_t addr = (uintptr_t)(void *)trap_entry;
    /* bit[1:0] = 0b00 = direct mode: all traps go to addr */
    __asm__ volatile("csrw mtvec, %0" :: "r"(addr));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * setup_plic — configure PLIC for USART_A and USART_B interrupts
 *
 * FE310-style PLIC (base 0x40000000):
 *   Priority[n] at base + n*4  (n=1 → USART_A, n=2 → USART_B)
 *   Enable word for hart 0 at base + 0x2000  (bit1=IRQ1, bit2=IRQ2)
 *   Threshold for hart 0 at base + 0x200000  (0 = allow all)
 *   Claim/Complete for hart 0 at base + 0x200004
 * ═══════════════════════════════════════════════════════════════════════════ */
static void setup_plic(void)
{
    PLIC_PRIO(IRQ_USART_A) = 1u;
    PLIC_PRIO(IRQ_USART_B) = 1u;
    PLIC_ENABLE_HART0      = (1u << IRQ_USART_A) | (1u << IRQ_USART_B);
    PLIC_THRESHOLD_HART0   = 0u;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * enable_irq — enable machine external interrupt globally
 * ═══════════════════════════════════════════════════════════════════════════ */
static void enable_irq(void)
{
    /* mie[11] = MEIE: enable machine external interrupts */
    __asm__ volatile("csrs mie, %0" :: "r"(1u << 11));
    /* mstatus[3] = MIE: global machine interrupt enable */
    __asm__ volatile("csrs mstatus, %0" :: "r"(1u << 3));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * wait_n — spin+WFI until at least `target` events have been logged.
 * Returns 1 on success, 0 on timeout (40 WFI iterations).
 * WFI suspends the ISS until the PLIC fires an interrupt, at which point
 * trap_entry runs, trap_handler logs the event, and WFI returns.
 * ═══════════════════════════════════════════════════════════════════════════ */
static int wait_n(uint32_t target)
{
    uint32_t i;
    for (i = 0u; i < 40u; ++i) {
        if (g_log_count >= target) return 1;
        __asm__ volatile("wfi" ::: "memory");
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * find_event — search g_log[from..count) for a matching entry.
 * Returns log index on success, -1 if not found.
 * ═══════════════════════════════════════════════════════════════════════════ */
static int find_event(uint32_t from, uint32_t irq_id, uint32_t status_bit)
{
    uint32_t i, count = g_log_count;
    for (i = from; i < count; ++i) {
        if (g_log[i].irq_id == irq_id && g_log[i].status_bit == status_bit)
            return (int)i;
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * test_begin — clear STATUS registers, return current log count as base.
 * ═══════════════════════════════════════════════════════════════════════════ */
static uint32_t test_begin(void)
{
    UA_STATUS = 0xFFFFFFFFu;
    UB_STATUS = 0xFFFFFFFFu;
    __asm__ volatile("fence" ::: "memory");
    return g_log_count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 1: A → B  (0x55)
 * Expected: TBIR_A fires, RIR_B fires, RBUF_B == 0x55
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test1(void)
{
    uint32_t base, data;

    put_str("Test 1: A sends 0x55, expect TBIR_A + RIR_B\r\n");
    base = test_begin();
    UA_TBUF = 0x55u;

    if (!wait_n(base + 2u)) { fail_test("T1", "IRQ timeout"); return; }

    if (find_event(base, IRQ_USART_A, STATUS_TBIR) < 0) {
        fail_test("T1-TBIR_A", "not in log"); return;
    }
    put_str("  [ISR] TBIR_A confirmed\r\n");

    if (find_event(base, IRQ_USART_B, STATUS_RIR) < 0) {
        fail_test("T1-RIR_B", "not in log"); return;
    }
    put_str("  [ISR] RIR_B  confirmed\r\n");

    data = UB_RBUF & 0xFFu;
    put_str("  RBUF_B="); put_hex(data); put_str("\r\n");
    if (data == 0x55u) pass_test("Test1 A->B 0x55");
    else               fail_test("Test1", "RBUF_B != 0x55");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 2: B → A  (0xAA)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test2(void)
{
    uint32_t base, data;

    put_str("Test 2: B sends 0xAA, expect TBIR_B + RIR_A\r\n");
    base = test_begin();
    UB_TBUF = 0xAAu;

    if (!wait_n(base + 2u)) { fail_test("T2", "IRQ timeout"); return; }

    if (find_event(base, IRQ_USART_B, STATUS_TBIR) < 0) {
        fail_test("T2-TBIR_B", "not in log"); return;
    }
    put_str("  [ISR] TBIR_B confirmed\r\n");

    if (find_event(base, IRQ_USART_A, STATUS_RIR) < 0) {
        fail_test("T2-RIR_A", "not in log"); return;
    }
    put_str("  [ISR] RIR_A  confirmed\r\n");

    data = UA_RBUF & 0xFFu;
    put_str("  RBUF_A="); put_hex(data); put_str("\r\n");
    if (data == 0xAAu) pass_test("Test2 B->A 0xAA");
    else               fail_test("Test2", "RBUF_A != 0xAA");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 3: Overrun / EIR
 *   Step 1: send 0x11 → RBUF_B fills → RIR_B fires
 *   Step 2: send 0x22 while RBUF_B still full → EIR_B fires
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test3(void)
{
    uint32_t base;

    put_str("Test 3: Overrun — EIR_B after second byte\r\n");
    base = test_begin();
    UA_TBUF = 0x11u;

    /* Wait for TBIR_A + RIR_B (2 events) */
    if (!wait_n(base + 2u)) { fail_test("T3-RIR_B", "first byte timeout"); return; }
    if (find_event(base, IRQ_USART_B, STATUS_RIR) < 0) {
        fail_test("T3-RIR_B", "not in log"); return;
    }
    put_str("  [ISR] RIR_B (RBUF_B full)\r\n");

    /* Do NOT drain RBUF_B — overrun only fires while RBUF is still full */
    base = g_log_count;
    UA_TBUF = 0x22u;

    /* Wait for TBIR_A + EIR_B (2 events) */
    if (!wait_n(base + 2u)) { fail_test("T3-EIR_B", "EIR timeout"); return; }
    if (find_event(base, IRQ_USART_B, STATUS_EIR) < 0) {
        fail_test("T3-EIR_B", "not in log"); return;
    }
    put_str("  [ISR] EIR_B  confirmed (overrun)\r\n");
    pass_test("Test3 Overrun EIR");

    /* Drain the 0x11 byte left in RBUF_B so test4 starts clean */
    (void)UB_RBUF;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 4: Multi-byte stream A → B  [0xDE, 0xAD, 0xBE, 0xEF]
 * Each byte: wait for TBIR_A + RIR_B, drain RBUF_B, verify.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test4(void)
{
    static const uint32_t stream[4] = { 0xDEu, 0xADu, 0xBEu, 0xEFu };
    uint32_t base, i;
    int ok = 1;

    put_str("Test 4: Multi-byte A->B [0xDE, 0xAD, 0xBE, 0xEF]\r\n");
    (void)test_begin();

    for (i = 0u; i < 4u; ++i) {
        uint32_t exp = stream[i], data;

        base = g_log_count;
        UA_TBUF = exp;

        if (!wait_n(base + 2u)) {
            put_str("  timeout byte "); put_hex(exp); put_str("\r\n");
            ok = 0; break;
        }
        if (find_event(base, IRQ_USART_A, STATUS_TBIR) < 0) {
            put_str("  TBIR_A missing for byte "); put_hex(exp); put_str("\r\n");
            ok = 0; break;
        }
        if (find_event(base, IRQ_USART_B, STATUS_RIR) < 0) {
            put_str("  RIR_B missing for byte "); put_hex(exp); put_str("\r\n");
            ok = 0; break;
        }

        data = UB_RBUF & 0xFFu;
        put_str("  byte "); put_hex(i);
        put_str(" sent="); put_hex(exp);
        put_str(" got=");  put_hex(data);
        put_str((data == exp) ? " OK\r\n" : " MISMATCH\r\n");
        if (data != exp) { ok = 0; break; }
    }
    if (ok) pass_test("Test4 multi-byte");
    else    fail_test("Test4 multi-byte", "mismatch or timeout");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 5: TIR — TX-frame complete interrupt
 *
 * The USART2 VP model schedules TIR 2 µs after each TBUF write (TIR_DELAY).
 * This guarantees TIR fires as a separate interrupt from TBIR:
 *   - TBIR fires immediately inside b_transport
 *   - TIR fires 2 µs later via the sc_event TIR_DELAY
 *
 * After confirming TBIR, the firmware polls (reads UA_CON + WFI loop) until
 * TIR appears in the log.  The poll loop gives the SC scheduler enough quanta
 * to advance SC time and fire the TIR event.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void test5(void)
{
    uint32_t base, i;
    int      tir_idx;

    put_str("Test 5: TIR — TX-frame complete (ISR)\r\n");
    base = test_begin();
    UA_TBUF = 0x5Au;

    /* Wait for TBIR_A first */
    if (!wait_n(base + 1u)) { fail_test("T5-TBIR_A", "timeout"); return; }
    if (find_event(base, IRQ_USART_A, STATUS_TBIR) < 0) {
        fail_test("T5-TBIR_A", "not in log"); return;
    }
    put_str("  [ISR] TBIR_A confirmed\r\n");

    /* Check if TIR already arrived in the same (or very next) ISR */
    tir_idx = find_event(base, IRQ_USART_A, STATUS_TIR);

    if (tir_idx < 0) {
        /* TIR scheduled 2 µs after TBUF write. Poll until it arrives.
         * Reading UA_CON is harmless (just returns stored CON value).
         * WFI yields the CPU so the SC scheduler can advance time.   */
        for (i = 0u; i < 500u; ++i) {
            volatile uint32_t dummy = UA_CON; (void)dummy;
            tir_idx = find_event(base, IRQ_USART_A, STATUS_TIR);
            if (tir_idx >= 0) break;
            __asm__ volatile("wfi" ::: "memory");
        }
    }

    if (tir_idx >= 0) {
        put_str("  [ISR] TIR_A  confirmed\r\n");
        pass_test("Test5 TIR TX-complete");
    } else {
        put_str("  TIR not seen after 500 polls\r\n");
        put_str("  STATUS_A="); put_hex(UA_STATUS); put_str("\r\n");
        fail_test("Test5 TIR", "TIR never arrived");
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * isr_main — entry point called from bootstrap _start
 * ═══════════════════════════════════════════════════════════════════════════ */
void isr_main(void)
{
    put_str("\r\n");
    put_str("================================================\r\n");
    put_str("  USART2 ISR Interrupt Test (RISC-V PLIC)\r\n");
    put_str("  A <-> B bridged via sc_fifo in VP\r\n");
    put_str("  Interrupts via machine-mode trap handler\r\n");
    put_str("================================================\r\n\r\n");

    setup_trap_handler();

    UA_CON    = CON_INIT;
    UB_CON    = CON_INIT;
    UA_STATUS = 0xFFFFFFFFu;
    UB_STATUS = 0xFFFFFFFFu;

    setup_plic();
    enable_irq();

    put_str("  TEST_MASK="); put_hex(g_test_mask); put_str("\r\n\r\n");

    if (g_test_mask & 0x01u) { put_str("\r\n"); test1(); }
    if (g_test_mask & 0x02u) { put_str("\r\n"); test2(); }
    if (g_test_mask & 0x04u) { put_str("\r\n"); test3(); }
    if (g_test_mask & 0x08u) { put_str("\r\n"); test4(); }
    if (g_test_mask & 0x10u) { put_str("\r\n"); test5(); }

    put_str("\r\n================================================\r\n");
    put_str("  Passed: "); put_hex((uint32_t)g_pass);
    put_str("   Failed: "); put_hex((uint32_t)g_fail);
    put_str("\r\n");
    put_str(g_fail == 0 ? "  ALL TESTS PASSED\r\n" : "  SOME TESTS FAILED\r\n");
    put_str("================================================\r\n");

    EXITER = 0u;   /* triggers sc_stop() in the VP */
}

/* ── Console output helpers ───────────────────────────────────────────────── */

static void put_char(char c)  { UC_TBUF = (uint32_t)(uint8_t)c; }

static void put_str(const char *s) { while (*s) put_char(*s++); }

static void put_hex(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    put_char('0'); put_char('x');
    put_char(h[(v >> 28) & 0xFu]);
    put_char(h[(v >> 24) & 0xFu]);
    put_char(h[(v >> 20) & 0xFu]);
    put_char(h[(v >> 16) & 0xFu]);
    put_char(h[(v >> 12) & 0xFu]);
    put_char(h[(v >>  8) & 0xFu]);
    put_char(h[(v >>  4) & 0xFu]);
    put_char(h[(v >>  0) & 0xFu]);
}

static void pass_test(const char *name)
{
    put_str("[PASS] "); put_str(name); put_str("\r\n");
    ++g_pass;
}

static void fail_test(const char *name, const char *why)
{
    put_str("[FAIL] "); put_str(name); put_str(": "); put_str(why); put_str("\r\n");
    ++g_fail;
}
