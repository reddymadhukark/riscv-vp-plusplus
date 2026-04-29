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
 * VCD tracing (optional):
 *   After instantiation, register the public sig_* signals with a
 *   sc_trace_file before sc_start().  Each signal reflects the
 *   corresponding STATUS bit: HIGH while the bit is set, LOW after W1C.
 *   sig_irq is HIGH whenever any STATUS bit is set.
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

    /* ── VCD-traceable signals (HIGH = status bit active) ─────────────── */
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

        SC_METHOD(tir_method);
        sensitive << m_tir_ev;
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

    static constexpr uint32_t CON_OEN = 1u << 6;

    uint32_t m_con       = 0;
    uint32_t m_status    = 0;
    uint8_t  m_rbuf      = 0;
    bool     m_rbuf_full = false;

    sc_core::sc_event m_tir_ev;

    void sync_trace() {
        sig_tbir.write(bool(m_status & STATUS_TBIR));
        sig_tir .write(bool(m_status & STATUS_TIR));
        sig_rir .write(bool(m_status & STATUS_RIR));
        sig_eir .write(bool(m_status & STATUS_EIR));
        sig_irq .write(bool(m_status & STATUS_ALL));
    }

    void fire_irq() {
        sync_trace();
        if (plic) plic->gateway_trigger_interrupt(irq_id);
    }

    /* ── b_transport: called from ISS SC_THREAD (same scheduler context) ── */
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
                uint8_t byte = static_cast<uint8_t>(rd() & 0xFFu);
                tx_port.write(byte);
                m_status |= STATUS_TBIR;
                fire_irq();
                /* TIR fires 2 µs later — separate interrupt from TBIR */
                m_tir_ev.notify(sc_core::sc_time(2, sc_core::SC_US));
            }
            break;

        case OFF_RBUF:
            if (!is_wr) {
                wr(m_rbuf);
                m_rbuf_full = false;
            }
            break;

        case OFF_STATUS:
            if (!is_wr) {
                wr(m_status);
            } else {
                m_status &= ~rd();   /* W1C */
                sync_trace();        /* lower trace signals for cleared bits */
            }
            break;

        default:
            break;
        }
        txn.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    /* ── rx_thread: waits for bytes arriving via rx_port (sc_fifo) ─────── */
    void rx_thread()
    {
        for (;;) {
            uint8_t byte = rx_port.read();
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

    /* ── tir_method: TX-complete interrupt, 2 µs after TBUF write ──────── */
    void tir_method()
    {
        m_status |= STATUS_TIR;
        fire_irq();
    }
};
