#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Build small throwaway VerifiedRoot fixtures for the host adapter tests.

The production policy header is signed by the owner key and covers the real
6 GiB OS and 3 GiB SystemOS windows. The host tests only need the same code
paths with small windows, so this script generates a synthetic SHNGROOT
image, signs it with a throwaway key under the output directory and writes
the matching policy header. The owner key is never read or written here.

Host-only: no device access, no flash and no owner-key use.
"""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_UNIXNU = ROOT.parent / 'UniXNU'
HEADER_BYTES = 4096
BLOCK_BYTES = 512
VR_BLOCK_BYTES = 4096
VR_HEADER = 160
VR_SIGNATURE = 384
GENERATION = 1
OS_UUID = uuid.UUID('04769D26-2CA0-4825-B443-E2E5F88300F2')
SYSTEMOS_UUID = uuid.UUID('8846FE78-2910-4297-AF97-D59BB6F8FC16')
OS_SIZE = 2 * 1024 * 1024 + BLOCK_BYTES
SYSTEMOS_SIZE = 1024 * 1024 + BLOCK_BYTES


def align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def load_manifest_tool(unixnu):
    path = unixnu / 'kits' / 'VerifiedRoot' / 'tools' / 'manifest.py'
    if not path.is_file():
        raise SystemExit(f'{path}: VerifiedRoot manifest tool missing')
    spec = importlib.util.spec_from_file_location('vroot_manifest', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_key(directory):
    directory.mkdir(parents=True, exist_ok=True)
    os.chmod(directory, 0o700)
    private = directory / 'owner.pem'
    public = directory / 'owner-public.pem'
    if not private.is_file():
        fd = os.open(private, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        os.close(fd)
        subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA',
                        '-pkeyopt', 'rsa_keygen_bits:3072', '-out',
                        str(private)], check=True, capture_output=True)
    os.chmod(private, 0o600)
    if not public.is_file():
        subprocess.run(['openssl', 'pkey', '-in', str(private), '-pubout',
                        '-out', str(public)], check=True,
                       capture_output=True)
    return private, public


def pattern(size, seed):
    return bytes((index * 37 + index // VR_BLOCK_BYTES + seed) & 0xff
                 for index in range(size))


def put_u32(target, offset, value):
    target[offset:offset + 4] = value.to_bytes(4, 'little')


def put_u64(target, offset, value):
    target[offset:offset + 8] = value.to_bytes(8, 'little')


def entry(target, index, name, offset, size, block_size, flags, digest):
    start = 24 + 128 * index
    encoded = name.encode('ascii')
    target[start:start + len(encoded)] = encoded
    put_u64(target, start + 32, offset)
    put_u64(target, start + 40, size)
    put_u32(target, start + 48, block_size)
    put_u32(target, start + 52, flags)
    target[start + 56:start + 88] = digest


def c_array(name, data):
    lines = [f'static uint8_t {name}[{len(data)}] = {{']
    for index in range(0, len(data), 12):
        lines.append('    ' + ', '.join(
            f'0x{byte:02x}' for byte in data[index:index + 12]) + ',')
    lines.append('};')
    return lines


def build(output):
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    private, public = make_key(output / 'keys')
    manifest = load_manifest_tool(DEFAULT_UNIXNU)
    anchor = manifest.public_anchor(public)

    os_data = pattern(OS_SIZE, 0)
    systemos_data = pattern(SYSTEMOS_SIZE, 91)
    windows = [
        ('OS', OS_UUID, os_data, 1 << 20),
        ('SystemOS', SYSTEMOS_UUID, systemos_data, None),
    ]
    signed = {}
    for name, volume_uuid, data, offset in windows:
        directory = output / 'signed' / name
        record = manifest.make(io.BytesIO(data), len(data), private,
                               public, volume_uuid, GENERATION, directory)
        envelope = (directory / 'manifest.bin').read_bytes()
        table = (directory / 'hashes.bin').read_bytes()
        if len(envelope) != VR_HEADER + VR_SIGNATURE + len(table):
            raise SystemExit(f'{name}: unexpected envelope size')
        signed[name] = {
            'uuid': volume_uuid,
            'data': data,
            'table': table,
            'header': envelope[:VR_HEADER],
            'signature': envelope[VR_HEADER:VR_HEADER + VR_SIGNATURE],
            'record': record,
            'offset': offset,
        }

    os_offset = signed['OS']['offset']
    systemos_offset = align_up(os_offset + OS_SIZE, 1 << 20)
    signed['SystemOS']['offset'] = systemos_offset
    os_object_offset = align_up(systemos_offset + SYSTEMOS_SIZE, HEADER_BYTES)
    systemos_object_offset = align_up(
        os_object_offset + len(signed['OS']['table']), HEADER_BYTES)
    image_bytes = align_up(
        systemos_object_offset + len(signed['SystemOS']['table']),
        HEADER_BYTES)

    image = bytearray(image_bytes)
    image[os_offset:os_offset + OS_SIZE] = os_data
    image[systemos_offset:systemos_offset + SYSTEMOS_SIZE] = systemos_data
    image[os_object_offset:os_object_offset + len(signed['OS']['table'])] = \
        signed['OS']['table']
    image[systemos_object_offset:
          systemos_object_offset + len(signed['SystemOS']['table'])] = \
        signed['SystemOS']['table']

    header = bytearray(HEADER_BYTES)
    header[0:8] = b'SHNGROOT'
    put_u32(header, 8, 2)
    put_u32(header, 12, 4)
    put_u64(header, 16, image_bytes)
    entry(header, 0, 'OS', os_offset, OS_SIZE, BLOCK_BYTES, 1,
          bytes.fromhex(signed['OS']['record']['image_sha256']))
    entry(header, 1, 'SystemOS', systemos_offset, SYSTEMOS_SIZE, BLOCK_BYTES,
          1, bytes.fromhex(signed['SystemOS']['record']['image_sha256']))
    entry(header, 2, 'OS.vroot', os_object_offset,
          len(signed['OS']['table']), VR_BLOCK_BYTES, 3,
          bytes.fromhex(signed['OS']['record']['hash_table_sha256']))
    entry(header, 3, 'SystemOS.vroot', systemos_object_offset,
          len(signed['SystemOS']['table']), VR_BLOCK_BYTES, 3,
          bytes.fromhex(signed['SystemOS']['record']['hash_table_sha256']))
    header[4064:4096] = hashlib.sha256(header[:4064]).digest()
    image[0:HEADER_BYTES] = header
    image_path = output / 'userdata.img'
    image_path.write_bytes(image)

    policy_dir = output / 'policies'
    policy_dir.mkdir(parents=True, exist_ok=True)
    header_lines = [
        '/* Generated by scripts/build-gta4l-verify-fixtures.py; '
        'host tests only. */',
        '#ifndef GTA4L_EMMC_BLOCK_POLICIES_H',
        '#define GTA4L_EMMC_BLOCK_POLICIES_H',
        '#include <stdint.h>',
        '',
        f'#define GTA4L_EMMC_BLOCK_POLICY_GENERATION {GENERATION}ULL',
        '',
    ]
    header_lines += c_array('gta4l_owner_anchor', anchor) + ['']
    for name in ('OS', 'SystemOS'):
        label = name.lower()
        header_lines += c_array(
            f'gta4l_{label}_policy_header', signed[name]['header']) + ['']
        header_lines += c_array(
            f'gta4l_{label}_policy_signature', signed[name]['signature']) + ['']
    header_lines += ['#endif', '']
    policy_header = policy_dir / 'Gta4lEmmcBlockPolicies.h'
    policy_header.write_text('\n'.join(header_lines))

    fixture = {
        'image': str(image_path),
        'image_bytes': image_bytes,
        'image_sha256': sha256(image),
        'generation': GENERATION,
        'anchor_sha256': sha256(anchor),
        'policy_header': str(policy_header),
        'windows': {
            name: {
                'offset': signed[name]['offset'],
                'size': len(signed[name]['data']),
                'table_bytes': len(signed[name]['table']),
                'table_offset': (os_object_offset if name == 'OS'
                                 else systemos_object_offset),
                'image_sha256':
                    signed[name]['record']['image_sha256'],
                'table_sha256':
                    signed[name]['record']['hash_table_sha256'],
            }
            for name in ('OS', 'SystemOS')
        },
    }
    (output / 'fixture.json').write_text(json.dumps(fixture, indent=2) + '\n')
    print(f'PASS: wrote {output} ({image_bytes} bytes)')
    return fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
