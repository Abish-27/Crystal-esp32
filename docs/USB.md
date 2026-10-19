# USB diagnostics

Install `requirements.txt`, select the actual Espressif badge port, and close other serial monitors. Examples:

```sh
python tools/console.py --port COM3 status
python tools/console.py --port COM3 shot captures/screen.png
python tools/console.py --port COM3 press start
python tools/console.py --port COM3 save
```

On Linux, substitute a device such as `/dev/ttyACM0`. The console uses 115200 baud, disables DTR/RTS before opening, and closes the port after each invocation. Serial access may still cause device/driver-specific behavior.

| Command | Purpose |
|---|---|
| `status` | Core errors, memory, frame counters, view, save sequence, LED counters |
| `shot` | Read native 160×144 framebuffer; console helper saves PNG |
| `pause`, `resume` | Toggle emulation execution |
| `press KEY [ms]`, `hold KEY ms` | Inject a game button; maximum duration 10 seconds |
| `render 1..12` | Request interval in emulated frames |
| `scale 1..4` | Native / 1.5× / aspect fit / full-screen stretch |
| `skip 0\|1` | Core graphics frame skipping |
| `led 0..3` | Off / very dim / default / brighter |
| `ledtest hurt\|hit\|catch\|save` | Simulated pattern, not evidence of a game event |
| `save` | Commit cartridge RAM and RTC |
| `reset` | Commit, then restart |
| `name NAME` | Early-game diagnostic rename, letters only, at most seven characters |

The rename command requires the known ROM profile, no battle, and an empty party. It refuses once Pokémon are present. Use the game's SAVE menu afterward. Neither `save` nor `reset` substitutes for in-game SAVE.
