#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Copy the pinned reference sources into the gitignored build tree.

The Linux, vendor and PixelExperience reference trees used by the eMMC work
were unpacked under a volatile temporary directory. This host-only helper
copies the named subtrees to `out/gta4l/reference-sources/`, writes a sorted
SHA-256 manifest, and can verify the copy later. It never downloads anything
and never touches a device.

Usage:
    python3.11 scripts/copy-gta4l-reference-sources.py
    python3.11 scripts/copy-gta4l-reference-sources.py --check
"""
import argparse
import hashlib
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = Path('/tmp/opencode')
DEFAULT_DEST = ROOT / 'out' / 'gta4l' / 'reference-sources'
SUBTREES = (
    'linux-pin',
    'linux-verify',
    'samsung-src/CWI2',
    'samsung-src/CXG1',
    'pe-ref',
    'lineage-kernel-files',
    'provenance-check',
)
MANIFEST = 'MANIFEST.sha256'
README = 'README.txt'


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            value.update(chunk)
    return value.hexdigest()


def files(dest):
    return sorted(path for path in dest.rglob('*') if path.is_file() and
                  path.name not in (MANIFEST, README))


def write_manifest(dest, source):
    lines = [f'{digest(path)}  {path.relative_to(dest).as_posix()}'
             for path in files(dest)]
    (dest / MANIFEST).write_text('\n'.join(lines) + '\n')
    (dest / README).write_text(
        'Pinned reference sources for the gta4l eMMC work.\n'
        f'Copied from {source} by scripts/copy-gta4l-reference-sources.py.\n'
        'This directory is gitignored build input, not a tracked record.\n'
        'Verify with --check after copying.\n')


def copy_tree(source, dest):
    copied = 0
    missing = []
    for name in SUBTREES:
        origin = source / name
        if not origin.is_dir():
            missing.append(name)
            continue
        target = dest / name
        shutil.copytree(origin, target, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('__pycache__', '*.o',
                                                      '*.a', '*.pyc'))
        copied += sum(1 for path in target.rglob('*') if path.is_file())
    return copied, missing


def check(dest):
    manifest = dest / MANIFEST
    if not manifest.is_file():
        return [f'{manifest}: missing manifest']
    errors = []
    listed = {}
    for number, line in enumerate(manifest.read_text().splitlines(), 1):
        parts = line.split('  ', 1)
        if len(parts) != 2 or len(parts[0]) != 64:
            errors.append(f'{MANIFEST}:{number}: malformed entry')
            continue
        listed[parts[1]] = parts[0]
    for relative, expected in listed.items():
        path = dest / relative
        if not path.is_file():
            errors.append(f'{relative}: missing')
        elif digest(path) != expected:
            errors.append(f'{relative}: SHA-256 differs')
    for path in files(dest):
        if path.relative_to(dest).as_posix() not in listed:
            errors.append(f'{path.relative_to(dest)}: not in manifest')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--dest', type=Path, default=DEFAULT_DEST)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.check:
        errors = check(args.dest)
        for error in errors:
            print(f'FAIL: {error}')
        if errors:
            return 1
        print(f'PASS: {args.dest} matches {MANIFEST}')
        return 0
    if not args.source.is_dir():
        print(f'FAIL: source {args.source} is not a directory', file=sys.stderr)
        return 1
    copied, missing = copy_tree(args.source, args.dest)
    write_manifest(args.dest, args.source)
    for name in missing:
        print(f'SKIP: {name} is not present in {args.source}')
    if not copied:
        print('FAIL: no reference files were copied', file=sys.stderr)
        return 1
    errors = check(args.dest)
    for error in errors:
        print(f'FAIL: {error}', file=sys.stderr)
    if errors:
        return 1
    print(f'PASS: copied {copied} reference files to {args.dest} '
          f'and verified {MANIFEST}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
