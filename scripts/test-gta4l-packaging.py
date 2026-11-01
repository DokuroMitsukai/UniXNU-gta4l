#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only checks for the gta4l packaging contract.

Verifies the board package values, the byte-identical canonical script and the
boot-DTB preflight staging.  It does not fetch sources, build an image, run
`unixnu release` or touch a device.
"""
import os
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGE_BYTES = 100_663_296
SCRIPT_SHA256 = 'f7b8e3ea043058b9c61d5b45c32b8e7669a85737c89d55d404957c3b43ec4aab'
PROFILE = 'ipados-17.7.11-21H461'


def need(condition, message):
    if not condition:
        raise SystemExit(f'FAIL {message}')


def main() -> int:
    board = tomllib.loads((ROOT / 'board.toml').read_text())

    package = board.get('package', {})
    need(package.get('unixnu') == '../UniXNU', 'package.unixnu')
    need(package.get('boot_partition_bytes') == PACKAGE_BYTES, 'package.boot_partition_bytes')
    need(package.get('defconfig', {}).get(PROFILE) == 'gta4l_defconfig', 'package.defconfig mapping')

    config = board.get('firmware', {}).get('config')
    need(config == 'gta4l', 'firmware.config')
    need(board.get('bootimg', {}).get('dtb') == f'build/{config}/boot.dtb', 'bootimg.dtb')
    need('scripts/prepare-gta4l-boot-dtb.py' in board.get('firmware', {}).get('preflight', []),
         'firmware preflight')

    script = ROOT / 'scripts/package.sh'
    need(script.is_file(), 'scripts/package.sh')
    need(os.access(script, os.X_OK), 'scripts/package.sh is executable')
    import hashlib
    need(hashlib.sha256(script.read_bytes()).hexdigest() == SCRIPT_SHA256, 'canonical script hash')
    template = ROOT.parent / 'UniXNU/templates/package.sh'
    if template.is_file():
        need(script.read_bytes() == template.read_bytes(), 'package.sh matches templates/package.sh')

    with tempfile.TemporaryDirectory(prefix='gta4l-packaging-') as name:
        work = Path(name)
        tree = work / 'tree'
        device = work / 'device'
        (tree / 'scripts').mkdir(parents=True)
        (tree / 'kits').mkdir()
        (device / 'package').mkdir(parents=True)
        (device / 'board.toml').write_bytes((ROOT / 'board.toml').read_bytes())
        (tree / 'scripts/prepare-gta4l-boot-dtb.py').write_bytes(
            (ROOT / 'scripts/prepare-gta4l-boot-dtb.py').read_bytes())
        (tree / 'kits/gta4l').symlink_to(device / 'package')

        uefi = work / 'uefi'
        (uefi / 'Resources/DTBs').mkdir(parents=True)
        body = b'\xd0\x0d\xfe\xed' + (48).to_bytes(4, 'big') + b'\0' * 32 + b'trailing'
        (uefi / 'Resources/DTBs/gta4l.dtb').write_bytes(body)

        environment = dict(os.environ, UNIXNU_UEFI=str(uefi))
        result = subprocess.run(
            [sys.executable, str(tree / 'scripts/prepare-gta4l-boot-dtb.py')],
            env=environment, text=True, capture_output=True,
        )
        need(result.returncode == 0, f'preflight failed: {result.stderr.strip()}')
        staged = tree / 'build/gta4l/boot.dtb'
        need(staged.is_file(), 'preflight output missing')
        need(staged.read_bytes() == body[:48], 'preflight did not truncate to totalsize')
        need(f'{staged.stat().st_size} bytes' in result.stdout, 'preflight did not report the size')

    print('PASS gta4l packaging contract')
    return 0


if __name__ == '__main__':
    sys.exit(main())
