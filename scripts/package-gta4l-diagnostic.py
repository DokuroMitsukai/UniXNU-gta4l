#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Package a gta4l diagnostic boot image from the pinned working baseline.

This script does not read or modify board.toml and does not run the boot-image
patcher. It copies the hardware-tested v5 header page, boot kernel section
(firmware + BootShim + appended boot DTB) and padding byte-for-byte, then
replaces only the ramdisk with the named payload. The result must pass
scripts/check-gta4l-boot-image.py before it is published.

The baseline and payload hashes are explicit inputs. The defaults pin the
hardware-tested v5 image; pass different values only for a new documented
baseline.
"""
import argparse
import datetime
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

PAGE = 4096
MAGIC = b'ANDROID!'
HEADER_ID_OFFSET = 576
HEADER_ID_SIZE = 32
HEADER_NAMES = (
    'kernel_size', 'kernel_address', 'ramdisk_size', 'ramdisk_address',
    'second_size', 'second_address', 'tags_address', 'page_size',
    'header_version', 'os_version',
)

DEFAULT_BASELINE_SHA256 = (
    'ce4d76e8024a220890e7f32e2992a7beb094087910b070fc615c6fe1ee86dcde'
)
DEFAULT_BASELINE_BYTES = 85688320
DEFAULT_KERNEL_SHA256 = (
    'a4b054e04d506d118471495116e75ef234d1b45027e3cddcb87f97bc04c718b3'
)
DEFAULT_DTB_SHA256 = (
    'dc4c719d06e408654680524af26052d609baa14dd57da9082fd9f35c6509de9e'
)
DEFAULT_FIRMWARE_SHA256 = (
    '1e8a3132f63d82c96284d1b5d2d3bda5a6e4c44a8770fafdf9fb2abb5e1b073d'
)
DEFAULT_HEADER = {
    'kernel_size': 1798052,
    'kernel_address': 0x40008000,
    'ramdisk_size': 83886080,
    'ramdisk_address': 0x41000000,
    'second_size': 0,
    'second_address': 0,
    'tags_address': 0x40000100,
    'page_size': PAGE,
    'header_version': 1,
    'os_version': 0,
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def parse_uint(value):
    return int(value, 0)


def parse_header(data, path):
    if len(data) < PAGE or data[:8] != MAGIC:
        raise SystemExit(f'{path}: not an Android boot image')
    values = struct.unpack_from('<10I', data, 8)
    header = dict(zip(HEADER_NAMES, values))
    if header['page_size'] != PAGE:
        raise SystemExit(f'{path}: page size {header["page_size"]} is not {PAGE}')
    if header['header_version'] != 1:
        raise SystemExit(f'{path}: header version {header["header_version"]} is not 1')
    if header['second_size'] != 0:
        raise SystemExit(f'{path}: second stage is not supported')
    return header


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--payload', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--manifest')
    parser.add_argument('--baseline-sha256', default=DEFAULT_BASELINE_SHA256)
    parser.add_argument('--baseline-bytes', type=parse_uint,
                        default=DEFAULT_BASELINE_BYTES)
    parser.add_argument('--expected-kernel-sha256',
                        default=DEFAULT_KERNEL_SHA256)
    parser.add_argument('--expected-dtb-sha256', default=DEFAULT_DTB_SHA256)
    parser.add_argument('--expected-firmware-sha256',
                        default=DEFAULT_FIRMWARE_SHA256)
    parser.add_argument('--payload-sha256', required=True)
    parser.add_argument('--expect-diagnostic',
                        choices=('any', 'host-init', 'audit', 'snapshot',
                                 'emmc-probe-1', 'emmc-probe-2',
                                 'emmc-probe-3'),
                        default='any',
                        help='require this board diagnostic argument in the payload')
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--component-gate',
                        default=str(Path(__file__).with_name(
                            'check-gta4l-boot-image.py')))
    parser.add_argument('--payload-contract',
                        default=str(Path(__file__).with_name(
                            'check-gta4l-payload.py')))
    args = parser.parse_args()

    baseline_path = Path(args.baseline)
    payload_path = Path(args.payload)
    output_path = Path(args.output)
    manifest_path = Path(args.manifest) if args.manifest else Path(
        str(output_path) + '.manifest.json')

    if output_path.exists() and not args.force:
        raise SystemExit(f'{output_path}: output exists; pass --force to replace')
    if manifest_path.exists() and not args.force:
        raise SystemExit(f'{manifest_path}: manifest exists; pass --force to replace')

    baseline = baseline_path.read_bytes()
    if len(baseline) != args.baseline_bytes:
        raise SystemExit(
            f'{baseline_path}: expected {args.baseline_bytes} bytes, got {len(baseline)}')
    baseline_sha256 = sha(baseline)
    if baseline_sha256 != args.baseline_sha256:
        raise SystemExit(
            f'{baseline_path}: baseline SHA-256 mismatch\n'
            f'  expected {args.baseline_sha256}\n'
            f'  actual   {baseline_sha256}')

    header = parse_header(baseline, baseline_path)
    if header != DEFAULT_HEADER:
        raise SystemExit(
            f'{baseline_path}: unexpected header {header}; expected {DEFAULT_HEADER}')

    kernel_start = PAGE
    kernel_end = kernel_start + header['kernel_size']
    kernel_padded_end = PAGE * (1 + (header['kernel_size'] + PAGE - 1) // PAGE)
    ramdisk_start = kernel_padded_end
    ramdisk_end = ramdisk_start + header['ramdisk_size']
    if ramdisk_end != len(baseline):
        raise SystemExit(
            f'{baseline_path}: section boundary mismatch; ramdisk ends at '
            f'{ramdisk_end}, image is {len(baseline)} bytes')
    kernel = baseline[kernel_start:kernel_end]
    padding = baseline[kernel_end:kernel_padded_end]
    if any(padding):
        raise SystemExit(f'{baseline_path}: kernel padding is not zero')
    kernel_sha256 = sha(kernel)
    if kernel_sha256 != args.expected_kernel_sha256:
        raise SystemExit(
            f'{baseline_path}: kernel section SHA-256 mismatch\n'
            f'  expected {args.expected_kernel_sha256}\n'
            f'  actual   {kernel_sha256}')

    payload = payload_path.read_bytes()
    payload_sha256 = sha(payload)
    if payload_sha256 != args.payload_sha256:
        raise SystemExit(
            f'{payload_path}: payload SHA-256 mismatch\n'
            f'  expected {args.payload_sha256}\n'
            f'  actual   {payload_sha256}')
    if len(payload) != header['ramdisk_size']:
        raise SystemExit(
            f'{payload_path}: payload is {len(payload)} bytes; the pinned ramdisk '
            f'is {header["ramdisk_size"]} bytes')

    contract = subprocess.run(
        [sys.executable, args.payload_contract,
         '--payload', str(payload_path),
         '--expect-diagnostic', args.expect_diagnostic,
         '--json'],
        capture_output=True, text=True)
    if contract.returncode != 0:
        if contract.stdout:
            print(contract.stdout, file=sys.stderr)
        if contract.stderr:
            print(contract.stderr, file=sys.stderr)
        raise SystemExit('payload/ADT contract failed; output not published')
    contract_result = json.loads(contract.stdout)

    output = bytearray(baseline)
    output[ramdisk_start:ramdisk_end] = payload
    id_hash = hashlib.sha1()
    id_hash.update(kernel)
    id_hash.update(struct.pack('<I', len(kernel)))
    id_hash.update(payload)
    id_hash.update(struct.pack('<I', len(payload)))
    id_hash.update(struct.pack('<I', 0))
    id_hash.update(struct.pack('<I', 0))
    header_id = id_hash.digest().ljust(HEADER_ID_SIZE, b'\0')
    output[HEADER_ID_OFFSET:HEADER_ID_OFFSET + HEADER_ID_SIZE] = header_id
    if len(output) != len(baseline):
        raise SystemExit('internal error: output length changed')

    output_path.parent.mkdir(parents=True, exist_ok=True)
    temp = tempfile.NamedTemporaryFile(
        prefix=output_path.name + '.', suffix='.tmp',
        dir=output_path.parent, delete=False)
    temp_path = Path(temp.name)
    try:
        with temp:
            temp.write(output)
            temp.flush()
            os.fsync(temp.fileno())

        gate = subprocess.run(
            [sys.executable, args.component_gate,
             '--baseline', str(baseline_path),
             '--candidate', str(temp_path),
             '--expected-ramdisk-sha256', payload_sha256,
             '--json'],
            capture_output=True, text=True)
        if gate.returncode != 0:
            if gate.stdout:
                print(gate.stdout, file=sys.stderr)
            if gate.stderr:
                print(gate.stderr, file=sys.stderr)
            raise SystemExit('component gate failed; output not published')
        gate_result = json.loads(gate.stdout)

        output_sha256 = sha(output)
        if args.force and output_path.exists():
            output_path.unlink()
        os.replace(temp_path, output_path)
    except BaseException:
        temp_path.unlink(missing_ok=True)
        raise

    baseline_info = gate_result['baseline']
    candidate_info = gate_result['candidate']
    manifest = {
        'schema_version': 1,
        'created_utc': datetime.datetime.now(
            datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
        'baseline': {
            'path': str(baseline_path),
            'bytes': len(baseline),
            'sha256': baseline_sha256,
        },
        'payload': {
            'path': str(payload_path),
            'bytes': len(payload),
            'sha256': payload_sha256,
            'expected_diagnostic': args.expect_diagnostic,
        },
        'payload_contract': {
            'script': args.payload_contract,
            'pass': contract_result['pass'],
            'checks': contract_result['checks'],
        },
        'output': {
            'path': str(output_path),
            'bytes': len(output),
            'sha256': output_sha256,
        },
        'header': header,
        'component_gate': {
            'script': args.component_gate,
            'pass': gate_result['pass'],
            'checks': gate_result['checks'],
        },
        'baseline_kernel_sha256': kernel_sha256,
        'candidate_kernel_sha256': candidate_info['kernel_sha256'],
        'firmware_sha256': candidate_info['firmware_sha256'],
        'dtb_sha256': candidate_info['dtb_sha256'],
        'dtb_bytes': candidate_info['dtb_bytes'],
        'ramdisk_sha256': candidate_info['ramdisk_sha256'],
        'ramdisk_bytes': candidate_info['ramdisk_bytes'],
        'header_id': header_id.hex(),
        'only_ramdisk_differs': (
            baseline_info['header_page_no_id_sha256']
            == candidate_info['header_page_no_id_sha256']
            and baseline_info['kernel_sha256'] == candidate_info['kernel_sha256']
            and baseline_info['firmware_sha256'] == candidate_info['firmware_sha256']
            and baseline_info['dtb_sha256'] == candidate_info['dtb_sha256']
        ),
        'command': sys.argv,
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'wrote {output_path} ({len(output)} bytes, {output_sha256})')
    print(f'wrote {manifest_path}')


if __name__ == '__main__':
    main()
