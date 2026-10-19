# Local Peanut core changes

Base: PicoPlus-devel/pico-peanutGB commit 3b6137eac0017475b50cb259482e93096e6de11c, peanut_gb.h. MIT notice retained.

The ESP32 compiler reported two unsequenced palette index updates. Both `id = (++id) & 63` expressions now use `id = (id + 1) & 63`, preserving wraparound with a single assignment. LCD timing's conditional gained braces to remove misleading indentation without changing its branches.

The wrapper provides the Pico-specific function-name and RGB conversion macros. No other vendor changes are intended. Host sanitizers are rerun after these patches; see the main session log for actual results and hardware status.
