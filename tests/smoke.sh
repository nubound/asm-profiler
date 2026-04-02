#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ ! -x "${ROOT_DIR}/build/asm-profiler" ]]; then
    echo "missing build/asm-profiler" >&2
    exit 1
fi

timeout 2s "${ROOT_DIR}/build/asm-profiler" -- /usr/bin/sleep 1 >/dev/null 2>&1 || true
