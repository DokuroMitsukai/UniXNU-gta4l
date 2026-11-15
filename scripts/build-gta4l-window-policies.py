#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Sign the gta4l OS and SystemOS windows for VerifiedRoot.

The windows are read from the SHNGROOT userdata image. Each window must
match its pinned SHA-256 digest; the owner key signs one policy per window
(160-byte header, 384-byte signature, ordered 4096-byte block hashes). The
envelopes and every block are then verified with the kit's actual C parser,
and the script writes the table objects for the version-2 userdata manifest
and the C header the block driver embeds.

The private key never leaves the key directory; only the public anchor and
the signed envelopes are written here. Host-only: no device access.
"""
import argparse
import ctypes
import hashlib
import importlib.util
import io
import json
import os
import shutil
import struct
import subprocess
import tempfile
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_UNIXNU = ROOT.parent / 'UniXNU'
DEFAULT_KEY_DIR = Path.home() / '.local' / 'share' / 'gta4l-owner-key'
OUT = ROOT / 'out' / 'gta4l' / 's5-policies'
HEADER_BYTES = 4096
BLOCK_BYTES = 4096
VR_HEADER = 160
VR_SIGNATURE = 384
VR_KEY = 395
GENERATION = 1
WINDOWS = (
    ('OS', uuid.UUID('04769D26-2CA0-4825-B443-E2E5F88300F2'),
     'e5634d04a8ac35f024845f583394e0a35deb26a04cb0c00286a8674e25a9a4f2'),
    ('SystemOS', uuid.UUID('8846FE78-2910-4297-AF97-D59BB6F8FC16'),
     'b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f'),
)
REPORT = OUT / 'policies.json'
HEADER = OUT / 'Gta4lEmmcBlockPolicies.h'
OBJECTS = OUT / 'objects'


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def read_manifest(image):
    with image.open('rb') as stream:
        header = stream.read(HEADER_BYTES)
        if len(header) != HEADER_BYTES or header[0:8] != b'SHNGROOT':
            raise SystemExit(f'{image}: not a SHNGROOT image')
        version, count = struct.unpack_from('<II', header, 8)
        image_bytes = struct.unpack_from('<Q', header, 16)[0]
        if version not in (1, 2) or not count or count > 8:
            raise SystemExit(f'{image}: unsupported manifest version/count {version}/{count}')
        if header[4064:4096] != hashlib.sha256(header[:4064]).digest():
            raise SystemExit(f'{image}: manifest digest mismatch')
        if image.stat().st_size != image_bytes:
            raise SystemExit(f'{image}: manifest image_bytes does not match the file')
        if image_bytes % HEADER_BYTES:
            raise SystemExit(f'{image}: image_bytes is not 4096 aligned')
        windows = []
        for index in range(count):
            entry = header[24 + 128 * index:24 + 128 * (index + 1)]
            name = entry[:32].split(b'\0', 1)[0].decode('ascii')
            offset, size, block_size, flags = struct.unpack_from('<QQII', entry, 32)
            if offset < HEADER_BYTES or offset % HEADER_BYTES or not size or \
                    offset + size > image_bytes or (size + BLOCK_BYTES - 1) // BLOCK_BYTES * BLOCK_BYTES > image_bytes - offset:
                raise SystemExit(f'{image}: window {name} is outside the image')
            windows.append({'name': name, 'offset': offset, 'size': size,
                            'block_size': block_size, 'flags': flags,
                            'sha256': entry[56:88].hex()})
    return windows


class WindowStream:
    """Bounded read-only window view of the userdata image."""

    def __init__(self, path, offset, size):
        self.path, self.offset, self.size = path, offset, size
        self.done = 0
        self.handle = path.open('rb')
        self.handle.seek(offset)

    def read(self, n):
        remaining = self.size - self.done
        if n > remaining:
            n = remaining
        data = self.handle.read(n) if n else b''
        self.done += len(data)
        return data

    def close(self):
        self.handle.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False


def require_key(key_dir, generate):
    key_dir = key_dir.resolve()
    private, public = key_dir / 'owner.pem', key_dir / 'owner-public.pem'
    if not private.is_file():
        if not generate:
            raise SystemExit(f'{private}: owner key missing; pass --generate-key once')
        key_dir.mkdir(parents=True, exist_ok=True)
        os.chmod(key_dir, 0o700)
        fd = os.open(private, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        os.close(fd)
        subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt',
                        'rsa_keygen_bits:3072', '-out', str(private)],
                       check=True, capture_output=True)
    os.chmod(private, 0o600)
    if not public.is_file():
        subprocess.run(['openssl', 'pkey', '-in', str(private), '-pubout',
                        '-out', str(public)], check=True, capture_output=True)
    return private, public


def load_manifest_tool(unixnu):
    path = unixnu / 'kits' / 'VerifiedRoot' / 'tools' / 'manifest.py'
    spec = importlib.util.spec_from_file_location('vroot_manifest', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def host_library(unixnu, work):
    kit = unixnu / 'kits' / 'VerifiedRoot'
    work.mkdir(parents=True, exist_ok=True)
    library = work / 'libverifiedroot-host.so'
    sources = [kit / 'src' / 'verified_root.c', kit / 'tests' / 'openssl_provider.c']
    if not library.is_file() or any(library.stat().st_mtime < source.stat().st_mtime for source in sources):
        subprocess.run(['gcc', '-std=c11', '-O2', '-fPIC', '-shared', '-Wall', '-Wextra',
                        '-Werror', '-I', str(kit / 'include'), *map(str, sources),
                        '-lcrypto', '-o', str(library)], check=True)
    native = ctypes.CDLL(str(library))

    class View(ctypes.Structure):
        _fields_ = [('hashes', ctypes.c_void_p), ('image_bytes', ctypes.c_uint64),
                    ('block_count', ctypes.c_uint64), ('generation', ctypes.c_uint64),
                    ('volume_uuid', ctypes.c_uint8 * 16), ('image_sha256', ctypes.c_uint8 * 32)]

    native.vr_host_open.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                    ctypes.c_uint64, ctypes.POINTER(View)]
    native.vr_host_open.restype = ctypes.c_int
    native.vr_host_block.argtypes = [ctypes.POINTER(View), ctypes.c_uint64,
                                     ctypes.c_void_p, ctypes.c_size_t]
    native.vr_host_block.restype = ctypes.c_int
    return native, View


def open_policy(native, View, envelope, anchor, generation):
    encoded = ctypes.create_string_buffer(envelope)
    trusted = ctypes.create_string_buffer(anchor)
    view = View()
    status = native.vr_host_open(encoded, len(envelope), trusted, generation, ctypes.byref(view))
    return status, view, (encoded, trusted)


def verify_window(native, View, envelope, anchor, window, generation, expected_bytes,
                  expected_uuid):
    status, view, keep = open_policy(native, View, envelope, anchor, generation)
    if status != 0 or view.image_bytes != expected_bytes or \
            view.block_count != expected_bytes // BLOCK_BYTES + (expected_bytes % BLOCK_BYTES != 0) or \
            bytes(view.volume_uuid) != expected_uuid.bytes or view.generation != generation:
        raise SystemExit(f'policy open failed: status={status} bytes={view.image_bytes} '
                         f'blocks={view.block_count} uuid mismatch')
    with WindowStream(window['image'], window['offset'], window['size']) as stream:
        for index in range(view.block_count):
            data = stream.read(BLOCK_BYTES)
            if native.vr_host_block(ctypes.byref(view), index, data, len(data)) != 0:
                raise SystemExit(f'{window["name"]}: block {index} does not verify')
        if stream.read(1):
            raise SystemExit(f'{window["name"]}: trailing data after the last block')
    return view, keep


def c_header(anchor, policies):
    def array(name, data):
        lines = [f'static const uint8_t {name}[{len(data)}] = {{']
        for index in range(0, len(data), 12):
            lines.append('    ' + ', '.join(f'0x{byte:02x}' for byte in data[index:index + 12]) + ',')
        lines.append('};')
        return lines

    lines = ['/* Generated by scripts/build-gta4l-window-policies.py; do not edit. */',
             '#ifndef GTA4L_EMMC_BLOCK_POLICIES_H', '#define GTA4L_EMMC_BLOCK_POLICIES_H',
             '#include <stdint.h>', '', f'#define GTA4L_EMMC_BLOCK_POLICY_GENERATION {GENERATION}ULL', '']
    lines += array('gta4l_owner_anchor', anchor) + ['']
    for label, header, signature in policies:
        lines += array(f'gta4l_{label.lower()}_policy_header', header) + ['']
        lines += array(f'gta4l_{label.lower()}_policy_signature', signature) + ['']
    lines += ['#endif', '']
    return '\n'.join(lines)


def key_fingerprint(anchor):
    return hashlib.sha256(anchor).hexdigest()


def sign(image, key_dir, unixnu, output, generation, generate, self_test=False):
    private, public = require_key(key_dir, generate)
    manifest_tool = load_manifest_tool(unixnu)
    anchor = manifest_tool.public_anchor(public)
    if len(anchor) != VR_KEY:
        raise SystemExit('the public anchor is not 395 bytes')
    windows = read_manifest(image)
    by_name = {window['name']: window for window in windows}
    native, View = host_library(unixnu, output / 'host')
    output.mkdir(parents=True, exist_ok=True)
    OBJECTS.mkdir(parents=True, exist_ok=True)
    policies, report = [], {
        'image': str(image), 'image_sha256': file_sha256(image),
        'generation': generation, 'trusted_key_sha256': key_fingerprint(anchor),
        'windows': [],
    }
    for label, expected_uuid, expected_digest in WINDOWS:
        window = by_name.get(label)
        if window is None:
            raise SystemExit(f'{image}: no {label} window')
        window['image'] = image
        if window['block_size'] != 512 or window['flags'] != 1:
            raise SystemExit(f'{label}: not a read-only 512-byte block window')
        if window['sha256'] != expected_digest:
            raise SystemExit(f'{label}: window digest {window["sha256"]} != {expected_digest}')
        if window['offset'] % HEADER_BYTES or window['size'] % 512:
            raise SystemExit(f'{label}: unexpected geometry')
        out = output / label
        out.mkdir(parents=True, exist_ok=True)
        with WindowStream(image, window['offset'], window['size']) as stream:
            record = manifest_tool.make(stream, window['size'], private, public,
                                        expected_uuid, generation, out)
        envelope = (out / 'manifest.bin').read_bytes()
        table = (out / 'hashes.bin').read_bytes()
        if len(envelope) != VR_HEADER + VR_SIGNATURE + len(table):
            raise SystemExit(f'{label}: unexpected envelope size')
        view, keep = verify_window(native, View, envelope, anchor, window, generation,
                                   window['size'], expected_uuid)
        object_path = OBJECTS / f'{label}.vroot'
        shutil.copyfile(out / 'hashes.bin', object_path)
        if sha256(object_path.read_bytes()) != record['hash_table_sha256']:
            raise SystemExit(f'{label}: object copy differs from the signed table')
        header = envelope[:VR_HEADER]
        signature = envelope[VR_HEADER:VR_HEADER + VR_SIGNATURE]
        policies.append((label, header, signature))
        report['windows'].append({
            'name': label, 'uuid': str(expected_uuid).upper(), 'bytes': window['size'],
            'blocks': view.block_count, 'window_sha256': window['sha256'],
            'image_sha256': record['image_sha256'], 'table_sha256': record['hash_table_sha256'],
            'header_sha256': sha256(header), 'signature_sha256': sha256(signature),
            'object': str(object_path.relative_to(ROOT)),
            'object_sha256': sha256(table),
        })
    report['objects'] = [{'name': f'{label}.vroot', 'path': entry['object'],
                          'size': (OBJECTS / f'{label}.vroot').stat().st_size,
                          'sha256': entry['object_sha256']}
                         for label, entry in zip((w[0] for w in WINDOWS), report['windows'])]
    HEADER.write_text(c_header(anchor, policies))
    report['header'] = str(HEADER.relative_to(ROOT))
    report['header_sha256'] = sha256(HEADER.read_bytes())
    REPORT.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: signed {len(report["windows"])} window policies with generation {generation}')
    for entry in report['windows']:
        print(f'  {entry["name"]}: {entry["bytes"]} bytes, {entry["blocks"]} blocks, '
              f'table {entry["table_sha256"][:16]} object {entry["object"]}')
    print(f'  anchor {report["trusted_key_sha256"]}')
    print(f'  wrote {HEADER} and {REPORT}')
    return report


def self_test(work):
    """Sign and verify a synthetic image, then refuse each mutation."""
    work.mkdir(parents=True, exist_ok=True)
    keys = work / 'keys'
    keys.mkdir(parents=True, exist_ok=True)
    private, public = require_key(keys, True)
    manifest_tool = load_manifest_tool(DEFAULT_UNIXNU)
    native, View = host_library(DEFAULT_UNIXNU, work / 'host')
    anchor = manifest_tool.public_anchor(public)
    data = bytes((index * 31 + index // 4096) & 255 for index in range(BLOCK_BYTES * 3 + 512))
    window_uuid = WINDOWS[0][1]
    record = manifest_tool.make(io.BytesIO(data), len(data), private, public,
                                window_uuid, GENERATION, work / 'signed')
    envelope = (work / 'signed' / 'manifest.bin').read_bytes()
    status, view, _ = open_policy(native, View, envelope, anchor, GENERATION)
    assert status == 0 and view.block_count == 4 and view.image_bytes == len(data)
    for index in range(4):
        block = data[index * BLOCK_BYTES:(index + 1) * BLOCK_BYTES]
        assert native.vr_host_block(ctypes.byref(view), index, block, len(block)) == 0
    wrong_key = manifest_tool.public_anchor(require_key(work / 'other', True)[1])
    assert open_policy(native, View, envelope, wrong_key, GENERATION)[0] == 2
    changed = bytearray(envelope)
    changed[VR_HEADER + 10] ^= 1
    assert open_policy(native, View, bytes(changed), anchor, GENERATION)[0] == 3
    changed = bytearray(envelope)
    changed[VR_HEADER + VR_SIGNATURE + 5] ^= 1
    assert open_policy(native, View, bytes(changed), anchor, GENERATION)[0] == 4
    assert open_policy(native, View, envelope, anchor, GENERATION + 1)[0] == 1
    block = bytearray(data[:BLOCK_BYTES])
    block[0] ^= 1
    assert native.vr_host_block(ctypes.byref(view), 0, bytes(block), BLOCK_BYTES) == 5
    print('PASS: policy self-test (open, blocks, wrong key, signature, table, rollback, mutation)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path)
    parser.add_argument('--key-dir', type=Path, default=DEFAULT_KEY_DIR)
    parser.add_argument('--unixnu', type=Path, default=DEFAULT_UNIXNU)
    parser.add_argument('--output', type=Path, default=OUT)
    parser.add_argument('--generation', type=int, default=GENERATION)
    parser.add_argument('--generate-key', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        with tempfile.TemporaryDirectory(prefix='gta4l-policy-selftest-') as temp:
            self_test(Path(temp))
        return 0
    if args.image is None:
        parser.error('--image is required (or pass --self-test)')
    if args.output != OUT:
        global OBJECTS, HEADER, REPORT
        OBJECTS = args.output / 'objects'
        HEADER = args.output / 'Gta4lEmmcBlockPolicies.h'
        REPORT = args.output / 'policies.json'
    sign(args.image, args.key_dir, args.unixnu, args.output, args.generation,
         args.generate_key)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
