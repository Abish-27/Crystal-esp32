# Build, backup, and deploy

This layout is for the **Hack the North 2026 ESP32-C3 badge with 4 MiB flash**. It temporarily replaces the stock application. Generic ESP32 boards and different badge revisions are not supported by this guide. Nothing in this repository contains a ROM or device backup.

## 1. Build

Install the prerequisites for ESP-IDF on your platform, then get the pinned SDK:

```sh
git clone --branch v5.5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3
. ./export.sh
cd ..
git clone https://github.com/Abish-27/esp32-crystal-badge.git
cd esp32-crystal-badge
idf.py set-target esp32c3
bash tools/build.sh
```

Use Espressif's Windows setup/export scripts if building natively on Windows. Linux/WSL was used for the original firmware builds; the Python USB tools can run on native Windows. WSL requires separate USB forwarding for hardware access, so building in WSL and using native Windows Python for flashing is an option.

The build produces `build/crystal_badge_test.bin` and `build/partition_table/partition-table.bin`. It does not embed a game ROM. Do not commit generated build outputs. ESP-IDF v5.5.3 is pinned because the display callback and performance were tested with that version.

## 2. Prepare private files

Create an environment for the USB tools, independently of the ESP-IDF build environment if needed:

```sh
python -m venv .venv
# Linux/macOS: source .venv/bin/activate
# Windows PowerShell: .\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

Put your own Crystal v1.1 ROM at `private/crystal.gbc`. Required identity:

- Size: 2,097,152 bytes.
- SHA-1: `f2f52230b536214ef7c9924f483392993e226cfb`.
- CRC32: `3358e30a` (semantic LED profile).

The firmware may initialize other compatible ROMs, but this deployment tool deliberately accepts only the tested profile. Do not use patched ROMs with unverified event addresses.

## 3. Preserve the original badge

Select the actual Espressif USB badge port; close other serial monitors. The examples use `COM3`; substitute your actual port, such as `/dev/ttyACM0` on Linux. Reading through esptool may reset the badge and use download mode. If needed, hold START while connecting USB to enter the bootloader.

```sh
mkdir private
python -m esptool --chip esp32c3 --port COM3 --baud 115200 read-flash 0 0x400000 private/original-full-flash.bin
python -c "import hashlib,pathlib; p=pathlib.Path('private/original-full-flash.bin'); b=p.read_bytes(); print('bytes=',len(b),'sha256=',hashlib.sha256(b).hexdigest())"
```

If `private` already exists, skip `mkdir`. Require a successful read and exactly 4,194,304 bytes. Record the printed SHA-256 separately and retain a second copy of the backup. A matching local hash detects later changes; it does not prove the backup can be restored. Full-image restoration has not been tested on this project. Flash backups do not include eFuses.

The original image can contain identity, contacts, configuration, and other private data. Never upload it or attach it to an issue. If the badge already runs this prototype, retain its original stock backup and use `--app-only`; a prototype dump is not a substitute for the stock recovery image.

## 4. Review and install

Replace `YOUR_RECORDED_SHA256` with the hash recorded in step 3. First validate without opening a device:

```sh
python tools/flash.py --backup private/original-full-flash.bin --backup-sha256 YOUR_RECORDED_SHA256 --rom private/crystal.gbc
```

Review the printed offsets and sizes. Add `--port COM3 --flash` to the same command to install. The flasher validates the recovery image, partition table MD5/layout, app target/size, and ROM fingerprint. It stages payloads, captures another complete private live backup, checks the installed layout/bootloader, and runs esptool's image parser before writing. Esptool verifies written payloads. The script does not erase the whole device.

| Region | Offset | Size | Initial install writes? |
|---|---|---|---|
| Existing bootloader | `0x000000` | Reserved through `0x008000` | No |
| Partition table | `0x008000` | 4 KiB reserved | Yes |
| NVS | `0x009000` | 16 KiB | No |
| PHY | `0x00d000` | 4 KiB | No |
| Prototype application | `0x010000` | 448 KiB | Yes |
| ROM | `0x080000` | 2 MiB | Yes |
| Game saves | `0x280000` | 128 KiB | No |
| Original LittleFS storage | `0x2b0000` | 1.25 MiB | No |

The app/ROM/saves occupy space formerly used by the stock application. NVS and LittleFS keep their original addresses; this firmware does not mount the original filesystem. Freshly repurposed save space can contain old app bytes, which the save loader ignores unless slot metadata and CRC are valid.

For later code updates, use the same validation command with `--app-only`. Add `--port COM3 --flash` after reviewing the plan. Only the app is written; the tool checks the installed prototype partition layout and ROM, and its fresh full readback preserves the current game-save slots privately.

**Do not use generic `idf.py flash` for this deployment.** It also writes a bootloader, whereas this path keeps the original one.

## 5. Verify operation

Check the physical display/buttons and use `python tools/console.py --port COM3 status`. Confirm full-screen scale 4, render interval 3, and no core or LED errors. Use the game's SAVE menu, then commit with HOME+START or USB `save`. Restart and verify CONTINUE before relying on persistence. Never interpret a simulated `ledtest` as proof that battle event detection works.

## Recovery reference

A full restore would write your original verified image back at address 0 using esptool for the exact chip and 4 MiB flash. This replaces prototype code, ROM, saves, and current badge data with the original snapshot. Preserve the latest prototype save/readback first. Recovery is documented as a fallback, not a tested outcome; no restore or device write is performed by the offline checks or CI.
