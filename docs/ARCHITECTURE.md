# Architecture and hardware

The tested target is the Hack the North 2026 badge: ESP32-C3-MINI-1, 4 MiB flash, ST7789 320×240 SPI LCD, shift-register buttons, and six WS2812 LEDs. The application selects 160 MHz. Hardware reference: https://github.com/hackthenorth/badge-hardware.

| Function | GPIO |
|---|---:|
| LCD MOSI / clock / CS / DC / reset | 10 / 1 / 2 / 0 / 4 |
| Button shift-register load / clock / data | 20 / 21 / 7 |
| START (also boot strap) | 9 |
| LED data | 3 |

No additional audio hardware is required by this release. I2C peripherals, NFC, wireless, and the stock UI are not initialized.

## Emulation and display

`gb_core.c` wraps the vendored CGB core. ROM reads use a memory-mapped flash partition; 32 KiB cartridge RAM and a 160×144 RGB565 framebuffer live in RAM. The measured target core state occupies 128,928 bytes, excluding RTOS, DMA, and application allocations.

The main task emulates frames. Every third frame it attempts to acquire a binary semaphore and copy the framebuffer into a stable snapshot. A higher-priority worker scales that snapshot and sends 16-row DMA stripes to the LCD. Requests arriving while the worker is busy are dropped. This bounds queued work and lets emulation overlap LCD transfers.

The SPI completion callback explicitly calls `portYIELD_FROM_ISR` when needed; this addressed completion latency observed with the tested SDK. Scale changes serialize with the display worker. Optional native/aspect-fit views are diagnostics; the default stretches the complete image to 320×240 without cropping.

## Saves

Each 64 KiB slot contains a 32-byte header, 32 KiB cartridge RAM, and RTC metadata. A commit erases the inactive slot, writes RAM, then writes the header last. On startup, valid magic/version/size and CRC determine which slot can be loaded; the newest valid sequence wins. A previous complete slot remains available if the other slot's write is interrupted, though power-interruption fault testing has not been performed.

The CRC covers the five RTC bytes followed by cartridge RAM. Dirty RAM is committed every 30 seconds; HOME+START and USB `save` request an explicit commit. The game must first write progress using its own SAVE menu. RTC advances while running, including while the emulator is paused, but not while powered off.

## LED events

Semantic hooks are enabled only when the mapped ROM CRC32 matches the known Crystal v1.1 profile. HP deltas use explicit WRAM banks and reject invalid values and identity changes. Red indicates player HP loss; blue indicates opponent HP loss, including poison/recoil. A ROM-read hook checks both banked ROM address and opcode-fetch PC at the successful-catch branch. Tutorial/debug modes and trainer catches are excluded.

Patterns: left red damage pulse, right blue damage pulse, green catch chase, cyan save fade, amber pause, and dim startup/error cues. Default maximum channel value is 12/255; idle LEDs are off. Native RMT drives the LED chain.
