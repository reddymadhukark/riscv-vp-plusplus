/*
 * usart2.h — Pure SystemC USART2 for riscv-vp-plusplus
 *
 * Register map (offsets from module base address):
 *   0x00  CON     control register (stored; bit 6 = OEN enables EIR)
 *   0x04  TBUF    TX buffer write (triggers TBIR + deferred TIR)
 *   0x08  RBUF    RX buffer read  (drains receive buffer)
 *   0x20  STATUS  sticky W1C: [0]=TBIR [1]=TIR [2]=RIR [3]=EIR
 *
 * Back-to-back wiring (in sc_main):
 *   sc_fifo<uint8_t> a2b(16), b2a(16);
 *   usart_a.tx_port(a2b);  usart_b.rx_port(a2b);
 *   usart_b.tx_port(b2a);  usart_a.rx_port(b2a);
 *
 * Interrupt delivery:
 *   plic must be set before sc_start().
 *   gateway_trigger_interrupt(irq_id) is called directly from the
 *   SystemC scheduler thread — no async_event, no pulse tricks.
 *
 * VCD-traceable signals (all public, register with sc_trace_file before sc_start):
 *
 *   sig_txd        sc_uint<8>  data byte being transmitted (set on TBUF write)
 *   sig_txd_parity bool        even parity of sig_txd
 *   sig_txd_line   bool        LOW = TX frame in progress, HIGH = idle
 *
 *   sig_rxd        sc_uint<8>  data byte just received (set when byte arrives)
 *   sig_rxd_parity bool        even parity of sig_rxd
 *   sig_rxd_line   bool        LOW = RX frame in buffer, HIGH = idle
 *
 *   sig_tbir … sig_irq  bool   HIGH while the corresponding STATUS bit is set
 */
#pragma once

#include <cstring>
#include <tlm_utils/simple_target_socket.h>
#include <systemc>
#include "core/common/irq_if.h"

struct Usart2 : sc_core::sc_module {
    tlm_utils::simple_target_socket<Usart2> tsock;
    sc_core::sc_fifo_out<uint8_t> tx_port;
    sc_core::sc_fifo_in<uint8_t>  rx_port;

    interrupt_gateway *plic   = nullptr;
    uint32_t           irq_id = 0;

    /* ── TXD: frame content + line level ──────────────────────────────── */
    sc_core::sc_signal<sc_dt::sc_uint<8>, sc_core::SC_MANY_WRITERS>
                                           sig_txd      {"sig_txd",      0};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS>
                                           sig_txd_parity{"sig_txd_parity", false};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS>
                                           sig_txd_line {"sig_txd_line", true};

    /* ── RXD: frame content + line level ──────────────────────────────── */
    sc_core::sc_signal<sc_dt::sc_uint<8>, sc_core::SC_MANY_WRITERS>
                                           sig_rxd      {"sig_rxd",      0};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS>
                                           sig_rxd_parity{"sig_rxd_parity", false};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS>
                                           sig_rxd_line {"sig_rxd_line", true};

    /* ── Interrupt STATUS signals ──────────────────────────────────────── */
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> sig_tbir{"sig_tbir", false};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> sig_tir {"sig_tir",  false};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> sig_rir {"sig_rir",  false};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> sig_eir {"sig_eir",  false};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> sig_irq {"sig_irq",  false};

    SC_HAS_PROCESS(Usart2);

    explicit Usart2(sc_core::sc_module_name n)
        : sc_module(n), tsock("tsock"), tx_port("tx_port"), rx_port("rx_port")
    {
        tsock.register_b_transport(this, &Usart2::b_transport);
        SC_THREAD(rx_thread);
        /* TIR and byte delivery both fire after one frame duration */
        SC_METHOD(tir_method);
        sensitive << m_frame_done_ev;
        dont_initialize();
        SC_METHOD(tx_deliver_method);
        sensitive << m_frame_done_ev;
        dont_initialize();
    }

private:
    static constexpr uint64_t OFF_CON    = 0x00;
    static constexpr uint64_t OFF_TBUF   = 0x04;
    static constexpr uint64_t OFF_RBUF   = 0x08;
    static constexpr uint64_t OFF_STATUS = 0x20;

    static constexpr uint32_t STATUS_TBIR = 1u << 0;
    static constexpr uint32_t STATUS_TIR  = 1u << 1;
    static constexpr uint32_t STATUS_RIR  = 1u << 2;
    static constexpr uint32_t STATUS_EIR  = 1u << 3;
    static constexpr uint32_t STATUS_ALL  = STATUS_TBIR | STATUS_TIR |
                                            STATUS_RIR  | STATUS_EIR;
    static constexpr uint32_t CON_OEN     = 1u << 6;

    uint32_t m_con       = 0;
    uint32_t m_status    = 0;
    uint8_t  m_rbuf      = 0;
    bool     m_rbuf_full = false;
    uint8_t  m_tx_byte   = 0;    /* byte held until frame completes */

    /* Single event fires after one frame duration (FRAME_DURATION).
     * Both tir_method and tx_deliver_method are sensitive to it so TIR
     * and byte delivery happen at exactly the same simulation time.       */
    sc_core::sc_event m_frame_done_ev;

    /* Even parity over 8 data bits */
    static bool parity8(uint8_t b) {
        b ^= b >> 4; b ^= b >> 2; b ^= b >> 1;
        return (b & 1u) != 0u;
    }

    void sync_irq_trace() {
        sig_tbir.write(bool(m_status & STATUS_TBIR));
        sig_tir .write(bool(m_status & STATUS_TIR));
        sig_rir .write(bool(m_status & STATUS_RIR));
        sig_eir .write(bool(m_status & STATUS_EIR));
        sig_irq .write(bool(m_status & STATUS_ALL));
    }

    void fire_irq() {
        sync_irq_trace();
        if (plic) plic->gateway_trigger_interrupt(irq_id);
    }

    void b_transport(tlm::tlm_generic_payload &txn, sc_core::sc_time & /*delay*/)
    {
        uint64_t off   = txn.get_address();
        uint8_t *ptr   = txn.get_data_ptr();
        unsigned len   = txn.get_data_length();
        bool     is_wr = (txn.get_command() == tlm::TLM_WRITE_COMMAND);

        auto rd = [&]() -> uint32_t {
            uint32_t v = 0;
            std::memcpy(&v, ptr, std::min(len, 4u));
            return v;
        };
        auto wr = [&](uint32_t v) {
            std::memcpy(ptr, &v, std::min(len, 4u));
        };

        switch (off) {
        case OFF_CON:
            if (is_wr) m_con = rd();
            else       wr(m_con);
            break;

        case OFF_TBUF:
            if (is_wr) {
                m_tx_byte = static_cast<uint8_t>(rd() & 0xFFu);
                /* Update TXD trace: data, parity, line LOW (frame start) */
                sig_txd.write(m_tx_byte);
                sig_txd_parity.write(parity8(m_tx_byte));
                sig_txd_line.write(false);
                /* TBIR fires now; TIR + byte delivery fire after one frame */
                m_status |= STATUS_TBIR;
                fire_irq();
                m_frame_done_ev.notify(sc_core::sc_time(2, sc_core::SC_US));
            }
            break;

        case OFF_RBUF:
            if (!is_wr) {
                wr(m_rbuf);
                m_rbuf_full  = false;
                sig_rxd_line.write(true);   /* buffer drained: line back HIGH */
            }
            break;

        case OFF_STATUS:
            if (!is_wr) wr(m_status);
            else {
                m_status &= ~rd();          /* W1C */
                sync_irq_trace();
            }
            break;

        default:
            break;
        }
        txn.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void rx_thread()
    {
        for (;;) {
            uint8_t byte = rx_port.read();
            /* Update RXD trace: data, parity, line LOW (frame arriving) */
            sig_rxd.write(byte);
            sig_rxd_parity.write(parity8(byte));
            sig_rxd_line.write(false);
            if (m_rbuf_full) {
                if (m_con & CON_OEN) {
                    m_status |= STATUS_EIR;
                    fire_irq();
                }
            } else {
                m_rbuf      = byte;
                m_rbuf_full = true;
                m_status   |= STATUS_RIR;
                fire_irq();
            }
        }
    }

    /* ── tx_deliver_method: forward byte to peer after one frame duration ─
     * Per spec, RIR fires at the receiver when the stop bit is sampled —
     * one full frame after the transmitter's TBUF write.  Co-fires with
     * tir_method via the shared m_frame_done_ev.                          */
    void tx_deliver_method()
    {
        tx_port.nb_write(m_tx_byte);        /* non-blocking: fifo always has space */
    }

    /* ── tir_method: TX-complete + line idle, one frame after TBUF write ── */
    void tir_method()
    {
        sig_txd_line.write(true);           /* frame complete: line back HIGH */
        m_status |= STATUS_TIR;
        fire_irq();
    }
};
