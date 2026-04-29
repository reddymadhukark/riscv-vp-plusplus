# usart2test — USART2 Back-to-Back ISR Tests on RISC-V VP

Five interrupt-driven tests that validate back-to-back USART2 communication
on the `usart2test-vp` platform.  Everything runs inside a single SystemC
kernel — no QEMU co-simulation, no inter-thread signal coalescing.

## What is tested

| Test | Direction | Expected interrupts |
|------|-----------|---------------------|
| 1 | A → B, byte `0x55` | `TBIR_A` + `RIR_B`, `RBUF_B == 0x55` |
| 2 | B → A, byte `0xAA` | `TBIR_B` + `RIR_A`, `RBUF_A == 0xAA` |
| 3 | Overrun on B | `RIR_B` (first byte), then `EIR_B` (RBUF full) |
| 4 | A → B, stream `[0xDE, 0xAD, 0xBE, 0xEF]` | `TBIR_A` + `RIR_B` per byte |
| 5 | TX-complete on A | `TBIR_A` first, then `TIR_A` 2 µs later |

## Platform layout

```
0x80000000  RAM (16 MB, firmware)
0x02000000  CLINT
0x40000000  PLIC  (FE310-style)
0x09002000  USART_A  → PLIC IRQ 1
0x09003000  USART_B  → PLIC IRQ 2
0x09004000  Console UART (stdout)
0x09010000  Exiter  (write → sc_stop)
```

USART_A TX → `sc_fifo` → USART_B RX, and vice-versa.
Each USART calls `plic->gateway_trigger_interrupt(irq_id)` directly —
no `sc_signal<bool>`, no edge-delivery workarounds.

## Prerequisites

### Toolchain (bare-metal RISC-V, Ubuntu 24.04)

`gcc-riscv64-unknown-elf` is not in Ubuntu 24.04 standard repos.
Use `gcc-riscv64-linux-gnu` with symlinks:

```bash
sudo apt-get install -y gcc-riscv64-linux-gnu
mkdir -p ~/.local/bin
for t in gcc as ld ar objcopy objdump nm ranlib readelf strip size; do
    ln -sf /usr/bin/riscv64-linux-gnu-$t ~/.local/bin/riscv32-unknown-elf-$t
done
export PATH="$HOME/.local/bin:$PATH"   # add to ~/.bashrc to make permanent
```

### VP build dependencies

```bash
sudo apt-get install -y \
    cmake libboost-iostreams-dev libboost-program-options-dev \
    libboost-log-dev nlohmann-json3-dev libvncserver-dev
```

## Build

### 1 — Build the VP (one-time, or after any platform change)

```bash
cd ~/Programming/riscv-vp-plusplus
git submodule update --init -- vp/src/vendor/systemc
cmake -B vp/build -S vp -DCMAKE_BUILD_TYPE=Release
cmake --build vp/build --target usart2test-vp -j$(nproc)
# binary: vp/build/bin/usart2test-vp
```

### 2 — Build the firmware

```bash
export PATH="$HOME/.local/bin:$PATH"
cd sw/usart2test
make
# binary: sw/usart2test/main.elf
```

## Run

```bash
VP=~/Programming/riscv-vp-plusplus/vp/build/bin/usart2test-vp
FW=~/Programming/riscv-vp-plusplus/sw/usart2test/main.elf

$VP $FW
```

Expected output:

```
================================================
  USART2 ISR Interrupt Test (RISC-V PLIC)
  A <-> B bridged via sc_fifo in VP
  Interrupts via machine-mode trap handler
================================================

Test 1: A sends 0x55, expect TBIR_A + RIR_B
  [ISR] TBIR_A confirmed
  [ISR] RIR_B  confirmed
  RBUF_B=0x00000055
[PASS] Test1 A->B 0x55
...
  ALL TESTS PASSED
================================================
```

## Run with VCD waveform output

```bash
$VP --vcd /tmp/usart2test $FW
# writes /tmp/usart2test.vcd
```

## View the waveform

```bash
sudo apt-get install -y gtkwave
gtkwave /tmp/usart2test.vcd
```

In GTKWave, drag signals from the **Signal Search Tree** into the wave view.
All ten signals are under the top-level scope:

| Signal | Meaning |
|--------|---------|
| `USART_A.TBIR` | HIGH while TX-buffer interrupt is pending on USART A |
| `USART_A.TIR`  | HIGH while TX-complete interrupt is pending on USART A |
| `USART_A.RIR`  | HIGH while RX-data-ready interrupt is pending on USART A |
| `USART_A.EIR`  | HIGH while overrun-error interrupt is pending on USART A |
| `USART_A.IRQ`  | HIGH whenever any interrupt is pending on USART A |
| `USART_B.*`    | Same five signals for USART B |

Each signal goes HIGH when the corresponding STATUS bit is set (interrupt
fires) and LOW when the firmware clears it with a W1C write to the STATUS
register.  The full 5-test sequence completes in ~341 µs of simulation time.

## Makefile targets

| Target | Action |
|--------|--------|
| `make`       | Build `main.elf` |
| `make run`   | Build and run (plain, no VCD) |
| `make clean` | Remove build artefacts |

## Architecture notes

- **Interrupt path**: `b_transport` (TBUF write) or `rx_thread` (byte arrives)
  → `plic->gateway_trigger_interrupt(irq_id)` → PLIC sets pending bit →
  ISS wakes from `wfi` → machine external trap → `trap_entry` (bootstrap.S)
  → `trap_handler` in C → PLIC claim → STATUS read + W1C → PLIC complete → `mret`
- **TIR timing**: scheduled via `sc_event::notify(2 µs)` after each TBUF
  write, guaranteeing it arrives as a separate interrupt from TBIR
- **Overrun (EIR)**: detected in `rx_thread` when `m_rbuf_full` is already
  set; fires only when CON bit 6 (OEN) is set
