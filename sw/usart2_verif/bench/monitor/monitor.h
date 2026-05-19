/*
 * monitor.h — Event monitoring and protocol checking
 *
 * Provides:
 *   - mon_check_order()   verify TBIR arrives before TIR for same USART
 *   - mon_check_eir_gating() verify EIR fires iff OEN=1
 *   - mon_dump_log()      print entire event log to console
 *   - mon_count_events()  count events of a given type in a window
 */

#ifndef MONITOR_H
#define MONITOR_H

#include "../common/platform.h"
#include "../env/env.h"

/* Forward-declare console output from scoreboard (defined in scoreboard.h) */
void sc_put_str(const char *s);
void sc_put_hex(uint32_t v);

/* ── mon_count_events — count occurrences of (irq_id, status_bit) in window */
static uint32_t mon_count_events(uint32_t from, uint32_t to,
                                  uint32_t irq_id, uint32_t status_bit)
{
    uint32_t i, cnt = 0u;
    if (to > g_log_count) to = g_log_count;
    for (i = from; i < to; ++i)
        if (g_log[i].irq_id == irq_id && g_log[i].status_bit == status_bit)
            ++cnt;
    return cnt;
}

/* ── mon_check_order — verify TBIR appears before TIR in the same window ──── */
/* Returns 1 if order is correct, 0 if TIR arrived before or without TBIR   */
static int mon_check_order(uint32_t from, uint32_t irq_id)
{
    int tbir_idx = find_event(from, irq_id, STATUS_TBIR);
    int tir_idx  = find_event(from, irq_id, STATUS_TIR);
    if (tbir_idx < 0 || tir_idx < 0) return 0;
    return (tir_idx > tbir_idx) ? 1 : 0;
}

/* ── mon_check_eir_gating — EIR should only fire when OEN=1 ─────────────── */
/* Returns 1 if EIR behavior matches OEN setting                             */
static int mon_check_eir_gating(uint32_t from, uint32_t irq_id, int oen_enabled)
{
    int eir_seen = (find_event(from, irq_id, STATUS_EIR) >= 0) ? 1 : 0;
    if (oen_enabled)  return eir_seen;     /* expect EIR when OEN=1 */
    else              return !eir_seen;    /* expect no EIR when OEN=0 */
}

/* ── mon_dump_log — print all events from `from` to current count ────────── */
static void mon_dump_log(uint32_t from)
{
    uint32_t i, count = g_log_count;
    sc_put_str("  [MON] Event log from ");
    sc_put_hex(from);
    sc_put_str(" to ");
    sc_put_hex(count);
    sc_put_str(":\r\n");
    for (i = from; i < count; ++i) {
        const char *irq  = (g_log[i].irq_id == IRQ_USART_A) ? "USART_A" : "USART_B";
        const char *bit  = "UNK";
        switch (g_log[i].status_bit) {
        case STATUS_TBIR: bit = "TBIR"; break;
        case STATUS_TIR:  bit = "TIR";  break;
        case STATUS_RIR:  bit = "RIR";  break;
        case STATUS_EIR:  bit = "EIR";  break;
        }
        sc_put_str("    [");
        sc_put_hex(i);
        sc_put_str("] seq=");
        sc_put_hex(g_log[i].seq);
        sc_put_str("  ");
        sc_put_str(irq);
        sc_put_str(".");
        sc_put_str(bit);
        sc_put_str("\r\n");
    }
}

#endif /* MONITOR_H */
