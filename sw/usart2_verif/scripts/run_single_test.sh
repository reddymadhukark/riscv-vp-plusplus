#!/usr/bin/env bash
# run_single_test.sh — Build and run one named test category
#
# Usage:
#   ./scripts/run_single_test.sh <test_name> [--no-build] [--vcd]
#
# Examples:
#   ./scripts/run_single_test.sh functional
#   ./scripts/run_single_test.sh reset --no-build
#   ./scripts/run_single_test.sh stress --vcd
#
# Valid test names:
#   reset rw functional clock loopback advanced performance stress
#   negative datatype memory_leak

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VP="${VP:-${ROOT}/../../vp/build/bin/usart2test-vp}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build}"
export PATH="$HOME/.local/bin:$PATH"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; NC='\033[0m'

# ── Argument parsing ────────────────────────────────────────────────────────
if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <test_name> [--no-build] [--vcd]"
    echo "Valid: reset rw functional clock loopback advanced performance stress negative datatype memory_leak"
    exit 1
fi

TEST_NAME="$1"
NO_BUILD=0
USE_VCD=0

shift
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) NO_BUILD=1 ;;
        --vcd)      USE_VCD=1  ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

TARGET="test_${TEST_NAME}"
ELF="${BUILD_DIR}/bin/${TARGET}.elf"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG_DIR="${ROOT}/logs/regression/${TIMESTAMP}"
LOG="${LOG_DIR}/${TARGET}.log"

mkdir -p "${LOG_DIR}"

# ── Build ───────────────────────────────────────────────────────────────────
if [[ "${NO_BUILD}" != "1" ]]; then
    echo -e "${YELLOW}Building ${TARGET}…${NC}"
    mkdir -p "${BUILD_DIR}"
    pushd "${BUILD_DIR}" > /dev/null
    cmake -DCMAKE_BUILD_TYPE=Release "${ROOT}" -DCMAKE_INSTALL_PREFIX="${ROOT}" \
          > /dev/null 2>&1
    make -j"$(nproc)" "${TARGET}" > /dev/null 2>&1
    make install > /dev/null 2>&1 || true
    popd > /dev/null
fi

if [[ ! -f "${ELF}" ]]; then
    echo -e "${RED}ELF not found: ${ELF}${NC}"
    exit 1
fi

# ── Run ─────────────────────────────────────────────────────────────────────
echo -e "${YELLOW}Running ${TARGET}…${NC}"
echo "  VP  : ${VP}"
echo "  ELF : ${ELF}"
echo "  LOG : ${LOG}"
echo ""

VP_ARGS=("${VP}" "${ELF}")
if [[ "${USE_VCD}" == "1" ]]; then
    VCD_FILE="${LOG_DIR}/${TARGET}"
    VP_ARGS+=(--vcd "${VCD_FILE}")
    echo -e "  VCD : ${VCD_FILE}.vcd"
fi

timeout 60 "${VP_ARGS[@]}" 2>&1 | tee "${LOG}"

echo ""

# ── Result ──────────────────────────────────────────────────────────────────
if grep -q "RESULT   : ALL PASSED" "${LOG}"; then
    echo -e "${GREEN}================================================${NC}"
    echo -e "${GREEN}  RESULT: PASSED — ${TARGET}${NC}"
    echo -e "${GREEN}================================================${NC}"
elif grep -q "\[FAIL\]" "${LOG}"; then
    echo -e "${RED}================================================${NC}"
    echo -e "${RED}  RESULT: FAILED — ${TARGET}${NC}"
    echo -e "${RED}================================================${NC}"
    cp "${LOG}" "${ROOT}/logs/failures/${TIMESTAMP}_${TARGET}.log"
    echo "  Failure log: ${ROOT}/logs/failures/${TIMESTAMP}_${TARGET}.log"
    exit 1
else
    echo -e "${YELLOW}  Result unclear — inspect ${LOG}${NC}"
fi

echo "  Log: ${LOG}"
