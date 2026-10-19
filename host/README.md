# Host tests

Linux, macOS with GCC/Clang, or WSL is suitable. Run from the repository root:

```sh
bash tools/test.sh
```

This builds a sanitizer-enabled event test and runs Python deployment tests. The default C test generates an empty test cartridge containing a minimal header and a short CPU instruction; it includes no commercial ROM. It covers HP changes, invalid snapshots, switches/fainting, WRAM banks, the opcode-fetch predicate, and rename guards. This does not prove actual game progression or hardware behavior.

Optionally supply your own Crystal v1.1 ROM (SHA-1 `f2f52230b536214ef7c9924f483392993e226cfb`) to execute the event checks against its actual bytes:

```sh
bash tools/test.sh private/crystal.gbc
```

## Gameplay save/reload smoke harness

```sh
mkdir -p build-host private/run-save private/run-reload
gcc -std=c99 -O1 -g -fno-strict-aliasing -fsanitize=address,undefined \
  -Ivendor -Imain main/gb_core.c host/smoke.c -o build-host/smoke
build-host/smoke private/crystal.gbc private/run-save 25800 host/save-input.txt private/test.ram
build-host/smoke private/crystal.gbc private/run-reload 3000 host/reload-input.txt private/test.ram
```

Use a fresh save filename for a fresh new game. The harness loads an existing cartridge-save file and overwrites it afterward. This file contains 32 KiB RAM plus five RTC bytes, distinct from the badge flash-slot format. Scripts use a placeholder trainer name. Rows contain frame index, duration, and pressed-key mask: A=1, B=2, Select=4, Start=8, Right=16, Left=32, Up=64, Down=128.

Inspect the generated PPM images as well as exit status and core-error count. Host speed is not an ESP32 benchmark.
