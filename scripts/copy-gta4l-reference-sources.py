#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Copy the pinned reference sources into the gitignored build tree.

The Linux, vendor and PixelExperience reference trees used by the eMMC work
were unpacked under a volatile temporary directory. This host-only helper
copies the named subtrees to `out/gta4l/reference-sources/`, hashes every
source file before the copy, verifies the destination against those source
hashes, and writes a sorted SHA-256 manifest. The manifest records copy
fidelity; it is not independent proof of an upstream pin.

All named subtrees are required by default. Use --allow-missing only when a
record explicitly says a subtree is unavailable.

Usage:
    python3.11 scripts/copy-gta4l-reference-sources.py
    python3.11 scripts/copy-gta4l-reference-sources.py --check
"""
import argparse
import hashlib
import os
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
IGNORE = ('__pycache__', '*.o', '*.a', '*.pyc')
MANIFEST = 'MANIFEST.sha256'
README = 'README.txt'


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            value.update(chunk)
    return value.hexdigest()


def ignored(path):
    return any(part == '__pycache__' for part in path.parts) or \
        path.suffix in ('.o', '.a', '.pyc')


def source_manifest(source):
    files = {}
    missing = []
    for name in SUBTREES:
        origin = source / name
        if not origin.is_dir():
            missing.append(name)
            continue
        for path in origin.rglob('*'):
            if path.is_file() and not ignored(path.relative_to(origin)):
                relative = f'{name}/{path.relative_to(origin).as_posix()}'
                files[relative] = digest(path)
    return files, missing


def destination_files(dest):
    return sorted(path for path in dest.rglob('*') if path.is_file() and
                  path.name not in (MANIFEST, README))


def write_manifest(dest, files, source):
    lines = [f'{value}  {relative}'
             for relative, value in sorted(files.items())]
    (dest / MANIFEST).write_text('\n'.join(lines) + '\n')
    (dest / README).write_text(
        'Pinned reference sources for the gta4l eMMC work.\n'
        f'Copied from {source} by scripts/copy-gta4l-reference-sources.py.\n'
        'The manifest records copy fidelity, not independent upstream\n'
        'provenance. This directory is gitignored build input.\n'
        'Verify with --check after copying.\n')


def remove_tree(path):
    if not path.exists():
        return
    for root, directories, files in os.walk(path):
        for name in files:
            os.chmod(os.path.join(root, name), 0o644)
        for name in directories:
            os.chmod(os.path.join(root, name), 0o755)
    shutil.rmtree(path)


def copy_tree(source, dest):
    copied = 0
    remove_tree(dest)
    for name in SUBTREES:
        origin = source / name
        if not origin.is_dir():
            continue
        target = dest / name
        shutil.copytree(origin, target, dirs_exist_ok=True,
                        copy_function=shutil.copyfile,
                        ignore=shutil.ignore_patterns(*IGNORE))
        copied += sum(1 for path in target.rglob('*') if path.is_file())
    return copied


def verify_copy(dest, files):
    errors = []
    for relative, expected in files.items():
        path = dest / relative
        if not path.is_file():
            errors.append(f'{relative}: missing after copy')
        elif digest(path) != expected:
            errors.append(f'{relative}: source and destination differ')
    for path in destination_files(dest):
        relative = path.relative_to(dest).as_posix()
        if relative not in files:
            errors.append(f'{relative}: unexpected destination file')
    return errors


def read_manifest(dest):
    manifest = dest / MANIFEST
    if not manifest.is_file():
        return None, [f'{manifest}: missing manifest']
    listed = {}
    errors = []
    for number, line in enumerate(manifest.read_text().splitlines(), 1):
        parts = line.split('  ', 1)
        if len(parts) != 2 or len(parts[0]) != 64:
            errors.append(f'{MANIFEST}:{number}: malformed entry')
            continue
        listed[parts[1]] = parts[0]
    return listed, errors


def check(dest):
    listed, errors = read_manifest(dest)
    if listed is None:
        return errors
    for relative, expected in listed.items():
        path = dest / relative
        if not path.is_file():
            errors.append(f'{relative}: missing')
        elif digest(path) != expected:
            errors.append(f'{relative}: SHA-256 differs')
    for path in destination_files(dest):
        if path.relative_to(dest).as_posix() not in listed:
            errors.append(f'{path.relative_to(dest)}: not in manifest')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--dest', type=Path, default=DEFAULT_DEST)
    parser.add_argument('--allow-missing', action='store_true')
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
    files, missing = source_manifest(args.source)
    if missing and not args.allow_missing:
        for name in missing:
            print(f'FAIL: required subtree missing: {name}', file=sys.stderr)
        return 1
    for name in missing:
        print(f'SKIP: {name} is not present in {args.source}')
    if not files:
        print('FAIL: no reference files were found', file=sys.stderr)
        return 1
    copied = copy_tree(args.source, args.dest)
    if copied == 0:
        print('FAIL: no reference files were copied', file=sys.stderr)
        return 1
    errors = verify_copy(args.dest, files)
    if errors:
        for error in errors:
            print(f'FAIL: {error}', file=sys.stderr)
        return 1
    write_manifest(args.dest, files, args.source)
    errors = check(args.dest)
    for error in errors:
        print(f'FAIL: {error}', file=sys.stderr)
    if errors:
        return 1
    print(f'PASS: copied and source-verified {len(files)} reference files '
          f'to {args.dest}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
