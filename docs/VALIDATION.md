# Validation and limitations

## Recorded hardware results — 2026-10-18

These results were recorded before preparing this public repository. They are historical measurements of the working prototype, not a fresh hardware test of every public-tooling change.

| Item | Result |
|---|---|
| Target | Hack the North 2026 ESP32-C3 rev0.4, 4 MiB flash, 160 MHz |
| SDK | ESP-IDF v5.5.3 |
| Bedroom emulation | 59.73 frames/sec |
| Completed LCD transfers | 19.88–19.98 frames/sec |
| Default view | 320×240 stretch, request every third emulated frame |
| Free heap with LEDs | 95,840 bytes |
| Reported core / LED transport errors | 0 / 0 in recorded intervals |
| Original app binary | 279,872 bytes; public rebuild can differ |
| Save → restart → CONTINUE | Passed on badge |
| Physical full-screen and green chase | Confirmed by the builder |

`draw_ms` was about 39.92 ms per completed LCD update and overlapped emulation; it must not be added directly to CPU time as if execution were serial. Full emulation speed does not mean 60 distinct pictures per second on the LCD.

## Host checks

Earlier ASan/UBSan runs covered intro/new-game, reaching the bedroom, an in-game save, and a fresh-state reload. Focused event tests covered damage/healing, switching, fainting, invalid HP, banked RAM, successful-catch opcode execution, exclusions, and rename guards. The public CI uses a generated cartridge to test the event predicates without distributing a commercial ROM. Optional private-ROM tests provide narrower integration evidence than a complete game run.

The original device's two flash-save slots passed independent CRC validation. Original bootloader/NVS/PHY bytes and identity/contact/config files were checked against local backups. Raw dumps and logs are intentionally private.

## Not yet established

- Complete-game compatibility or performance in every scene.
- End-to-end physical battle/catch LED feedback during a real encounter.
- Physical shortcut/debounce correctness across repeated presses.
- AA battery runtime and audio support.
- RTC progression during power-off.
- Original full-image restoration, interrupted flashing, or power-loss fault injection.

The hardware firmware source was copied into this repository without functional changes. Public build/deployment tools replace earlier machine-specific scripts and are validated separately with offline tests. Public CI never connects to or flashes a device.

## Public repository preparation — 2026-10-18

- Generated-cartridge ASan/UBSan event checks passed; optional checks using the private Crystal v1.1 ROM also passed.
- Seven Python deployment-guard tests passed, covering write plans, backup identity, ROM identity, partition corruption/layout, wrong-chip/oversized apps, and live bootloader/layout checks.
- A fresh ESP-IDF v5.5.3 application build produced a 279,872-byte ESP32-C3 image, with 39% free in the app partition. Initial and app-only offline deployment validation both accepted the fresh build.
- Tracked-file publication scan, local documentation links, and Git whitespace checks passed. Commercial ROMs, dumps, saves, raw logs, personal email, and local workstation paths are excluded.
- No device was opened or flashed while preparing the public repository.
