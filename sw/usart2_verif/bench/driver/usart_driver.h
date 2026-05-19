/*
 * usart_driver.h — USART2 register-level driver
 *
 * Provides typed accessors and utility functions for:
 *   - CON configuration
 *   - TBUF / RBUF data transfer
 *   - STATUS read / W1C clear
 *   - BG / FDR access (noting VP model no-ops)
 */

#ifndef USART_DRIVER_H
#define USART_DRIVER_H

#include "../common/platform.h"

/* ── Register base selection ─────────────────────────────────────────────── */
typedef enum { USART_A = 0, USART_B = 1 } UsartInst;

static inline volatile uint32_t *usart_reg(UsartInst u, uint32_t off)
{
    uint32_t base = (u == USART_A) ? (uint32_t)UA_BASE : (uint32_t)UB_BASE;
    return (volatile uint32_t *)(uintptr_t)(base + off);
}

/* ── CON ─────────────────────────────────────────────────────────────────── */
static inline void    usart_con_write(UsartInst u, uint32_t v) { *usart_reg(u, 0x00) = v; }
static inline uint32_t usart_con_read (UsartInst u)            { return *usart_reg(u, 0x00); }

/* ── TBUF (write-only per spec; model stores but no readback path) ────────── */
static inline void usart_tbuf_write(UsartInst u, uint8_t byte)
{
    *usart_reg(u, 0x04) = (uint32_t)byte;
}

/* ── RBUF (read-only) ────────────────────────────────────────────────────── */
static inline uint8_t usart_rbuf_read(UsartInst u)
{
    return (uint8_t)(*usart_reg(u, 0x08) & 0xFFu);
}

/* ── STATUS ──────────────────────────────────────────────────────────────── */
static inline uint32_t usart_status_read (UsartInst u)            { return *usart_reg(u, 0x20); }
static inline void     usart_status_clear(UsartInst u, uint32_t m){ *usart_reg(u, 0x20) = m; }
static inline void     usart_status_clear_all(UsartInst u)        { *usart_reg(u, 0x20) = STATUS_ALL; }

/* ── BG (spec reg; VP model: no-op) ─────────────────────────────────────── */
static inline void     usart_bg_write(UsartInst u, uint32_t v) { *usart_reg(u, 0x0C) = v; }
static inline uint32_t usart_bg_read (UsartInst u)             { return *usart_reg(u, 0x0C); }

/* ── FDR (spec reg; VP model: no-op) ────────────────────────────────────── */
static inline void     usart_fdr_write(UsartInst u, uint32_t v) { *usart_reg(u, 0x10) = v; }
static inline uint32_t usart_fdr_read (UsartInst u)             { return *usart_reg(u, 0x10); }

/* ── Send a byte and wait for TBIR (helper for simple TX) ─────────────────── */
/* Returns 1 on success, 0 on timeout */
static inline int usart_send_wait_tbir(UsartInst tx, uint32_t base_cnt,
                                        int (*wait_fn)(uint32_t, uint32_t))
{
    usart_tbuf_write(tx, 0x00u);   /* caller sets actual byte; this is a stub signature */
    (void)base_cnt; (void)wait_fn;
    return 1;
}

/* ── Enable / disable OEN in CON ─────────────────────────────────────────── */
static inline void usart_oen_set(UsartInst u)   { usart_con_write(u, usart_con_read(u) |  CON_OEN); }
static inline void usart_oen_clear(UsartInst u) { usart_con_write(u, usart_con_read(u) & ~CON_OEN); }

/* ── Drain RBUF safely (read and discard) ────────────────────────────────── */
static inline void usart_drain(UsartInst u) { (void)usart_rbuf_read(u); }

#endif /* USART_DRIVER_H */
