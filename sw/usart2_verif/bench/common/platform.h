/*
 * platform.h — Shared platform definitions for USART2 verification
 *
 * Target: usart2test-vp (riscv-vp-plusplus)
 * ISA   : RV32IMAC
 *
 * Memory map:
 *   0x80000000  RAM (16 MB)
 *   0x02000000  CLINT
 *   0x40000000  PLIC  (FE310-style)
 *   0x09002000  USART_A  → PLIC IRQ 1
 *   0x09003000  USART_B  → PLIC IRQ 2
 *   0x09004000  Console UART
 *   0x09010000  Exiter  (any write → sc_stop)
 *
 * USART2 VP register map (offsets from base):
 *   0x00  CON     Control register  (R/W) — stored; only bit[6]=OEN is functional
 *   0x04  TBUF    TX buffer          (W)   — triggers TBIR immediately; TIR 2µs later
 *   0x08  RBUF    RX buffer          (R)   — drains receive FIFO; clears rbuf_full
 *   0x20  STATUS  Sticky W1C         (R/W) — [0]=TBIR [1]=TIR [2]=RIR [3]=EIR
 *
 * NOTE — Spec registers NOT implemented in VP model:
 *   0x0C  BG   — baud generator     → accesses silently ignored (no-op)
 *   0x10  FDR  — fractional divider → accesses silently ignored (no-op)
 */

#ifndef PLATFORM_H
#define PLATFORM_H

/* ── Primitive types (no glibc — bare-metal cross-toolchain) ─────────────── */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;
typedef unsigned int        uintptr_t;
typedef int                 int32_t;

/* ── MMIO accessor ───────────────────────────────────────────────────────── */
#define MMIO32(addr)  (*(volatile uint32_t *)(uintptr_t)(addr))

/* ── RAM layout ──────────────────────────────────────────────────────────── */
#define RAM_BASE        0x80000000UL
#define RAM_SIZE        0x01000000UL   /* 16 MB */

/* ── CLINT ───────────────────────────────────────────────────────────────── */
#define CLINT_BASE      0x02000000UL
#define CLINT_MTIME     (*(volatile uint64_t *)(CLINT_BASE + 0xBFF8UL))
#define CLINT_MTIMECMP  (*(volatile uint64_t *)(CLINT_BASE + 0x4000UL))

/* ── PLIC (FE310-style) ───────────────────────────────────────────────────── */
#define PLIC_BASE               0x40000000UL
#define PLIC_PRIO(n)            MMIO32(PLIC_BASE + (uint32_t)(n) * 4UL)
#define PLIC_ENABLE_HART0       MMIO32(PLIC_BASE + 0x2000UL)
#define PLIC_THRESHOLD_HART0    MMIO32(PLIC_BASE + 0x200000UL)
#define PLIC_CLAIM_HART0        MMIO32(PLIC_BASE + 0x200004UL)

/* ── USART IRQ IDs ────────────────────────────────────────────────────────── */
#define IRQ_USART_A   1u
#define IRQ_USART_B   2u

/* ── USART_A register addresses ───────────────────────────────────────────── */
#define UA_BASE     0x09002000UL
#define UA_CON      MMIO32(UA_BASE + 0x00UL)
#define UA_TBUF     MMIO32(UA_BASE + 0x04UL)
#define UA_RBUF     MMIO32(UA_BASE + 0x08UL)
#define UA_BG       MMIO32(UA_BASE + 0x0CUL)   /* NOT implemented in VP */
#define UA_FDR      MMIO32(UA_BASE + 0x10UL)   /* NOT implemented in VP */
#define UA_STATUS   MMIO32(UA_BASE + 0x20UL)

/* ── USART_B register addresses ───────────────────────────────────────────── */
#define UB_BASE     0x09003000UL
#define UB_CON      MMIO32(UB_BASE + 0x00UL)
#define UB_TBUF     MMIO32(UB_BASE + 0x04UL)
#define UB_RBUF     MMIO32(UB_BASE + 0x08UL)
#define UB_BG       MMIO32(UB_BASE + 0x0CUL)   /* NOT implemented in VP */
#define UB_FDR      MMIO32(UB_BASE + 0x10UL)   /* NOT implemented in VP */
#define UB_STATUS   MMIO32(UB_BASE + 0x20UL)

/* ── Console UART (stdout via ConsoleUart in VP) ──────────────────────────── */
#define CONSOLE_TBUF  MMIO32(0x09004004UL)

/* ── Exiter: any write → sc_stop() ───────────────────────────────────────── */
#define EXITER        MMIO32(0x09010000UL)

/* ── CON register bit definitions ────────────────────────────────────────── */
#define CON_M0    (1u << 0)   /* Mode bit 0   (stored, not used by model) */
#define CON_M1    (1u << 1)   /* Mode bit 1   (stored, not used by model) */
#define CON_M2    (1u << 2)   /* Mode bit 2   (stored, not used by model) */
#define CON_REN   (1u << 3)   /* Receiver enable (stored, not used)       */
#define CON_ODD   (1u << 4)   /* Parity sense    (stored, not used)       */
#define CON_STP   (1u << 5)   /* Stop bits       (stored, not used)       */
#define CON_OEN   (1u << 6)   /* Overrun IRQ enable — MODEL ENFORCES THIS */
#define CON_FEN   (1u << 7)   /* Framing error enable (stored, not used)  */
#define CON_PEN   (1u << 8)   /* Parity error enable  (stored, not used)  */
#define CON_BRS   (1u << 12)  /* Baud prescaler       (stored, not used)  */
#define CON_FDE   (1u << 13)  /* FDR enable           (stored, not used)  */
#define CON_LB    (1u << 14)  /* Loopback             (stored, not used)  */
#define CON_R     (1u << 15)  /* Module run           (stored, not used)  */

/* CON_INIT: OEN | REN | M0 | R  — matches firmware default */
#define CON_INIT  (CON_R | CON_OEN | CON_REN | CON_M0)   /* 0x8049 */

/* ── STATUS register bit definitions ─────────────────────────────────────── */
#define STATUS_TBIR  (1u << 0)
#define STATUS_TIR   (1u << 1)
#define STATUS_RIR   (1u << 2)
#define STATUS_EIR   (1u << 3)
#define STATUS_ALL   (STATUS_TBIR | STATUS_TIR | STATUS_RIR | STATUS_EIR)

/*
 * ISR event log capacity.
 * Each TBUF write generates up to 3 events (TBIR + TIR + RIR).
 * Size must accommodate the longest test run without wrapping:
 *   DT-001: 256 bytes × 3 = 768   MEM: 350 bytes × 3 = 1050
 * 4096 gives comfortable headroom.
 */
#define LOG_SIZE  4096u

/* ── ISR event structure ──────────────────────────────────────────────────── */
typedef struct {
    uint32_t irq_id;      /* IRQ_USART_A or IRQ_USART_B */
    uint32_t status_bit;  /* one of STATUS_TBIR/TIR/RIR/EIR */
    uint32_t seq;         /* monotonic sequence number */
} IrqEvent;

#endif /* PLATFORM_H */
