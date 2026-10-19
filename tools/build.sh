#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v idf.py >/dev/null 2>&1; then
    if [[ -z "${IDF_PATH:-}" || ! -f "$IDF_PATH/export.sh" ]]; then
        echo 'Activate ESP-IDF v5.5.3 first: source /path/to/esp-idf/export.sh' >&2
        exit 1
    fi
    source "$IDF_PATH/export.sh" >/dev/null
fi
if [[ $# -eq 0 ]]; then set -- build; fi
idf.py "$@"
