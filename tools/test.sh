#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
cc="${CC:-gcc}"
"$cc" -std=c99 -O1 -g -DCORE_TEST -fno-strict-aliasing \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Imain -Ivendor \
    main/gb_core.c main/battle_fx.c host/battle_fx_test.c -o build-host/battle-fx-test
build-host/battle-fx-test "$@"
for test in save_completion graphics_pacing; do
    "$cc" -std=c99 -O1 -g -DCORE_TEST -fno-strict-aliasing \
        -fsanitize=address,undefined -fno-omit-frame-pointer -Imain -Ivendor \
        main/gb_core.c "host/${test}_test.c" -o "build-host/${test}-test"
    if [[ "$test" == save_completion ]]; then
        "build-host/${test}-test" "$@"
    else
        "build-host/${test}-test"
    fi
done
python3 -m unittest discover -s tests -v
