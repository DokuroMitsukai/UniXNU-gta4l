#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Stage the compiled boot device tree from the firmware checkout.

The release pack step reads ``[bootimg] dtb`` relative to the assembled tree.
This firmware preflight copies the Mu-Silicium device tree named by
``[firmware] config`` to that path and truncates it to the FDT totalsize.

The Mu-Silicium ``Resources/DTBs/gta4l.dtb`` file starts with the board FDT but
carries further FDT images and padding after it.  The bootloader appends one
FDT, so only the first image is staged; ``bootimg`` then applies the
``[[bootimg.dtb_node]]`` edits and verifies the result round-trips.  See
docs/boot-dtb-provenance.md in the device repository for the reproduction.

Host-only: reads the firmware checkout and writes inside the build tree.  It
does not touch a device, the firmware checkout or an image.
"""
import hashlib
import os
import struct
import sys
import tomllib
from pathlib import Path

FDT_MAGIC = b'\xd0\x0d\xfe\xed'
FDT_HEADER_BYTES = 40


def main() -> int:
    tree = Path(__file__).resolve().parent.parent
    device = tree
    kit_link = tree / 'kits' / 'gta4l'
    if kit_link.exists():
        # kits/gta4l -> <device>/package
        device = kit_link.resolve().parent

    board = tomllib.loads((device / 'board.toml').read_text())
    config = board.get('firmware', {}).get('config')
    if not config:
        print('[prepare-gta4l-boot-dtb] board.toml has no [firmware] config', file=sys.stderr)
        return 1

    uefi = os.environ.get('UNIXNU_UEFI')
    if not uefi:
        print('[prepare-gta4l-boot-dtb] UNIXNU_UEFI is not set', file=sys.stderr)
        return 1

    source = Path(uefi) / 'Resources/DTBs' / f'{config}.dtb'
    if not source.is_file():
        print(f'[prepare-gta4l-boot-dtb] missing firmware device tree: {source}', file=sys.stderr)
        return 1

    data = source.read_bytes()
    if len(data) < FDT_HEADER_BYTES or data[:4] != FDT_MAGIC:
        print(f'[prepare-gta4l-boot-dtb] not an FDT: {source}', file=sys.stderr)
        return 1
    totalsize = struct.unpack_from('>I', data, 4)[0]
    if totalsize < FDT_HEADER_BYTES or totalsize > len(data):
        print(f'[prepare-gta4l-boot-dtb] invalid FDT totalsize {totalsize} in {source}', file=sys.stderr)
        return 1

    output = tree / 'build' / config / 'boot.dtb'
    output.parent.mkdir(parents=True, exist_ok=True)
    image = data[:totalsize]
    output.write_bytes(image)
    digest = hashlib.sha256(image).hexdigest()
    print(f'[prepare-gta4l-boot-dtb] {source} -> {output} ({len(image)} bytes, sha256 {digest})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
