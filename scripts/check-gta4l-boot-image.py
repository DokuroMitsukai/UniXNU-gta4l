#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Check a gta4l diagnostic boot image against a known-working baseline.

The bootloader-facing boot DTB is not the minimal XNU hardware DTB. This gate
requires the baseline boot header page, firmware + BootShim, boot kernel section
and appended boot DTB to remain byte-identical. Only the ramdisk payload may
differ, and it must stay the same size so the bootloader-side initrd range is
unchanged.

The candidate must also have zero kernel padding, no unexpected trailing data,
a valid appended FDT header, and the Qualcomm identification properties.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
import zlib
from pathlib import Path

PAGE = 4096
MAGIC = b'ANDROID!'
FDT_MAGIC = b'\xd0\x0d\xfe\xed'
HEADER_ID_OFFSET = 576
HEADER_ID_SIZE = 32
HEADER_NAMES = (
    'kernel_size', 'kernel_address', 'ramdisk_size', 'ramdisk_address',
    'second_size', 'second_address', 'tags_address', 'page_size',
    'header_version', 'os_version',
)
SHA256_RE = re.compile(r'^[0-9a-f]{64}$')


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
    if header['header_version'] != 1:
        raise SystemExit(f'{path}: header version {header["header_version"]} is not 1')
    if header['second_size']:
        raise SystemExit(f'{path}: second stage is not supported')

    kernel_start = page
    kernel_end = kernel_start + header['kernel_size']
    kernel_padded_end = page * (1 + (header['kernel_size'] + page - 1) // page)
    if kernel_padded_end > len(data):
        raise SystemExit(f'{path}: truncated kernel section')
    kernel = data[kernel_start:kernel_end]
    kernel_padding = data[kernel_end:kernel_padded_end]
    if kernel[:2] != b'\x1f\x8b':
        raise SystemExit(f'{path}: boot kernel section is not gzip')

    decompressor = zlib.decompressobj(31)
    firmware = decompressor.decompress(kernel)
    dtb = decompressor.unused_data
    if not decompressor.eof:
        raise SystemExit(f'{path}: incomplete gzip stream')

    ramdisk_start = kernel_padded_end
    ramdisk_end = ramdisk_start + header['ramdisk_size']
    if ramdisk_end > len(data):
        raise SystemExit(f'{path}: truncated ramdisk')
    ramdisk = data[ramdisk_start:ramdisk_end]
    trailing = data[ramdisk_end:]

    header_id = data[HEADER_ID_OFFSET:HEADER_ID_OFFSET + HEADER_ID_SIZE]
    id_hash = hashlib.sha1()
    id_hash.update(kernel)
    id_hash.update(struct.pack('<I', len(kernel)))
    id_hash.update(ramdisk)
    id_hash.update(struct.pack('<I', len(ramdisk)))
    id_hash.update(struct.pack('<I', 0))
    id_hash.update(struct.pack('<I', 0))
    expected_header_id = id_hash.digest().ljust(HEADER_ID_SIZE, b'\0')
    header_page_no_id = (data[:HEADER_ID_OFFSET] + b'\0' * HEADER_ID_SIZE
                         + data[HEADER_ID_OFFSET + HEADER_ID_SIZE:page])

    dtb_total_size = struct.unpack_from('>I', dtb, 4)[0] if len(dtb) >= 8 else 0
    return {
        'path': str(path),
        'image_bytes': len(data),
        'image_sha256': sha(data),
        'header': header,
        'header_page_no_id_sha256': sha(header_page_no_id),
        'header_id': header_id.hex(),
        'expected_header_id': expected_header_id.hex(),
        'header_id_matches': header_id == expected_header_id,
        'kernel_sha256': sha(kernel),
        'kernel_padding_bytes': len(kernel_padding),
        'kernel_padding_zero': not any(kernel_padding),
        'firmware_sha256': sha(firmware),
        'firmware_bytes': len(firmware),
        'dtb_sha256': sha(dtb),
        'dtb_bytes': len(dtb),
        'dtb_magic_ok': dtb[:4] == FDT_MAGIC,
        'dtb_total_size': dtb_total_size,
        'dtb_total_size_ok': dtb_total_size == len(dtb),
        'dtb_has_qcom_ids': b'qcom,msm-id' in dtb and b'qcom,board-id' in dtb,
        'ramdisk_start': ramdisk_start,
        'ramdisk_end': ramdisk_end,
        'ramdisk_sha256': sha(ramdisk),
        'ramdisk_bytes': len(ramdisk),
        'trailing_bytes': len(trailing),
    }


def check(baseline, candidate, expected_ramdisk_sha256=None):
    result = {
        'baseline': baseline,
        'candidate': candidate,
        'expected_ramdisk_sha256': expected_ramdisk_sha256,
        'checks': {},
    }

    def require(name, condition):
        result['checks'][name] = bool(condition)
        if not condition:
            raise ValueError(name)

    try:
        require('baseline_header_page_readable', baseline['image_bytes'] >= PAGE)
        require('baseline_header_id_matches_computed',
                baseline['header_id_matches'])
        require('baseline_kernel_padding_zero', baseline['kernel_padding_zero'])
        require('baseline_no_unexpected_trailing_data',
                baseline['trailing_bytes'] == 0)
        require('baseline_dtb_valid',
                baseline['dtb_magic_ok'] and baseline['dtb_total_size_ok'])

        require('header_page_excluding_id_matches_baseline',
                candidate['header_page_no_id_sha256']
                == baseline['header_page_no_id_sha256'])
        require('header_fields_match_baseline',
                candidate['header'] == baseline['header'])
        require('candidate_header_id_matches_computed',
                candidate['header_id_matches'])
        require('firmware_matches_baseline',
                candidate['firmware_sha256'] == baseline['firmware_sha256'])
        require('candidate_dtb_has_valid_magic', candidate['dtb_magic_ok'])
        require('candidate_dtb_total_size_matches', candidate['dtb_total_size_ok'])
        require('candidate_dtb_has_qcom_ids', candidate['dtb_has_qcom_ids'])
        require('boot_dtb_matches_baseline',
                candidate['dtb_sha256'] == baseline['dtb_sha256'])
        require('boot_kernel_section_matches_baseline',
                candidate['kernel_sha256'] == baseline['kernel_sha256'])
        require('ramdisk_size_matches_baseline',
                candidate['ramdisk_bytes'] == baseline['ramdisk_bytes'])
        require('kernel_padding_is_zero', candidate['kernel_padding_zero'])
        require('no_unexpected_trailing_data', candidate['trailing_bytes'] == 0)
        if expected_ramdisk_sha256 is not None:
            if not SHA256_RE.match(expected_ramdisk_sha256):
                raise SystemExit('--expected-ramdisk-sha256 must be 64 lowercase hex digits')
            require('ramdisk_matches_expected_payload',
                    candidate['ramdisk_sha256'] == expected_ramdisk_sha256)
        result['pass'] = True
    except ValueError as error:
        result['pass'] = False
        result['failed_check'] = str(error)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--expected-ramdisk-sha256')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    result = check(parse(args.baseline), parse(args.candidate),
                   args.expected_ramdisk_sha256)
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
