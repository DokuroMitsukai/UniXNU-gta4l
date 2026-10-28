#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Check a gta4l diagnostic boot image against a known-working baseline.

The bootloader-facing boot DTB is not the minimal XNU hardware DTB. This gate
requires the baseline boot header, firmware + BootShim, boot kernel section and
appended boot DTB to remain byte-identical. Only the ramdisk payload may differ,
and it must stay the same size so the bootloader-side initrd range is unchanged.

The candidate boot DTB must also retain the Qualcomm identification properties.
"""
import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

PAGE = 4096
MAGIC = b'ANDROID!'
HEADER_NAMES = (
    'kernel_size', 'kernel_address', 'ramdisk_size', 'ramdisk_address',
    'second_size', 'second_address', 'tags_address', 'page_size',
    'header_version', 'os_version',
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def parse(path):
    data = Path(path).read_bytes()
    if len(data) < PAGE or data[:8] != MAGIC:
        raise SystemExit(f'{path}: not an Android boot image')
    values = struct.unpack_from('<10I', data, 8)
    header = dict(zip(HEADER_NAMES, values))
    page = header['page_size']
    if page != PAGE:
        raise SystemExit(f'{path}: page size {page} is not {PAGE}')
    if header['second_size']:
        raise SystemExit(f'{path}: second stage is not supported')
    kernel_size = header['kernel_size']
    kernel = data[page:page + kernel_size]
    if len(kernel) != kernel_size:
        raise SystemExit(f'{path}: truncated kernel section')
    if kernel[:2] != b'\x1f\x8b':
        raise SystemExit(f'{path}: boot kernel section is not gzip')
    decompressor = zlib.decompressobj(31)
    firmware = decompressor.decompress(kernel)
    dtb = decompressor.unused_data
    if not decompressor.eof:
        raise SystemExit(f'{path}: incomplete gzip stream')
    ramdisk_start = page * (1 + (kernel_size + page - 1) // page)
    ramdisk = data[ramdisk_start:ramdisk_start + header['ramdisk_size']]
    if len(ramdisk) != header['ramdisk_size']:
        raise SystemExit(f'{path}: truncated ramdisk')
    return {
        'path': str(path),
        'image_bytes': len(data),
        'image_sha256': sha(data),
        'header': header,
        'kernel_sha256': sha(kernel),
        'firmware_sha256': sha(firmware),
        'firmware_bytes': len(firmware),
        'dtb_sha256': sha(dtb),
        'dtb_bytes': len(dtb),
        'dtb_has_qcom_ids': b'qcom,msm-id' in dtb and b'qcom,board-id' in dtb,
        'ramdisk_sha256': sha(ramdisk),
        'ramdisk_bytes': len(ramdisk),
    }


def check(baseline, candidate):
    result = {'baseline': baseline, 'candidate': candidate, 'checks': {}}

    def require(name, condition):
        result['checks'][name] = bool(condition)
        if not condition:
            raise ValueError(name)

    try:
        require('header_matches_baseline',
                candidate['header'] == baseline['header'])
        require('firmware_matches_baseline',
                candidate['firmware_sha256'] == baseline['firmware_sha256'])
        require('boot_dtb_matches_baseline',
                candidate['dtb_sha256'] == baseline['dtb_sha256'])
        require('boot_kernel_section_matches_baseline',
                candidate['kernel_sha256'] == baseline['kernel_sha256'])
        require('ramdisk_size_matches_baseline',
                candidate['ramdisk_bytes'] == baseline['ramdisk_bytes'])
        require('candidate_dtb_has_qcom_ids',
                candidate['dtb_has_qcom_ids'])
        result['pass'] = True
    except ValueError as error:
        result['pass'] = False
        result['failed_check'] = str(error)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    result = check(parse(args.baseline), parse(args.candidate))
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f'baseline:  {result["baseline"]["path"]}')
        print(f'candidate: {result["candidate"]["path"]}')
        for name, ok in result['checks'].items():
            print(f'{"PASS" if ok else "FAIL"} {name}')
        if not result['pass']:
            print(f'FAIL: {result["failed_check"]}', file=sys.stderr)
    raise SystemExit(0 if result['pass'] else 1)


if __name__ == '__main__':
    main()
