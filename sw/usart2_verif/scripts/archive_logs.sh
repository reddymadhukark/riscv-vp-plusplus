#!/usr/bin/env bash
# archive_logs.sh — Compress and archive all regression log directories
#
# Usage:
#   ./scripts/archive_logs.sh [--keep N]   # keep N most recent (default 5)
#
# Moves all but the N most-recent regression runs from logs/regression/
# into logs/archived/ as compressed tarballs.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REG_DIR="${ROOT}/logs/regression"
ARC_DIR="${ROOT}/logs/archived"
KEEP=5

while [[ $# -gt 0 ]]; do
    case "$1" in
        --keep) KEEP="$2"; shift ;;
        *) echo "Unknown: $1"; exit 1 ;;
    esac
    shift
done

mkdir -p "${ARC_DIR}"

# Get sorted list of run directories (oldest first)
mapfile -t RUNS < <(find "${REG_DIR}" -mindepth 1 -maxdepth 1 -type d | sort)
TOTAL=${#RUNS[@]}

if [[ "${TOTAL}" -le "${KEEP}" ]]; then
    echo "  Only ${TOTAL} run(s) — nothing to archive (keeping ${KEEP})."
    exit 0
fi

TO_ARCHIVE=$(( TOTAL - KEEP ))
echo "  Archiving ${TO_ARCHIVE} of ${TOTAL} run(s), keeping ${KEEP} most recent."

for (( i=0; i<TO_ARCHIVE; i++ )); do
    DIR="${RUNS[$i]}"
    NAME="$(basename "${DIR}")"
    TAR="${ARC_DIR}/${NAME}.tar.gz"
    echo "  Archiving ${NAME} → ${TAR}"
    tar -czf "${TAR}" -C "${REG_DIR}" "${NAME}"
    rm -rf "${DIR}"
done

echo "  Done. Archived logs in ${ARC_DIR}/"
