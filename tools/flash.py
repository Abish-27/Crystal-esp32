"""Offline deployment validation; hardware writes require --flash.

Uses the caller's own verified 4 MiB recovery image. Never distributes firmware,
ROMs, device dumps, or saves. Run from an activated requirements.txt environment.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
FLASH_SIZE = 0x400000
ROM_SHA1 = 'f2f52230b536214ef7c9924f483392993e226cfb'
EXPECTED = {
    'nvs': (1, 2, 0x9000, 0x4000),
    'phy_init': (1, 1, 0xd000, 0x1000),
    'factory': (0, 0, 0x10000, 0x70000),
    'gamerom': (1, 0x40, 0x80000, 0x200000),
    'gamesave': (1, 0x41, 0x280000, 0x20000),
    'storage': (1, 0x83, 0x2b0000, 0x140000),
}
STOCK = {key: value for key, value in EXPECTED.items()
         if key not in ('gamerom', 'gamesave')}
STOCK['factory'] = (0, 0, 0x10000, 0x2a0000)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def partitions(data):
    """Read IDF entries and require a valid table MD5 trailer."""
    result = {}
    for at in range(0, len(data), 32):
        entry = data[at:at + 32]
        require(len(entry) == 32, 'Truncated partition table')
        if entry[:2] == b'\xeb\xeb':
            require(entry[16:] == hashlib.md5(data[:at]).digest(),
                    'Partition table MD5 mismatch')
            return result
        require(entry[:2] == b'\xaa\x50', 'Missing partition entry or MD5 trailer')
        _, kind, subtype, offset, size, label, flags = struct.unpack('<HBBII16sI', entry)
        label = label.rstrip(b'\0').decode('ascii')
        require(label and label not in result, 'Empty or duplicate partition name')
        require(flags == 0, 'Encrypted or flagged partitions are unsupported')
        require(offset >= 0x9000 and size > 0 and offset + size <= FLASH_SIZE,
                'Partition outside reviewed 4 MiB layout')
        result[label] = (kind, subtype, offset, size)
    raise ValueError('Missing partition table MD5 trailer')


def validate(backup, backup_sha256, rom, build_dir, app_only=False):
    recovery = Path(backup).read_bytes()
    require(len(recovery) == FLASH_SIZE, 'Recovery backup must be exactly 4 MiB')
    require(len(backup_sha256) == 64 and sha256(recovery) == backup_sha256.lower(),
            'Recovery backup SHA-256 mismatch')
    require(partitions(recovery[0x8000:0x8c00]) == STOCK,
            'Recovery backup must contain the supported stock partition layout')
    build_dir = Path(build_dir)
    table = build_dir / 'partition_table/partition-table.bin'
    app = build_dir / 'crystal_badge_test.bin'
    table_bytes = table.read_bytes()
    require(len(table_bytes) <= 0x1000 and partitions(table_bytes) == EXPECTED,
            'Build partition layout differs from reviewed layout')
    app_bytes = app.read_bytes()
    require(24 <= len(app_bytes) <= EXPECTED['factory'][3] and app_bytes[0] == 0xe9,
            'Application missing, oversized, or not an ESP image')
    require(struct.unpack_from('<H', app_bytes, 12)[0] == 5,
            'Application is not for ESP32-C3')
    rom = Path(rom)
    rom_bytes = rom.read_bytes()
    require(len(rom_bytes) == 0x200000 and hashlib.sha1(rom_bytes).hexdigest() == ROM_SHA1,
            'ROM must match supported Crystal v1.1 (2 MiB, documented SHA-1)')
    parts = [(0x10000, app)] if app_only else [(0x8000, table), (0x10000, app), (0x80000, rom)]
    plan = []
    for offset, path in parts:
        data = path.read_bytes()
        end = offset + ((len(data) + 4095) // 4096) * 4096
        require((offset == 0x8000 and end <= 0x9000)
                or (offset >= 0x10000 and end <= 0x280000),
                'Write would touch protected bootloader, NVS, PHY, saves, or storage')
        plan.append({'offset': offset, 'bytes': len(data), 'sha256': sha256(data),
                     'path': str(path.resolve())})
    return plan, recovery


def esptool(port, *args):
    return [sys.executable, '-m', 'esptool', '--chip', 'esp32c3', '--port', port,
            '--baud', '115200', '--connect-attempts', '3', *map(str, args)]


def check_live_image(live, recovery, app_only):
    require(len(live) == FLASH_SIZE, 'Live recovery readback is incomplete')
    require(live[:0x8000] == recovery[:0x8000],
            'Live bootloader differs from the supplied stock recovery image')
    layout = partitions(live[0x8000:0x8c00])
    require(layout == (EXPECTED if app_only else STOCK),
            'Live layout does not match deployment mode; use --app-only for an installed prototype')
    if app_only:
        require(hashlib.sha1(live[0x80000:0x280000]).hexdigest() == ROM_SHA1,
                'Installed ROM does not match supported profile')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backup', type=Path, required=True, help='Original stock 4 MiB recovery dump')
    parser.add_argument('--backup-sha256', required=True, help='Previously recorded backup SHA-256')
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--app-only', action='store_true', help='Update an installed prototype; preserve ROM/saves')
    parser.add_argument('--port', help='Actual Espressif USB serial port; required with --flash')
    parser.add_argument('--flash', action='store_true', help='Read a fresh recovery dump, then write the validated plan')
    args = parser.parse_args()
    try:
        plan, recovery = validate(args.backup, args.backup_sha256, args.rom,
                                  args.build_dir, args.app_only)
        print(json.dumps({'validated': True, 'mode': 'app-only' if args.app_only else 'initial',
                          'write_plan': plan}, indent=2))
        if not args.flash:
            return
        require(args.port, '--port is required with --flash')
        import serial.tools.list_ports
        device = next((p for p in serial.tools.list_ports.comports()
                       if p.device.casefold() == args.port.casefold()), None)
        require(device is not None and device.vid == 0x303a and device.pid == 0x1001,
                'Port must identify as the badge Espressif USB device (303a:1001)')
        # Freeze validated payloads so originals cannot change during the readback.
        with tempfile.TemporaryDirectory(prefix='crystal-flash-') as temp:
            staged = []
            for part in plan:
                data = Path(part['path']).read_bytes()
                require(sha256(data) == part['sha256'], 'Payload changed after validation')
                path = Path(temp) / f"{part['offset']:x}.bin"
                path.write_bytes(data)
                staged.extend([hex(part['offset']), str(path)])
            stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
            folder = ROOT / 'private/deployments' / stamp
            folder.mkdir(parents=True, exist_ok=False)
            (folder / 'plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
            live_file = folder / 'before-flash.bin'
            with (folder / 'readback.log').open('w', encoding='utf-8') as log:
                subprocess.run(esptool(args.port, 'read-flash', '0', hex(FLASH_SIZE), live_file),
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            live = live_file.read_bytes()
            (folder / 'backup-sha256.txt').write_text(sha256(live) + '\n', encoding='utf-8')
            check_live_image(live, recovery, args.app_only)
            # Check ESP checksum/hash using the esptool image parser before writing.
            subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32c3',
                            'image-info', staged[1 if args.app_only else 3]], check=True)
            command = esptool(args.port, 'write-flash', '--flash-mode', 'dio',
                              '--flash-freq', '80m', '--flash-size', '4MB', *staged)
            with (folder / 'write.log').open('w', encoding='utf-8') as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
            (folder / 'result.json').write_text(json.dumps({'exit_code': result.returncode,
                'recovery_sha256': sha256(live), 'restore_tested': False}, indent=2), encoding='utf-8')
            require(result.returncode == 0, f'Flashing failed; inspect private logs in {folder}')
            print(f'Flashed and verified by esptool. Recovery dump and logs: {folder}')
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f'Error: {exc}\n')


if __name__ == '__main__':
    main()
