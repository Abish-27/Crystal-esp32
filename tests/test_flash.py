"""Offline deployment guard tests: no serial hardware or commercial ROM required."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('flash', Path(__file__).resolve().parents[1] / 'tools/flash.py')
flash = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(flash)


def table(entries):
    data = b''.join(struct.pack('<HBBII16sI', 0x50aa, *values, name.encode(), 0)
                    for name, values in entries.items())
    return data + b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(data).digest()


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.backup = self.root / 'stock.bin'
        backup = bytearray(flash.FLASH_SIZE)
        stock = table(flash.STOCK)
        backup[0x8000:0x8000 + len(stock)] = stock
        self.backup.write_bytes(backup)
        self.hash = flash.sha256(backup)
        (self.root / 'partition_table').mkdir()
        (self.root / 'partition_table/partition-table.bin').write_bytes(table(flash.EXPECTED))
        app = bytearray(32)
        app[0] = 0xe9
        struct.pack_into('<H', app, 12, 5)
        (self.root / 'crystal_badge_test.bin').write_bytes(app)
        self.rom = self.root / 'test.gbc'
        self.rom.write_bytes(bytes(0x200000))

    def validate(self, app_only=False):
        # Generated data exercises guards without shipping a commercial ROM.
        with patch.object(flash, 'ROM_SHA1', hashlib.sha1(self.rom.read_bytes()).hexdigest()):
            return flash.validate(self.backup, self.hash, self.rom, self.root, app_only)

    def test_initial_and_app_only_write_ranges(self):
        plan, _ = self.validate()
        self.assertEqual([p['offset'] for p in plan], [0x8000, 0x10000, 0x80000])
        plan, _ = self.validate(True)
        self.assertEqual([p['offset'] for p in plan], [0x10000])

    def test_backup_hash_and_truncation_rejected(self):
        self.hash = '0' * 64
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            self.validate()
        self.backup.write_bytes(b'bad')
        with self.assertRaisesRegex(ValueError, '4 MiB'):
            self.validate()

    def test_unknown_rom_rejected(self):
        with self.assertRaisesRegex(ValueError, 'ROM must match'):
            flash.validate(self.backup, self.hash, self.rom, self.root)

    def test_bad_table_checksum_rejected(self):
        data = bytearray(table(flash.EXPECTED))
        data[10] ^= 1
        with self.assertRaisesRegex(ValueError, 'MD5'):
            flash.partitions(data)

    def test_moved_storage_rejected(self):
        entries = dict(flash.EXPECTED)
        entries['storage'] = (1, 0x83, 0x2a0000, 0x140000)
        (self.root / 'partition_table/partition-table.bin').write_bytes(table(entries))
        with self.assertRaisesRegex(ValueError, 'layout'):
            self.validate()

    def test_wrong_chip_and_oversize_app_rejected(self):
        path = self.root / 'crystal_badge_test.bin'
        data = bytearray(path.read_bytes())
        struct.pack_into('<H', data, 12, 0)
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'ESP32-C3'):
            self.validate()
        path.write_bytes(bytes(0x70001))
        with self.assertRaisesRegex(ValueError, 'oversized'):
            self.validate()

    def test_live_mode_and_bootloader_checks(self):
        stock = self.backup.read_bytes()
        flash.check_live_image(stock, stock, False)
        with self.assertRaisesRegex(ValueError, 'Live layout'):
            flash.check_live_image(stock, stock, True)
        live = bytearray(stock)
        live[0] ^= 1
        with self.assertRaisesRegex(ValueError, 'bootloader'):
            flash.check_live_image(live, stock, False)


if __name__ == '__main__':
    unittest.main()
