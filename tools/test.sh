#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
cc="${CC:-gcc}"
"$cc" -std=c99 -O1 -g -DCORE_TEST -fno-strict-aliasing \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Imain -Ivendor \
    main/gb_core.c main/battle_fx.c host/battle_fx_test.c -o build-host/battle-fx-test
build-host/battle-fx-test "$@"
python3 -m unittest discover -s tests -v
