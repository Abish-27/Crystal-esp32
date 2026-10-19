"""Reject known private/generated payloads and credential patterns in tracked files."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_SUFFIXES = {'.gb', '.gbc', '.gba', '.rom', '.bin', '.elf', '.ram', '.sav',
                      '.srm', '.zip', '.log', '.ppm', '.pyc', '.uf2'}
FORBIDDEN_PARTS = {'private', 'backups', 'roms', 'saves', '.venv', '__pycache__', 'build'}
PATTERNS = [
    re.compile(rb'gh[pousr]_[A-Za-z0-9]{30,}'),
    re.compile(rb'github_pat_[A-Za-z0-9_]{30,}'),
    re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
    re.compile(rb'AKIA[A-Z0-9]{16}'),
    re.compile(rb'(?i)[A-Z]:[\\/]Users[\\/][^\s"\']+'),
]


def main():
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
    errors = []
    count = 0
    for name in filter(None, names):
        p = Path(name)
        if p.suffix.lower() in FORBIDDEN_SUFFIXES or set(p.parts) & FORBIDDEN_PARTS:
            errors.append(f'Private/generated file tracked: {name}')
        data = (ROOT / p).read_bytes()
        if p.suffix.lower() != '.png':
            if b'\x00' in data:
                errors.append(f'Unexpected binary file: {name}')
            for pattern in PATTERNS:
                if pattern.search(data):
                    errors.append(f'Credential or local user path pattern: {name}')
        count += 1
    require_files = ['README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'main/gb_core.c']
    for name in require_files:
        if name not in names:
            errors.append(f'Required source/document missing from Git index: {name}')
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print(f'PASS: {count} tracked files checked; no forbidden payloads or listed credential patterns')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
