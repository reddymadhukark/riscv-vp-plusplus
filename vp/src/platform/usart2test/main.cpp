/*
 * USART2 ISR test platform for riscv-vp-plusplus
 *
 * Memory map:
 *   0x80000000 – 0x80FFFFFF   RAM (16 MB)
 *   0x02000000 – 0x0200FFFF   CLINT
 *   0x40000000 – 0x40FFFFFF   PLIC
 *   0x09002000 – 0x09002FFF   USART_A  (PLIC IRQ 1)
 *   0x09003000 – 0x09003FFF   USART_B  (PLIC IRQ 2)
 *   0x09004000 – 0x09004FFF   Console UART (stdout)
 *   0x09010000 – 0x09010FFF   Exiter  (any write → sc_stop)
 *
 * Run:
 *   ./build/bin/usart2test-vp sw/usart2test/main.elf
 */

#include <boost/io/ios_state.hpp>
#include <boost/program_options.hpp>
#include <cstdlib>
#include <ctime>
#include <iostream>

#include "core/common/clint.h"
#include "core/common/gdb-mc/gdb_runner.h"
#include "core/common/gdb-mc/gdb_server.h"
#include "core/rv32/elf_loader.h"
#include "core/rv32/iss.h"
#include "core/rv32/mem.h"
#include "core/rv32/syscall.h"
#include "platform/common/bus.h"
#include "platform/common/fe310_plic.h"
#include "platform/common/memory.h"
#include "platform/common/options.h"
#include "util/options.h"
#include "util/propertytree.h"
#include "usart2.h"

using namespace rv32;
namespace po = boost::program_options;

/* ── Tiny inline peripherals ──────────────────────────────────────────────── */

struct ConsoleUart : sc_core::sc_module {
    tlm_utils::simple_target_socket<ConsoleUart> tsock;
    explicit ConsoleUart(sc_core::sc_module_name n) : sc_module(n), tsock("tsock") {
        tsock.register_b_transport(this, &ConsoleUart::b_transport);
    }
    void b_transport(tlm::tlm_generic_payload &txn, sc_core::sc_time &) {
        if (txn.get_command() == tlm::TLM_WRITE_COMMAND && txn.get_address() == 4) {
            putchar(static_cast<char>(*txn.get_data_ptr()));
            fflush(stdout);
        }
        txn.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

struct Exiter : sc_core::sc_module {
    tlm_utils::simple_target_socket<Exiter> tsock;
    explicit Exiter(sc_core::sc_module_name n) : sc_module(n), tsock("tsock") {
        tsock.register_b_transport(this, &Exiter::b_transport);
    }
    void b_transport(tlm::tlm_generic_payload &, sc_core::sc_time &) {
        sc_core::sc_stop();
    }
};

/* ── Options ──────────────────────────────────────────────────────────────── */

struct UsartTestOptions : Options {
    uint64_t    mem_start_addr = 0x80000000;
    uint64_t    mem_size       = 0x01000000;  /* 16 MB */
    uint64_t    mem_end_addr   = 0x80FFFFFF;
    std::string vcd_file;   /* basename for VCD output; empty = disabled */

    UsartTestOptions() {
        add_options()
            ("vcd", po::value<std::string>(&vcd_file)->default_value(""),
             "VCD output file basename (e.g. 'trace' writes trace.vcd)");
    }

    /* Fixed peripheral addresses */
    static constexpr uint64_t CLINT_START   = 0x02000000;
    static constexpr uint64_t CLINT_END     = 0x0200FFFF;
    static constexpr uint64_t PLIC_START    = 0x40000000;
    static constexpr uint64_t PLIC_END      = 0x40FFFFFF;
    static constexpr uint64_t USART_A_START = 0x09002000;
    static constexpr uint64_t USART_A_END   = 0x09002FFF;
    static constexpr uint64_t USART_B_START = 0x09003000;
    static constexpr uint64_t USART_B_END   = 0x09003FFF;
    static constexpr uint64_t CONSOLE_START = 0x09004000;
    static constexpr uint64_t CONSOLE_END   = 0x09004FFF;
    static constexpr uint64_t EXITER_START  = 0x09010000;
    static constexpr uint64_t EXITER_END    = 0x09010FFF;
    static constexpr uint64_t SYS_START     = 0x02010000;
    static constexpr uint64_t SYS_END       = 0x020103FF;
};

/* ── sc_main ──────────────────────────────────────────────────────────────── */

int sc_main(int argc, char **argv)
{
    UsartTestOptions opt;
    opt.parse(argc, argv);

    std::srand(std::time(nullptr));

    if (!opt.property_tree_is_loaded)
        VPPP_PROPERTY_SET("", "clock_cycle_period", sc_core::sc_time,
                          sc_core::sc_time(10, sc_core::SC_NS));

    tlm::tlm_global_quantum::instance().set(
        sc_core::sc_time(opt.tlm_global_quantum, sc_core::SC_NS));

    RV_ISA_Config isa_config(opt.use_E_base_isa, opt.en_ext_Zfh);
    ISS core(&isa_config, 0);

    SimpleMemory            mem("RAM", opt.mem_size);
    CombinedMemoryInterface iss_mem_if("MemIf", core);
    ELFLoader               loader(opt.input_program.c_str());
    SyscallHandler          sys("SyscallHandler");

    FE310_PLIC<1, 16, 32, 32> plic("PLIC");
    CLINT<1>                   clint("CLINT");

    Usart2      usart_a("USART_A");
    Usart2      usart_b("USART_B");
    ConsoleUart console("Console");
    Exiter      exiter("Exiter");

    /* Back-to-back serial FIFOs (capacity 16 to avoid blocking on bursts) */
    sc_core::sc_fifo<uint8_t> fifo_a2b("FIFO_A2B", 16);
    sc_core::sc_fifo<uint8_t> fifo_b2a("FIFO_B2A", 16);

    usart_a.tx_port(fifo_a2b);  usart_b.rx_port(fifo_a2b);
    usart_b.tx_port(fifo_b2a);  usart_a.rx_port(fifo_b2a);

    /* Bus: 1 initiator (ISS), 8 targets */
    SimpleBus<1, 8> bus("Bus", nullptr, opt.break_on_transaction);

    {
        unsigned i = 0;
        bus.ports[i++] = new PortMapping(opt.mem_start_addr,    opt.mem_end_addr,      mem);
        bus.ports[i++] = new PortMapping(opt.CLINT_START,       opt.CLINT_END,         clint);
        bus.ports[i++] = new PortMapping(opt.PLIC_START,        opt.PLIC_END,          plic);
        bus.ports[i++] = new PortMapping(opt.USART_A_START,     opt.USART_A_END,       usart_a);
        bus.ports[i++] = new PortMapping(opt.USART_B_START,     opt.USART_B_END,       usart_b);
        bus.ports[i++] = new PortMapping(opt.CONSOLE_START,     opt.CONSOLE_END,       console);
        bus.ports[i++] = new PortMapping(opt.EXITER_START,      opt.EXITER_END,        exiter);
        bus.ports[i++] = new PortMapping(opt.SYS_START,         opt.SYS_END,           sys);
    }
    bus.mapping_complete();

    /* TLM socket connections */
    iss_mem_if.isock.bind(bus.tsocks[0]);
    {
        unsigned i = 0;
        bus.isocks[i++].bind(mem.tsock);
        bus.isocks[i++].bind(clint.tsock);
        bus.isocks[i++].bind(plic.tsock);
        bus.isocks[i++].bind(usart_a.tsock);
        bus.isocks[i++].bind(usart_b.tsock);
        bus.isocks[i++].bind(console.tsock);
        bus.isocks[i++].bind(exiter.tsock);
        bus.isocks[i++].bind(sys.tsock);
    }

    /* DMI for fast instruction fetch */
    std::shared_ptr<BusLock> bus_lock = std::make_shared<BusLock>();
    iss_mem_if.bus_lock = bus_lock;

    MemoryDMI dmi = MemoryDMI::create_start_size_mapping(
        mem.data, opt.mem_start_addr, mem.get_size());
    InstrMemoryProxy instr_mem(dmi, core);
    iss_mem_if.dmi_add(dmi);
    iss_mem_if.dmi_enable(opt.use_data_dmi);

    instr_memory_if *instr_mem_if = opt.use_instr_dmi
        ? static_cast<instr_memory_if*>(&instr_mem)
        : static_cast<instr_memory_if*>(&iss_mem_if);
    data_memory_if *data_mem_if = &iss_mem_if;

    /* Load firmware ELF */
    uint64_t entry_point = loader.get_entrypoint();
    try {
        loader.load_executable_image(mem, mem.get_size(), opt.mem_start_addr);
    } catch (ELFLoader::load_executable_exception &e) {
        std::cerr << e.what() << "\nRAM: 0x" << std::hex << opt.mem_start_addr << "\n";
        return -1;
    }

    core.init(instr_mem_if, opt.use_dbbcache, data_mem_if, opt.use_lscache,
              &clint, entry_point, opt.mem_end_addr);

    sys.init(mem.data, opt.mem_start_addr,
             loader.get_heap_addr(mem.get_size(), opt.mem_start_addr));
    sys.register_core(&core);
    if (opt.intercept_syscalls) core.sys = &sys;
    core.error_on_zero_traphandler = opt.error_on_zero_traphandler;

    /* Interrupt wiring */
    plic.target_harts[0]  = &core;
    clint.target_harts[0] = &core;
    usart_a.plic = &plic;  usart_a.irq_id = 1;
    usart_b.plic = &plic;  usart_b.irq_id = 2;

    /* Optional VCD tracing */
    sc_core::sc_trace_file *tf = nullptr;
    if (!opt.vcd_file.empty()) {
        tf = sc_core::sc_create_vcd_trace_file(opt.vcd_file.c_str());
        std::string a = "USART_A", b = "USART_B";
        sc_core::sc_trace(tf, usart_a.sig_tbir, a + ".TBIR");
        sc_core::sc_trace(tf, usart_a.sig_tir,  a + ".TIR");
        sc_core::sc_trace(tf, usart_a.sig_rir,  a + ".RIR");
        sc_core::sc_trace(tf, usart_a.sig_eir,  a + ".EIR");
        sc_core::sc_trace(tf, usart_a.sig_irq,  a + ".IRQ");
        sc_core::sc_trace(tf, usart_b.sig_tbir, b + ".TBIR");
        sc_core::sc_trace(tf, usart_b.sig_tir,  b + ".TIR");
        sc_core::sc_trace(tf, usart_b.sig_rir,  b + ".RIR");
        sc_core::sc_trace(tf, usart_b.sig_eir,  b + ".EIR");
        sc_core::sc_trace(tf, usart_b.sig_irq,  b + ".IRQ");
        std::cout << "[VP] VCD tracing → " << opt.vcd_file << ".vcd\n";
    }

    core.enable_trace(opt.trace_mode);
    new DirectCoreRunner(core);

    opt.handle_property_export_and_exit();
    sc_core::sc_start();
    if (tf) sc_core::sc_close_vcd_trace_file(tf);
    core.show();

    return 0;
}
