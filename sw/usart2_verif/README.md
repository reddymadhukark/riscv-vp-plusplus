# USART2 Verification Package

Production-quality verification suite for the USART2 peripheral in
[riscv-vp-plusplus](https://github.com/agra-uni-bremen/riscv-vp-plusplus).

## Quick Start

```bash
# 1 — Build the VP (one-time)
cd /path/to/riscv-vp-plusplus
cmake -B vp/build -S vp && cmake --build vp/build --target usart2test-vp -j$(nproc)

# 2 — Set up RISC-V cross-compiler (Ubuntu 24.04)
sudo apt-get install -y gcc-riscv64-linux-gnu
mkdir -p ~/.local/bin
for t in gcc as ld ar objcopy objdump nm ranlib readelf strip size; do
    ln -sf /usr/bin/riscv64-linux-gnu-$t ~/.local/bin/riscv32-unknown-elf-$t
done
export PATH="$HOME/.local/bin:$PATH"

# 3 — Build all test ELFs
cd sw/usart2_verif && mkdir -p build && cd build
cmake .. && make -j$(nproc) install

# 4 — Full regression
cd .. && ./scripts/run_all.sh

# 5 — Single test
./scripts/run_single_test.sh functional
./scripts/run_single_test.sh stress --vcd

# 6 — Generate documents
pip install openpyxl python-docx
python3 docs/generate_xlsx.py
python3 docs/generate_docx.py
```

## Test Categories

| Category | File | IDs | What is verified |
|----------|------|-----|-----------------|
| reset | test_reset.c | RST-001..005 | Register reset values, recovery |
| rw | test_rw.c | RW-001..007 | Register R/W/W1C semantics |
| functional | test_functional.c | FN-001..005 | TBIR/TIR/RIR/EIR interrupts |
| clock | test_clock.c | CLK-001..004 | Interrupt ordering and timing |
| loopback | test_loopback.c | LB-001..004 | A↔B data path integrity |
| advanced | test_advanced.c | ADV-001..006 | OEN gating, overrun recovery |
| performance | test_performance.c | PERF-001..004 | Throughput, latency |
| stress | test_stress.c | STR-001..003 | 100+ byte sustained transfers |
| negative | test_negative.c | NEG-001..007 | Error injection |
| datatype | test_datatype.c | DT-001..003 | All 256 byte values |
| memory_leak | test_memory_leak.c | MEM-001..003 | Valgrind stability |

## Known Issues

See `errata/known_issues.txt` for 4 open spec/model gaps:
- BUG-002/003: BG and FDR registers not implemented (no-op)
- BUG-004: CON.LB loopback not routed functionally
- BUG-001: Reserved CON[31:16] bits not masked

## Deliverables

| File | Description |
|------|-------------|
| `docs/USART_TestPlan.xlsx` | 59-test plan with coverage mapping |
| `docs/usart_errata.xlsx` | Spec/model gap tracker |
| `docs/coverage_summary.xlsx` | Coverage metrics by category |
| `docs/USART_Verification_Plan.docx` | Full verification plan document |
| `logs/regression/<TS>/` | Timestamped per-run logs |
