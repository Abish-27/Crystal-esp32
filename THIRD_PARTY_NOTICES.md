# Third-party notices and provenance

## Hack the North badge firmware

- Source: https://github.com/hackthenorth/badge-firmware
- Revision: `23c66f09bd102120537d4607a2464515b2f0c384`
- License: GNU GPL version 3; the license text is included in `LICENSE`.
- The LCD initialization and button wiring in `main/badge_main.c` were adapted from the official firmware. This is a modified standalone integration dated 2026-10-18, with emulator scheduling, asynchronous display transfer, saves, USB diagnostics, and LED feedback. It does not include the original launcher or its application suite.
- Upstream contributors retain copyright in their original work.

## Peanut-GB CGB fork

- Source: https://github.com/PicoPlus-devel/pico-peanutGB
- Revision: `3b6137eac0017475b50cb259482e93096e6de11c`
- File: `vendor/peanut_gb.h`
- License: MIT; the complete notice remains at the top of the file.
- Copyright (c) 2018–2023 Mahyar Koshkouei.
- The header also attributes portions to SameBoy, Copyright (c) 2015–2019 Lior Halphon, under MIT.
- Changes made 2026-10-18: two palette index expressions rewritten to remove unsequenced modification, and braces added to a timing conditional. See `vendor/LOCAL_CHANGES.md`. The wrapper supplies Pico compatibility macros and RGB conversion.

## Crystal address profile

- Reference: https://github.com/pret/pokecrystal
- Revision: `3bc8daa4173e96a7f4011dad3922eb6fa5dad5c6`, built using RGBDS 1.0.4.
- A local v1.1 build was compared byte-for-byte with the supplied ROM to validate the address profile in `main/crystal_profile.h`.
- No ROM, disassembly checkout, game soundtrack, or game-save data is distributed here. The two documentation screenshots show game output and are not covered by the code license.

## Integration

Copyright (c) 2026 Abish Kulkarni. Project-authored integration and tooling are distributed under GPL-3.0. Third-party files retain their original notices. ESP-IDF is obtained separately, with its own licenses and dependency notices.
