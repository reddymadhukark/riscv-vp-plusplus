#!/usr/bin/env bash
# run_all.sh — Full regression: build all test ELFs, run each through the VP,
#               capture timestamped logs, produce a summary.
#
# Usage:
#   ./scripts/run_all.sh [--vp PATH_TO_VP] [--build-dir DIR] [--no-build]
#
# Outputs:
#   logs/regression/<TIMESTAMP>/  — one .log file per test
#   logs/regression/<TIMESTAMP>/SUMMARY.txt

set -euo pipefail

# ── Defaults ──────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VP="${VP:-${ROOT}/../../vp/build/bin/usart2test-vp}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build}"
NO_BUILD="${NO_BUILD:-0}"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG_DIR="${ROOT}/logs/regression/${TIMESTAMP}"

# Add symlink bin to PATH for riscv32-unknown-elf-gcc
export PATH="$HOME/.local/bin:$PATH"

mkdir -p "${LOG_DIR}"

# ── Colour helpers ─────────────────────────────────────────────────────────
GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; NC='\033[0m'

log()  { echo -e "  $*"; }
pass() { echo -e "  ${GREEN}[PASS]${NC} $*"; }
fail() { echo -e "  ${RED}[FAIL]${NC} $*"; }
info() { echo -e "  ${YELLOW}[INFO]${NC} $*"; }

# ── Build ──────────────────────────────────────────────────────────────────
if [[ "${NO_BUILD}" != "1" ]]; then
    info "Building all test ELFs …"
    mkdir -p "${BUILD_DIR}"
    pushd "${BUILD_DIR}" > /dev/null
    cmake -DCMAKE_BUILD_TYPE=Release "${ROOT}" -DCMAKE_INSTALL_PREFIX="${ROOT}" \
          > "${LOG_DIR}/cmake.log" 2>&1
    make -j"$(nproc)" install >> "${LOG_DIR}/cmake.log" 2>&1
    popd > /dev/null
    info "Build complete. ELFs in ${BUILD_DIR}/bin/"
fi

# ── Test list ──────────────────────────────────────────────────────────────
TESTS=(
    test_reset
    test_rw
    test_functional
    test_clock
    test_loopback
    test_advanced
    test_performance
    test_stress
    test_negative
    test_datatype
    test_memory_leak
)

# ── Verify VP binary ────────────────────────────────────────────────────────
if [[ ! -x "${VP}" ]]; then
    echo -e "${RED}ERROR: VP binary not found or not executable: ${VP}${NC}"
    echo "  Build the VP first: cd \$(ROOT)/../.. && make vps"
    exit 1
fi

# ── Run loop ───────────────────────────────────────────────────────────────
PASS_COUNT=0
FAIL_COUNT=0
SUMMARY_FILE="${LOG_DIR}/SUMMARY.txt"

{
    echo "================================================"
    echo "  USART2 Verification — Full Regression"
    echo "  Run: ${TIMESTAMP}"
    echo "  VP : ${VP}"
    echo "================================================"
    echo ""
} > "${SUMMARY_FILE}"

echo ""
echo "================================================"
echo "  USART2 Full Regression  —  ${TIMESTAMP}"
echo "================================================"
echo ""

for TEST in "${TESTS[@]}"; do
    ELF="${BUILD_DIR}/bin/${TEST}.elf"
    LOG="${LOG_DIR}/${TEST}.log"

    if [[ ! -f "${ELF}" ]]; then
        fail "${TEST}: ELF not found — ${ELF}"
        echo "[SKIP] ${TEST}: ELF not found" >> "${SUMMARY_FILE}"
        FAIL_COUNT=$(( FAIL_COUNT + 1 ))
        continue
    fi

    log "Running ${TEST} …"

    # Run VP with 60-second timeout
    if timeout 60 "${VP}" "${ELF}" > "${LOG}" 2>&1; then
        VP_EXIT=0
    else
        VP_EXIT=$?
    fi

    # Parse log for PASS/FAIL result line
    if grep -q "RESULT   : ALL PASSED" "${LOG}"; then
        pass "${TEST}"
        echo "[PASS] ${TEST}" >> "${SUMMARY_FILE}"
        PASS_COUNT=$(( PASS_COUNT + 1 ))
    elif grep -q "\[FAIL\]" "${LOG}"; then
        fail "${TEST}  (see ${LOG})"
        echo "[FAIL] ${TEST}" >> "${SUMMARY_FILE}"
        # Copy failing log to failures/
        cp "${LOG}" "${ROOT}/logs/failures/${TIMESTAMP}_${TEST}.log"
        FAIL_COUNT=$(( FAIL_COUNT + 1 ))
    elif [[ ${VP_EXIT} -ne 0 ]]; then
        fail "${TEST}  VP exit=${VP_EXIT}"
        echo "[FAIL] ${TEST}: VP exited with code ${VP_EXIT}" >> "${SUMMARY_FILE}"
        cp "${LOG}" "${ROOT}/logs/failures/${TIMESTAMP}_${TEST}.log"
        FAIL_COUNT=$(( FAIL_COUNT + 1 ))
    else
        info "${TEST}  (no RESULT line — check log)"
        echo "[UNKN] ${TEST}" >> "${SUMMARY_FILE}"
        FAIL_COUNT=$(( FAIL_COUNT + 1 ))
    fi

    # Always copy FINDING lines to summary
    grep "\[FINDING\]" "${LOG}" >> "${SUMMARY_FILE}" 2>/dev/null || true
done

# ── Final summary ──────────────────────────────────────────────────────────
{
    echo ""
    echo "================================================"
    echo "  Total Passed : ${PASS_COUNT}"
    echo "  Total Failed : ${FAIL_COUNT}"
    echo "  Total Tests  : $(( PASS_COUNT + FAIL_COUNT ))"
    if [[ "${FAIL_COUNT}" -eq 0 ]]; then
        echo "  OVERALL      : ALL PASSED"
    else
        echo "  OVERALL      : FAILURES DETECTED"
    fi
    echo "  Log dir      : ${LOG_DIR}"
    echo "================================================"
} | tee -a "${SUMMARY_FILE}"

echo ""
echo "  Full logs: ${LOG_DIR}"
echo ""

[[ "${FAIL_COUNT}" -eq 0 ]]
