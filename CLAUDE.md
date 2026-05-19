# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

RISC-V VP++ is a SystemC TLM-2.0–based RISC-V virtual prototype (VP) that simulates multiple RISC-V platforms, supports RV32/RV64/CHERI, and provides ISS-level simulation with optional optimization caches. It extends the original [RISC-V VP](https://github.com/agra-uni-bremen/riscv-vp).

## Build

### Prerequisites

Boost (iostreams, program_options, log), a C++17 compiler, CMake, and SystemC (vendored by default).

Install on Debian:
```bash
sudo apt-get install libboost-iostreams-dev libboost-program-options-dev libboost-log-dev
```

### Build VPs (from repo root)

```bash
make vps                    # Release build (default, -O3)
RELEASE_BUILD=OFF make vps  # Debug build (-g3)
```

This runs CMake inside `vp/build/` and installs binaries to `vp/build/bin/`.

### Manual CMake build

```bash
cd vp && mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DUSE_SYSTEM_SYSTEMC=OFF ..
make install -j$(nproc)
```

Use `-DUSE_SYSTEM_SYSTEMC=ON` to link against a system-installed SystemC instead of the vendored one in `vp/src/vendor/systemc/`.

### GUI environments (optional)

```bash
make vp-display        # Qt5-based display for basic platform
make gd32-breadboard   # Lua-based breadboard UI for GD32
make all               # All of the above
```

### Code style

```bash
make codestyle   # Validates formatting via clang-format-19
```

## Running

After building, VP binaries are in `vp/build/bin/`. Example:

```bash
./vp/build/bin/riscv32-vp --help
./vp/build/bin/tiny32-vp <elf>
./vp/build/bin/riscv32-vp --intercept-syscalls <elf>
```

Key runtime flags:
- `--intercept-syscalls` — Handle syscalls directly in the VP (no trap handler needed in firmware)
- `--use-debug-runner` — Start GDB RSP server on port 5005
- `--trace-mode` — Log each executed instruction
- `--use-dbbcache` / `--use-lscache` — Enable ISS speed-up caches (off by default)
- `--use-instr-dmi` / `--use-data-dmi` — Direct Memory Interface for instruction/data fetch
- `--tlm-global-quantum N` — TLM quantum in cycles (default 10)

To exit the VP terminal: press `Ctrl-A`, then `Ctrl-X`.

## Testing

### Run all tests (CTest)

```bash
cd vp/build && ctest --verbose
```

### Run individual test suites

```bash
cd vp/tests/gdb && ./test.sh          # GDB RSP protocol tests
cd vp/tests/libgdb && ./test.sh       # libgdb parser/memory leak tests
cd vp/tests/integration && ./test.sh  # ISS integration tests
```

Integration and GDB tests require the RISC-V GNU toolchain on `PATH`. The libgdb tests require valgrind.

### Software examples

```bash
cd sw/<example> && make       # Build one example
cd sw/<example> && make sim   # Build and simulate
```

Cross-compilation uses toolchain files in `vp/tests/toolchain/` (e.g., `rv32.cmake`, `rv64.cmake`).

## Architecture

### Directory layout

- `vp/src/core/` — ISS implementations (RV32, RV64, CHERI RV64)
- `vp/src/platform/` — Platform definitions (each has a `main.cpp` that wires SystemC components)
- `vp/src/platform/common/` — Shared peripherals (bus, UART, PLIC, GPIO, memory, channels)
- `vp/src/util/` — Utilities (ELF loading, GDB server, logging)
- `vp/src/vendor/` — Vendored deps (SystemC, SoftFloat, MPC, LuaBridge3, ptest)
- `sw/` — Bare-metal and RTOS firmware examples built with the RISC-V toolchain
- `env/` — Qt/Lua GUI frontend environments
- `vp/tests/` — Test suites (gdb, libgdb, integration)

### ISS core structure (`vp/src/core/`)

The ISS is split by ISA width:
- `common/` — Shared components: instruction decoding (`instr.cpp`), CSRs, MMU (Sv32/Sv39/Sv48), CLINT, ELF loader, FP (`fp.h`), RVV vector extension (`v.h`), DBBCache, LSCache
- `rv32/` / `rv64/` — Width-specific ISS templates (`iss.h`), CSR definitions, memory interface
- `rv64_cheriv9/` + `common_cheriv9/` — CHERI capability extension (tagged memory, capability CSRs, RVFI-DII interface)

The ISS is a C++ template class. Platforms instantiate it and connect it to a TLM bus via the `mem.h` interface.

### Platform structure (`vp/src/platform/`)

Each platform directory contains a `*_main.cpp` (or `main.cpp`) that:
1. Parses CLI options (Boost program_options via `common/options.h`)
2. Instantiates SystemC modules (ISS, bus, peripherals)
3. Connects TLM sockets and maps memory regions
4. Calls `sc_start()` to run the simulation

Platforms:

| Directory | Binaries | Notes |
|-----------|----------|-------|
| `basic/` | `riscv32-vp`, `riscv64-vp`, `riscv64-cheriv9-vp` | Full-featured generic platform |
| `tiny/` | `tiny32`, `tiny64` | Minimal, used by integration tests |
| `tiny-mc/` | `tiny32-mc`, `tiny64-mc` | Multi-core variant |
| `linux/` | `linux32-vp`, `linux64-vp` | Virtual memory, device tree, memory-mapped rootfs |
| `qemu_virt/` | `qemu_virt32-vp`, `qemu_virt64-vp`, CHERI variants | QEMU virt machine layout |
| `hifive/` | `hifive-vp` | SiFive HiFive1, VBB protocol |
| `gd32/` | `gd32vf103-vp` | GD32VF103 (Nuclei N205), ECLIC, ILI9341 display |
| `microrv32/` | `microrv32-vp` | Minimal RV32 |
| `usart2test/` | `usart2test` | Pure SystemC USART2 test harness (no ISS), VCD tracing |

### TLM/SystemC conventions

- All components use TLM-2.0 with temporal decoupling and a quantum keeper.
- `SimpleBus` (in `platform/common/`) routes TLM transactions to peripherals by address range.
- Peripherals implement `b_transport` and optionally `transport_dbg` (for non-intrusive debug access).
- The ISS drives the bus as a TLM initiator; peripherals are targets.

### ISS optimization caches

- **DBBCache** (Dynamic Basic Block Cache): Caches decoded instruction sequences with computed goto. Enabled at compile time by `DBBCACHE_ENABLED` and at runtime with `--use-dbbcache`.
- **LSCache** (Load/Store Cache): Caches VA→PA translations using DMI pointers for fast load/store. Enabled by `LSCACHE_ENABLED` / `--use-lscache`.
- Both are off by default; they improve throughput significantly for compute-heavy workloads.

### Software (sw/) syscall model

Two approaches for firmware syscalls:
1. **Trap-based**: Firmware installs its own trap handler; `ecall` goes through the simulated RISC-V trap mechanism.
2. **Intercept-based**: Pass `--intercept-syscalls` to the VP; the ISS intercepts `ecall` and handles it in the host process without a firmware trap handler.

### GD32 platform specifics

The GD32 platform (`platform/gd32/`) uses a custom `nuclei_core` ISS subclass that adds ECLIC (Enhanced Core-Level Interrupt Controller) and the Nuclei timer on top of the standard RV32 ISS. It does not use the standard CLINT. The optional `gd32-breadboard` GUI uses Lua scripts for component layout.

## Submodules

Initialize all submodules before building:
```bash
git submodule update --init --recursive
```

Key submodules: SystemC (vendored), SoftFloat, virtual-bus, vbb-protocol, LuaBridge3, ptest.
