#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Check a gta4l S4 WINDOWS line against the userdata root image.

The S4 rung prints the manifest verdict, image size and window list, the
first/last 1 MiB digest prefixes of each window, the APFS container UUID
and both window volume UUIDs (OS `uuid`, SystemOS `suuid`). This host-only
checker recomputes the manifest digest and every window digest from the
flashed image, parses the first window's GPT and APFS container UUID, and
requires the publication verdict (pub=2 phase=5 perr=0). A pre-write
report must instead show valid=0 reason=2 with no window fields. A
report that carries a VERIFY line is checked against the enrolled policy
envelopes: valid=1 reason=0 with 1,025 blocks, dma=1, the recomputed
SHA-384 prefixes and ms inside the 12,000 ms budget; --expect-verify
makes the line mandatory. It never reads a device.
"""
import argparse
import hashlib
import re
import struct
import tempfile
from pathlib import Path

LABEL = 'GTA4L-EMMC P5 WINDOWS: '
VERIFY_LABEL = 'GTA4L-EMMC P5 VERIFY: '
VREAD_LABEL = 'GTA4L-EMMC P5 VREAD: '
HEADER_BYTES = 4096
EDGE_BYTES = 1 << 20
NAME_BYTES = 32
ENTRY_BYTES = 128
ENTRY_BASE = 24
DIGEST_BASE = 4064
ALIGN_BYTES = 1 << 20
OBJECT_ALIGN_BYTES = 1 << 12
BLOCK_BYTES = 512
APFS_TYPE = bytes.fromhex('ef57347c0000aa11aa1100306543ecac')
HEX32 = re.compile(r'^[0-9a-f]{32}$')
UUID_TEXT = re.compile(
    r'^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$')
VOLUME_UUID = '04769D26-2CA0-4825-B443-E2E5F88300F2'
SYSTEM_VOLUME_UUID = '8846FE78-2910-4297-AF97-D59BB6F8FC16'
# Whole-media wait plus userdata wait from the driver's discovery budget.
DISCOVERY_BUDGET_MS = 8000 + 2000
VALID_FIELDS = (
    'valid', 'reason', 'count', 'image', 'pub', 'phase', 'perr', 'mdig',
    'pmc', 'pwait', 'rms', 'rch', 'dma', 'wfail', 'wdiag', 'wpoll',
    'wtime')
# Four 1 MiB edges and one APFS chunk, 32 KiB each, for a two-window
# image; allow up to four windows.
MAX_READ_CHUNKS = 2 * 4 * (1 << 20) // (32 << 10) + 1
WINDOW_FIELDS = ('off', 'size', 'hd0', 'hd1')
VERIFY_FIELDS = ('valid', 'reason', 'os', 'sysos', 'blocks', 'ms',
                 'fail', 'dma', 'osha', 'ssha')
VERIFY_FAIL_FIELDS = ('vobj', 'voff', 'vlen', 'vio', 'vact')
VERIFY_BLOCKS = 1025
VERIFY_BUDGET_MS = 12000
VREAD_CASES = 8
VREAD_SEQ_BYTES = 4 << 20
VREAD_SEQ_BUDGET_MS = 30000
VREAD_FIELDS = ('cases', 'passed', 'failed', 'reason', 'mask',
                'windows', 'msize', 'mwhole', 'vst', 'spans', 'mismatches',
                'write', 'exports',
                'seq_ms', 'seq_bytes', 'failcase', 'failstatus',
                'failblock', 'failactual', 'dig', 'sdig')


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def u64(data, offset):
    return struct.unpack_from('<Q', data, offset)[0]


def uuid_bytes(text):
    return bytes.fromhex(text.replace('-', '').lower())


def digest_prefix(data):
    return hashlib.sha256(data).hexdigest()[:32]


def parse_line(line):
    """Return (fields, windows) or raise ValueError.

    `windows` is a list of dicts in w0..wN order, each with name/off/
    size/hd0/hd1. Fields before the first wN are the manifest fields;
    apfs/cid/uuid/suuid follow the last window.
    """
    if not line.startswith(LABEL):
        raise ValueError('missing WINDOWS label')
    tokens = line[len(LABEL):].split()
    fields = {}
    windows = {}
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if '=' not in token:
            raise ValueError(f'malformed token {token!r}')
        key, value = token.split('=', 1)
        if key in ('apfs', 'cid', 'uuid', 'suuid') or key in VALID_FIELDS:
            if key in fields:
                raise ValueError(f'duplicate field {key}')
            fields[key] = value
            index += 1
            continue
        if key.startswith('w') and key[1:].isdigit():
            slot = int(key[1:])
            if slot in windows:
                raise ValueError(f'duplicate window {slot}')
            if index + len(WINDOW_FIELDS) >= len(tokens):
                raise ValueError(f'window {slot} is short')
            window = {'name': value}
            for name in WINDOW_FIELDS:
                item = tokens[index + 1 + WINDOW_FIELDS.index(name)]
                if not item.startswith(name + '='):
                    raise ValueError(
                        f'window {slot} expected {name}, got {item}')
                window[name] = item.split('=', 1)[1]
            windows[slot] = window
            index += 1 + len(WINDOW_FIELDS)
            continue
        raise ValueError(f'unknown field {key}')
    ordered = [windows[slot] for slot in sorted(windows)]
    return fields, ordered


def check_pre_write(line):
    errors = []
    try:
        fields, windows = parse_line(line)
    except ValueError as error:
        return [str(error)]
    if windows:
        errors.append('pre-write report carries window fields')
    expected = {
        'valid': '0', 'reason': '2', 'count': '0', 'image': '0'}
    for key, value in expected.items():
        if fields.get(key) != value:
            errors.append(f'pre-write {key}={fields.get(key)!r}, '
                          f'expected {value!r}')
    wanted = set(expected) | {'pmc', 'pwait', 'rms', 'rch'}
    if set(fields) != wanted:
        errors.append(f'pre-write fields {sorted(fields)!r}, '
                      f'expected {sorted(wanted)!r}')
    if fields.get('pmc') != '1':
        errors.append(f"pre-write pmc={fields.get('pmc')!r}, "
                      f"expected '1' (userdata found once)")
    add_wait_check(fields, errors, 'pre-write')
    if fields.get('rms') != '0' or fields.get('rch') != '0':
        errors.append('pre-write read stats '
                      f"rms={fields.get('rms')!r} "
                      f"rch={fields.get('rch')!r}, expected 0/0")
    return errors


def add_wait_check(fields, errors, kind):
    """Require a numeric pwait inside the driver's discovery budget."""
    value = fields.get('pwait')
    if value is None or not value.isdigit():
        errors.append(f'{kind} pwait={value!r}, expected milliseconds')
        return
    if int(value) > DISCOVERY_BUDGET_MS:
        errors.append(f'{kind} pwait={value}, above the '
                      f'{DISCOVERY_BUDGET_MS} ms discovery budget')


def add_read_stats(fields, errors):
    """Require a bounded window-phase read time and chunk count."""
    value = fields.get('rms')
    if value is None or not value.isdigit():
        errors.append(f'post-write rms={value!r}, expected milliseconds')
    elif int(value) > 4000:
        errors.append(f'post-write rms={value}, above the 4000 ms '
                      f'read budget')
    chunks = fields.get('rch')
    if chunks is None or not chunks.isdigit():
        errors.append(f'post-write rch={chunks!r}, expected a count')
    elif not 1 <= int(chunks) <= MAX_READ_CHUNKS:
        errors.append(f'post-write rch={chunks}, expected 1..'
                      f'{MAX_READ_CHUNKS}')


ACCEPT_DIAG = 0x1 | 0x2 | 0x8
KNOWN_DIAG = ACCEPT_DIAG | 0x4


def add_publication_diag(fields, errors):
    """Require success-stage media, UUID and accept bits; APFS is optional."""
    fail = fields.get('wfail')
    if fail != '4294967295':
        errors.append(f'post-write wfail={fail!r}, expected 4294967295 '
                      f'(no failing window)')
    diag = fields.get('wdiag')
    if diag is None or not diag.isdigit():
        errors.append(f'post-write wdiag={diag!r}, expected a bit mask')
    elif (int(diag) & ACCEPT_DIAG) != ACCEPT_DIAG:
        errors.append(f'post-write wdiag={diag}, expected the media, UUID '
                      f'and accept bits (0x{ACCEPT_DIAG:x})')
    elif (int(diag) & ~KNOWN_DIAG) != 0:
        errors.append(f'post-write wdiag={diag}, unexpected diagnostic bits')
    polls = fields.get('wpoll')
    if polls is None or not polls.isdigit():
        errors.append(f'post-write wpoll={polls!r}, expected a count')
    elif not 1 <= int(polls) <= 21:
        errors.append(f'post-write wpoll={polls}, expected 1..21')
    wait = fields.get('wtime')
    if wait is None or not wait.isdigit():
        errors.append(f'post-write wtime={wait!r}, expected milliseconds')
    elif int(wait) > 2000:
        errors.append(f'post-write wtime={wait}, above the 2000 ms '
                      f'window-media budget')


def check(line, expected):
    """Check a post-write WINDOWS line against the expected dict."""
    errors = []
    try:
        fields, windows = parse_line(line)
    except ValueError as error:
        return [str(error)]
    required = set(VALID_FIELDS) | {'apfs', 'cid', 'uuid', 'suuid'}
    if 'dma' in fields:
        required.add('dma')
    else:
        required.discard('dma')
    if set(fields) != required:
        errors.append(f'fields {sorted(fields)!r} do not match '
                      f'{sorted(required)!r}')
    if ('dma' in fields) and (fields['dma'] not in ('0', '1')):
        errors.append(f"dma={fields['dma']!r}, expected 0 or 1")
    fixed = {
        'valid': '1',
        'reason': '0',
        'count': str(expected['count']),
        'image': str(expected['image_bytes']),
        'pub': '2',
        'phase': '5',
        'perr': '0',
        'mdig': expected['manifest'],
        'apfs': '1',
        'cid': expected['container'],
        'uuid': expected['volume'],
        'suuid': expected['system_volume'],
    }
    for key, value in fixed.items():
        if fields.get(key) != value:
            errors.append(f'{key}={fields.get(key)!r}, expected {value!r}')
    if fields.get('pmc') != '1':
        errors.append(f"pmc={fields.get('pmc')!r}, expected '1'")
    add_wait_check(fields, errors, 'post-write')
    add_read_stats(fields, errors)
    add_publication_diag(fields, errors)
    if len(windows) != expected['count']:
        errors.append(f'{len(windows)} window blocks, '
                      f'expected {expected["count"]}')
    for index, wanted in enumerate(expected['windows']):
        got = windows[index] if index < len(windows) else {}
        for key in ('name', 'off', 'size', 'hd0', 'hd1'):
            value = wanted[key] if key != 'off' and key != 'size' else \
                str(wanted[key])
            if got.get(key) != value:
                errors.append(f'w{index}.{key}={got.get(key)!r}, '
                              f'expected {value!r}')
    return errors


def parse_verify(line):
    """Return the VERIFY fields or raise ValueError."""
    if not line.startswith(VERIFY_LABEL):
        raise ValueError('missing VERIFY label')
    fields = {}
    allowed = set(VERIFY_FIELDS) | set(VERIFY_FAIL_FIELDS)
    for token in line[len(VERIFY_LABEL):].split():
        key, value = token.split('=', 1) if '=' in token else (token, None)
        if key not in allowed:
            raise ValueError(f'unknown VERIFY field {key!r}')
        if value is None:
            raise ValueError(f'malformed VERIFY token {token!r}')
        if key in fields:
            raise ValueError(f'duplicate VERIFY field {key!r}')
        fields[key] = value
    return fields


def check_verify(line, expected_osha, expected_ssha,
    budget_ms=VERIFY_BUDGET_MS):
    """Check the success VERIFY line against the enrolled policies."""
    errors = []
    try:
        fields = parse_verify(line)
    except ValueError as error:
        return [str(error)]
    fixed = {
        'valid': '1',
        'reason': '0',
        'os': '1',
        'sysos': '1',
        'blocks': str(VERIFY_BLOCKS),
        'fail': '4294967295',
        'dma': '1',
        'osha': expected_osha,
        'ssha': expected_ssha,
    }
    required = set(VERIFY_FIELDS)
    if set(fields) != required:
        errors.append(f'VERIFY fields {sorted(fields)!r} do not match '
                      f'{sorted(required)!r}')
    for key, value in fixed.items():
        if fields.get(key) != value:
            errors.append(f'VERIFY {key}={fields.get(key)!r}, '
                          f'expected {value!r}')
    ms = fields.get('ms')
    if ms is None or not ms.isdigit():
        errors.append(f'VERIFY ms={ms!r}, expected milliseconds')
    elif int(ms) > budget_ms:
        errors.append(f'VERIFY ms={ms}, above the {budget_ms} ms budget')
    return errors


def parse_vread(line):
    if not line.startswith(VREAD_LABEL):
        raise ValueError('missing VREAD label')
    fields = {}
    for token in line[len(VREAD_LABEL):].split():
        if '=' not in token:
            raise ValueError(f'malformed token {token!r}')
        key, value = token.split('=', 1)
        if key not in VREAD_FIELDS:
            raise ValueError(f'unknown VREAD field {key}')
        if key in fields:
            raise ValueError(f'duplicate VREAD field {key}')
        fields[key] = value
    missing = [key for key in VREAD_FIELDS if key not in fields]
    if missing:
        raise ValueError(f'missing VREAD field {missing[0]}')
    return fields


def vread_spans(image_bytes, start, end):
    count = 0
    position = start & ~(HEADER_BYTES - 1)
    while position < end:
        count += 1
        position += HEADER_BYTES
    if end > image_bytes:
        return 0
    return count


def check_vread(line, image, expected):
    errors = []
    try:
        fields = parse_vread(line)
    except ValueError as error:
        return [str(error)]
    required = {
        'cases': str(VREAD_CASES), 'passed': str(VREAD_CASES),
        'failed': '0', 'reason': '0', 'mask': 'ff', 'windows': '2',
        'mismatches': '0', 'write': '1', 'exports': '7',
        'failcase': '4294967295', 'failstatus': '00000000',
        'failblock': '0', 'failactual': '0',
    }
    for key, value in required.items():
        if fields[key] != value:
            errors.append(f'VREAD {key} {fields[key]} != {value}')
    if fields['seq_bytes'] != str(VREAD_SEQ_BYTES):
        errors.append(f'VREAD seq_bytes {fields["seq_bytes"]} != '
                      f'{VREAD_SEQ_BYTES}')
    try:
        seq_ms = int(fields['seq_ms'])
    except ValueError:
        errors.append('VREAD seq_ms is not an integer')
        seq_ms = 0
    if seq_ms > VREAD_SEQ_BUDGET_MS:
        errors.append(f'VREAD seq_ms {seq_ms} exceeds '
                      f'{VREAD_SEQ_BUDGET_MS}')
    windows = {window['name']: window for window in expected['windows']}
    if 'OS' not in windows or 'SystemOS' not in windows:
        errors.append('VREAD needs OS and SystemOS windows')
        return errors
    os_window = windows['OS']
    system_window = windows['SystemOS']
    os_size = os_window['size']
    system_size = system_window['size']
    if fields['mwhole'] != '1':
        errors.append(f'VREAD mwhole {fields["mwhole"]} != 1')
    if fields['vst'] != '00000000':
        errors.append(f'VREAD vst {fields["vst"]} != 00000000')
    if fields['msize'] != str(os_size):
        errors.append(f'VREAD msize {fields["msize"]} != {os_size}')
    if os_size < VREAD_SEQ_BYTES or os_size < 1536:
        errors.append('VREAD OS window is too small for the cases')
        return errors
    spans = (1 + 1 + 1 + 2 +
             vread_spans(os_size, os_size - 1536, os_size) +
             VREAD_SEQ_BYTES // HEADER_BYTES)
    if fields['spans'] != str(spans):
        errors.append(f'VREAD spans {fields["spans"]} != {spans}')
    with image.open('rb') as stream:
        case_hash = hashlib.sha256()
        for offset, length in (
                (os_window['off'], 4096),
                (system_window['off'], 4096),
                (os_window['off'] + 512, 512),
                (os_window['off'] + 7 * 512, 1024),
                (os_window['off'] + os_size - 1536, 1536)):
            stream.seek(offset)
            data = stream.read(length)
            if len(data) != length:
                errors.append(f'VREAD image range {offset}+{length} '
                              'is short')
                return errors
            case_hash.update(data)
        stream.seek(os_window['off'])
        seq = stream.read(VREAD_SEQ_BYTES)
        if len(seq) != VREAD_SEQ_BYTES:
            errors.append('VREAD sequential range is short')
            return errors
    expected_digest = case_hash.hexdigest()[:32]
    expected_seq = hashlib.sha256(seq).hexdigest()[:32]
    if fields['dig'] != expected_digest:
        errors.append(f'VREAD dig {fields["dig"]} != {expected_digest}')
    if fields['sdig'] != expected_seq:
        errors.append(f'VREAD sdig {fields["sdig"]} != {expected_seq}')
    return errors


def policy_digests(directory):
    """Return the SHA-384 prefixes of both enrolled envelopes."""
    prefixes = []
    for name in ('OS', 'SystemOS'):
        header = (directory / name / 'header.bin').read_bytes()
        signature = (directory / name / 'signature.bin').read_bytes()
        table = (directory / 'objects' / f'{name}.vroot').read_bytes()
        if len(header) != 160 or len(signature) != 384:
            raise ValueError(f'{name} policy header/signature geometry')
        prefixes.append(hashlib.sha384(
            header + signature + table).hexdigest()[:32])
    return prefixes[0], prefixes[1]


def find_apfs_container(chunk):
    """Return (container UUID, first LBA) from the window's first 1 MiB."""
    header = chunk[BLOCK_BYTES:2 * BLOCK_BYTES]
    if header[0:8] != b'EFI PART':
        raise ValueError('window has no GPT header')
    entries_lba = u64(header, 72)
    count = u32(header, 80)
    entry_size = u32(header, 84)
    if not count or count > 128 or entry_size != ENTRY_BYTES:
        raise ValueError('unexpected GPT entry geometry')
    entries = chunk[entries_lba * BLOCK_BYTES:
                    entries_lba * BLOCK_BYTES + count * ENTRY_BYTES]
    for index in range(count):
        entry = entries[index * ENTRY_BYTES:(index + 1) * ENTRY_BYTES]
        if entry[0:16] != APFS_TYPE:
            continue
        first_lba = u64(entry, 32)
        node = chunk[first_lba * BLOCK_BYTES:
                     (first_lba + 1) * BLOCK_BYTES]
        if node[32:36] != b'NXSB':
            raise ValueError('Apple_APFS entry has no NXSB')
        if u32(node, 36) != 4096:
            raise ValueError('unexpected APFS block size')
        return node[0x48:0x58].hex(), first_lba
    raise ValueError('no Apple_APFS GPT entry')


def expected_from_image(path):
    size = path.stat().st_size
    with path.open('rb') as stream:
        header = stream.read(HEADER_BYTES)
        if len(header) != HEADER_BYTES:
            raise ValueError('short manifest header')
        if header[0:8] != b'SHNGROOT':
            raise ValueError('no SHNGROOT magic')
        version = u32(header, 8)
        count = u32(header, 12)
        image_bytes = u64(header, 16)
        if not ((version == 1 and 2 <= count <= 4) or
                (version == 2 and 2 <= count <= 8)):
            raise ValueError('unsupported manifest version/count')
        if image_bytes != size:
            raise ValueError(
                f'image is {size} bytes, manifest says {image_bytes}')
        digest = hashlib.sha256(header[:DIGEST_BASE]).digest()
        if digest != header[DIGEST_BASE:DIGEST_BASE + 32]:
            raise ValueError('manifest header digest mismatch')
        windows = []
        objects = []
        names = set()
        spans = []

        def padded(size):
            return (size + OBJECT_ALIGN_BYTES - 1) // OBJECT_ALIGN_BYTES * OBJECT_ALIGN_BYTES
        for index in range(count):
            entry = header[ENTRY_BASE + index * ENTRY_BYTES:
                           ENTRY_BASE + (index + 1) * ENTRY_BYTES]
            name_bytes = entry[:NAME_BYTES].split(b'\0', 1)[0]
            name = name_bytes.decode('ascii')
            offset = u64(entry, 32)
            window_bytes = u64(entry, 40)
            block_size = u32(entry, 48)
            flags = u32(entry, 52)
            if not name or name in names:
                raise ValueError('duplicate or empty window/object name')
            names.add(name)
            if any(offset < other + padded(other_size) and
                   other < offset + padded(window_bytes)
                   for other, other_size in spans):
                raise ValueError(f'entry {index} overlaps another entry')
            spans.append((offset, window_bytes))
            if (block_size, flags) == (512, 1):
                if offset % ALIGN_BYTES or window_bytes < EDGE_BYTES:
                    raise ValueError(f'window {index} geometry')
                end = offset + window_bytes
                if end > image_bytes:
                    raise ValueError(f'window {index} exceeds the image')
                stream.seek(offset)
                first = stream.read(EDGE_BYTES)
                stream.seek(end - EDGE_BYTES)
                last = stream.read(EDGE_BYTES)
                if len(first) != EDGE_BYTES or len(last) != EDGE_BYTES:
                    raise ValueError(f'window {index} is shorter than 1 MiB')
                windows.append({
                    'name': name,
                    'off': offset,
                    'size': window_bytes,
                    'hd0': digest_prefix(first),
                    'hd1': digest_prefix(last),
                })
            elif version == 2 and (block_size, flags) == (4096, 3):
                if offset % OBJECT_ALIGN_BYTES or not window_bytes or \
                        (window_bytes + OBJECT_ALIGN_BYTES - 1) // OBJECT_ALIGN_BYTES * OBJECT_ALIGN_BYTES > image_bytes - offset:
                    raise ValueError(f'object {index} geometry')
                stream.seek(offset)
                blob = stream.read(window_bytes)
                if len(blob) != window_bytes:
                    raise ValueError(f'object {index} is truncated')
                objects.append({'name': name, 'off': offset,
                                'size': window_bytes,
                                'sha256': hashlib.sha256(blob).hexdigest()})
            else:
                raise ValueError(f'entry {index} has unknown block size/flags')
        if len(windows) != 2:
            raise ValueError(f'expected 2 windows, found {len(windows)}')
        stream.seek(windows[0]['off'])
        first_chunk = stream.read(EDGE_BYTES)
        if len(first_chunk) != EDGE_BYTES:
            raise ValueError('first window is shorter than 1 MiB')
    container, _ = find_apfs_container(first_chunk)
    return {
        'count': len(windows),
        'version': version,
        'image_bytes': image_bytes,
        'manifest': hashlib.sha256(header).hexdigest()[:32],
        'container': container,
        'windows': windows,
        'objects': objects,
    }


def build_line(expected, volume, system_volume):
    parts = [f'{LABEL}valid=1 reason=0 count={expected["count"]} '
             f'image={expected["image_bytes"]} pub=2 phase=5 perr=0 '
             f'wfail=4294967295 wdiag=15 wpoll=1 wtime=0 '
             f'mdig={expected["manifest"]}']
    for index, window in enumerate(expected['windows']):
        parts.append(
            f'w{index}={window["name"]} off={window["off"]} '
            f'size={window["size"]} hd0={window["hd0"]} '
            f'hd1={window["hd1"]}')
    parts.append(f'apfs=1 cid={expected["container"]} uuid={volume} '
                 f'suuid={system_volume}')
    parts.append('pmc=1 pwait=0 rms=1677 rch=129')
    return ' '.join(parts)


def self_test():
    expected = {
        'count': 2,
        'image_bytes': 8 << 20,
        'manifest': 'e' * 32,
        'container': 'c' * 32,
        'windows': [
            {'name': 'OS', 'off': 1 << 20, 'size': 2 << 20,
             'hd0': 'a' * 32, 'hd1': 'b' * 32},
            {'name': 'SystemOS', 'off': 3 << 20, 'size': 2 << 20,
             'hd0': 'd' * 32, 'hd1': 'f' * 32},
        ],
    }
    volume = uuid_bytes(VOLUME_UUID).hex()
    system_volume = uuid_bytes(SYSTEM_VOLUME_UUID).hex()
    line = build_line(expected, volume, system_volume)
    errors = check(line, dict(expected, volume=volume,
                              system_volume=system_volume))
    assert not errors, errors
    mutations = (
        (line.replace('valid=1', 'valid=0'), 'valid'),
        (line.replace('reason=0', 'reason=2'), 'reason'),
        (line.replace('count=2', 'count=1'), 'count'),
        (line.replace('image=8388608', 'image=8388609'), 'image'),
        (line.replace('pub=2', 'pub=1'), 'pub'),
        (line.replace('phase=5', 'phase=4'), 'phase'),
        (line.replace('perr=0', 'perr=4'), 'perr'),
        (line.replace('mdig=' + 'e' * 32, 'mdig=' + '0' * 32), 'mdig'),
        (line.replace('w0=OS', 'w0=SystemOS'), 'name'),
        (line.replace('off=1048576', 'off=2097152'), 'offset'),
        (line.replace('size=2097152', 'size=1048576'), 'size'),
        (line.replace('hd0=' + 'a' * 32, 'hd0=' + '0' * 32), 'hd0'),
        (line.replace('hd1=' + 'b' * 32, 'hd1=' + '0' * 32), 'hd1'),
        (line.replace('apfs=1', 'apfs=0'), 'apfs'),
        (line.replace('cid=' + 'c' * 32, 'cid=' + '0' * 32), 'cid'),
        (line.replace('uuid=' + volume, 'uuid=' + '0' * 32), 'uuid'),
        (line.replace('suuid=' + system_volume, 'suuid=' + '0' * 32),
         'suuid'),
        (line.replace('rms=1677', 'rms=9999'), 'rms budget'),
        (line.replace('rch=129', 'rch=0'), 'rch zero'),
        (line.replace('wfail=4294967295', 'wfail=1'), 'wfail'),
        (line.replace('wdiag=15', 'wdiag=0'), 'wdiag zero'),
        (line.replace('wdiag=15', 'wdiag=3'), 'wdiag no accept'),
        (line.replace('wdiag=15', 'wdiag=7'), 'wdiag no accept apfs'),
        (line.replace('wdiag=15', 'wdiag=27'), 'wdiag duplicate'),
        (line.replace('wdiag=15', 'wdiag=23'), 'wdiag duplicate no accept'),
        (line.replace('wdiag=15', 'wdiag=31'), 'wdiag all bits'),
        (line.replace('wpoll=1', 'wpoll=0'), 'wpoll'),
        (line.replace('wtime=0', 'wtime=2001'), 'wtime'),
        (line + ' extra=1', 'extra field'),
        (line.replace('w1=SystemOS', 'w1=SystemOS extra=x'), 'gap'),
    )
    for text, label in mutations:
        assert check(text, dict(expected, volume=volume,
                                system_volume=system_volume)), \
            f'{label} was accepted'
    raw_accept = line.replace('wdiag=15', 'wdiag=11')
    assert not check(raw_accept, dict(expected, volume=volume,
                                      system_volume=system_volume)), \
        'media+UUID+accept diagnostics without the optional APFS bit ' \
        'were rejected'
    pre = (f'{LABEL}valid=0 reason=2 count=0 image=0 pmc=1 pwait=0 '
           f'rms=0 rch=0')
    assert not check_pre_write(pre), check_pre_write(pre)
    for text, label in (
            (pre.replace('reason=2', 'reason=1'), 'pre reason'),
            (pre.replace('valid=0', 'valid=1'), 'pre valid'),
            (pre + ' pub=0', 'pre extra'),
            (pre.replace('pmc=1', 'pmc=0'), 'pre missing media'),
            (pre.replace('pmc=1', 'pmc=2'), 'pre duplicate'),
            (pre.replace(' pwait=0', ''), 'pre no wait'),
            (pre.replace('pwait=0', 'pwait=99999'), 'pre wait budget'),
            (pre.replace('rms=0 rch=0', 'rms=1000 rch=74'),
             'pre read stats')):
        assert check_pre_write(text), f'{label} was accepted'
    osha, ssha = '1' * 32, '2' * 32
    verify = (f'{VERIFY_LABEL}valid=1 reason=0 os=1 sysos=1 blocks=1025 '
              f'ms=10968 fail=4294967295 dma=1 osha={osha} ssha={ssha}')
    assert not check_verify(verify, osha, ssha), \
        check_verify(verify, osha, ssha)
    for text, label in (
            (verify.replace('valid=1', 'valid=0'), 'verify valid'),
            (verify.replace('reason=0', 'reason=3'), 'verify reason'),
            (verify.replace('os=1', 'os=0'), 'verify os'),
            (verify.replace('sysos=1', 'sysos=0'), 'verify sysos'),
            (verify.replace('blocks=1025', 'blocks=1024'), 'verify blocks'),
            (verify.replace('ms=10968', 'ms=12001'), 'verify budget'),
            (verify.replace('fail=4294967295', 'fail=7'), 'verify fail'),
            (verify.replace('dma=1', 'dma=0'), 'verify dma'),
            (verify.replace('osha=' + osha, 'osha=' + '0' * 32),
             'verify osha'),
            (verify.replace('ssha=' + ssha, 'ssha=' + '0' * 32),
             'verify ssha'),
            (verify + ' vobj=0', 'verify failure field on success'),
            (verify + ' extra=1', 'verify extra field'),
            (verify.replace('ms=10968', 'ms=x'), 'verify ms type')):
        assert check_verify(text, osha, ssha), f'{label} was accepted'
    failure = (f'{VERIFY_LABEL}valid=0 reason=3 os=0 sysos=0 blocks=0 '
               f'ms=16236 fail=4294967295 dma=0 vobj=0 voff=47480832 '
               f'vlen=288 vio=e00002c2 vact=0 osha=' + '0' * 32 +
               f' ssha=' + '0' * 32)
    assert check_verify(failure, osha, ssha), \
        'the S5c reason=3 line was accepted'
    with tempfile.TemporaryDirectory(prefix='gta4l-windows-check-') as raw:
        image = Path(raw) / 'userdata.img'
        total = 8 << 20

        def make_image(version, object_entries, duplicate=False):
            data = bytearray(total)
            header = bytearray(HEADER_BYTES)
            header[0:8] = b'SHNGROOT'
            struct.pack_into('<I', header, 8, version)
            struct.pack_into('<I', header, 12, 2 + len(object_entries))
            struct.pack_into('<Q', header, 16, total)
            entries = [('OS', 1 << 20, 1 << 20, 512, 1),
                       ('SystemOS', 2 << 20, 1 << 20, 512, 1)] + object_entries
            for index, (name, offset, window_bytes, block_size, flags) in enumerate(entries):
                entry = ENTRY_BASE + index * ENTRY_BYTES
                header[entry:entry + len(name)] = name.encode()
                struct.pack_into('<Q', header, entry + 32, offset)
                struct.pack_into('<Q', header, entry + 40, window_bytes)
                struct.pack_into('<I', header, entry + 48, block_size)
                struct.pack_into('<I', header, entry + 52, flags)
            for byte in range(1 << 20):
                data[(1 << 20) + byte] = byte & 0xff
                data[(2 << 20) + byte] = (0x40 + byte) & 0xff
            for name, offset, size, _, _ in object_entries:
                data[offset:offset + size] = name.encode().ljust(size, b'\0')
            header[DIGEST_BASE:DIGEST_BASE + 32] = \
                hashlib.sha256(header[:DIGEST_BASE]).digest()
            data[:HEADER_BYTES] = header
            gpt = 1 << 20
            data[gpt + 512:gpt + 520] = b'EFI PART'
            struct.pack_into('<Q', data, gpt + 512 + 72, 2)
            struct.pack_into('<I', data, gpt + 512 + 80, 1)
            struct.pack_into('<I', data, gpt + 512 + 84, 128)
            data[gpt + 1024:gpt + 1024 + 16] = APFS_TYPE
            struct.pack_into('<Q', data, gpt + 1024 + 32, 34)
            struct.pack_into('<Q', data, gpt + 1024 + 40, 35)
            node = gpt + 34 * 512
            data[node + 32:node + 36] = b'NXSB'
            struct.pack_into('<I', data, node + 36, 4096)
            data[node + 0x48:node + 0x58] = bytes(range(16))
            image.write_bytes(bytes(data))
            return image

        make_image(1, [])
        parsed = expected_from_image(image)
        assert parsed['count'] == 2 and parsed['version'] == 1
        assert parsed['container'] == bytes(range(16)).hex()
        assert parsed['windows'][0]['name'] == 'OS'
        assert parsed['objects'] == []
        synthetic = build_line(parsed, volume, system_volume)
        assert not check(synthetic, dict(parsed, volume=volume,
                                        system_volume=system_volume)), \
            check(synthetic, dict(parsed, volume=volume,
                                  system_volume=system_volume))
        objects = [('OS.vroot', 4 << 20, 4096, 4096, 3),
                   ('SystemOS.vroot', 5 << 20, 4096, 4096, 3)]
        make_image(2, objects)
        parsed = expected_from_image(image)
        assert parsed['count'] == 2 and parsed['version'] == 2
        assert [entry['name'] for entry in parsed['objects']] == \
            ['OS.vroot', 'SystemOS.vroot']
        assert parsed['objects'][0]['sha256'] == \
            hashlib.sha256(b'OS.vroot'.ljust(4096, b'\0')).hexdigest()
        synthetic = build_line(parsed, volume, system_volume)
        assert not check(synthetic, dict(parsed, volume=volume,
                                        system_volume=system_volume))
        for bad_objects, label in (
                ([('OS.vroot', 4 << 20, 4096, 512, 1)],
                 'object with window attributes'),
                ([('OS.vroot', 4 << 20, 4096, 4096, 3),
                  ('SystemOS.vroot', 4 << 20, 4096, 4096, 3)],
                 'object overlap with another object'),
                ([('OS', 6 << 20, 4096, 4096, 3)],
                 'duplicate window name')):
            make_image(2, bad_objects)
            try:
                expected_from_image(image)
            except ValueError:
                pass
            else:
                raise AssertionError(f'{label} was accepted')
        make_image(1, objects)
        try:
            expected_from_image(image)
        except ValueError:
            pass
        else:
            raise AssertionError('a version-1 manifest with objects was accepted')
    with tempfile.TemporaryDirectory(prefix='gta4l-vread-check-') as raw:
        image = Path(raw) / 'vread.img'
        total = 8 << 20
        os_off = 1 << 20
        os_size = (4 << 20) + 1536
        system_off = 6 << 20
        system_size = (1 << 20) + 1536
        data = bytearray(total)
        for index in range(total):
            data[index] = (index * 29 + index // 4096) & 0xff
        image.write_bytes(bytes(data))
        expected_vread = {
            'windows': [
                {'name': 'OS', 'off': os_off, 'size': os_size},
                {'name': 'SystemOS', 'off': system_off,
                 'size': system_size},
            ],
        }
        case_hash = hashlib.sha256()
        for offset, length in (
                (os_off, 4096), (system_off, 4096),
                (os_off + 512, 512), (os_off + 7 * 512, 1024),
                (os_off + os_size - 1536, 1536)):
            case_hash.update(data[offset:offset + length])
        seq_digest = hashlib.sha256(
            data[os_off:os_off + (4 << 20)]).hexdigest()[:32]
        vread = (f'{VREAD_LABEL}cases=8 passed=8 failed=0 reason=0 '
                 f'mask=ff windows=2 msize={os_size} mwhole=1 '
                 f'vst=00000000 spans=1030 mismatches=0 write=1 '
                 f'exports=7 seq_ms=1000 seq_bytes=4194304 '
                 f'failcase=4294967295 failstatus=00000000 failblock=0 '
                 f'failactual=0 dig={case_hash.hexdigest()[:32]} '
                 f'sdig={seq_digest}')
        assert not check_vread(vread, image, expected_vread), \
            check_vread(vread, image, expected_vread)
        for text, label in (
                (vread.replace('mask=ff', 'mask=7f'), 'vread mask'),
                (vread.replace(f'msize={os_size}', 'msize=1'),
                 'vread msize'),
                (vread.replace('mwhole=1', 'mwhole=0'), 'vread mwhole'),
                (vread.replace('vst=00000000', 'vst=00000001'),
                 'vread target status'),
                (vread.replace('spans=1030', 'spans=1029'), 'vread spans'),
                (vread.replace('write=1', 'write=0'), 'vread write'),
                (vread.replace('exports=7', 'exports=3'), 'vread exports'),
                (vread.replace('seq_bytes=4194304', 'seq_bytes=1048576'),
                 'vread seq bytes'),
                (vread.replace('cases=8', 'cases=7'), 'vread cases'),
                (vread.replace('dig=' + case_hash.hexdigest()[:32],
                               'dig=' + '0' * 32), 'vread dig'),
                (vread.replace('sdig=' + seq_digest, 'sdig=' + '0' * 32),
                 'vread sdig'),
                (vread.replace('seq_ms=1000', 'seq_ms=30001'),
                 'vread seq budget'),
                (vread + ' extra=1', 'vread extra field')):
            assert check_vread(text, image, expected_vread), \
                f'{label} was accepted'
    print('PASS: windows evidence checker self-test')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--line')
    parser.add_argument('--policies', type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        'out/gta4l/s5-policies')
    parser.add_argument('--expect-verify', action='store_true')
    parser.add_argument('--expect-vread', action='store_true')
    parser.add_argument('--pre-write', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.image is None or not (args.report or args.line):
        parser.error('--image and one of --report/--line are required')
    if args.line is not None:
        text = args.line
    else:
        text = args.report.read_text()
    lines = [line for line in text.splitlines() if line.startswith(LABEL)]
    if len(lines) != 1:
        print(f'FAIL: expected one WINDOWS line, found {len(lines)}')
        return 1
    line = lines[0].strip()
    if args.pre_write:
        errors = check_pre_write(line)
    else:
        try:
            expected = expected_from_image(args.image)
        except (OSError, ValueError, struct.error) as error:
            print(f'FAIL: {error}')
            return 1
        expected['volume'] = uuid_bytes(VOLUME_UUID).hex()
        expected['system_volume'] = uuid_bytes(SYSTEM_VOLUME_UUID).hex()
        errors = check(line, expected)
    verify_lines = [line for line in text.splitlines()
                    if line.startswith(VERIFY_LABEL)]
    if args.expect_verify and not verify_lines:
        errors.append('missing VERIFY line')
    if verify_lines:
        if len(verify_lines) != 1:
            errors.append(f'expected one VERIFY line, found '
                          f'{len(verify_lines)}')
        else:
            try:
                osha, ssha = policy_digests(args.policies)
            except (OSError, ValueError) as error:
                print(f'FAIL: policies: {error}')
                return 1
            errors += check_verify(verify_lines[0].strip(), osha, ssha)
    vread_lines = [line for line in text.splitlines()
                   if line.startswith(VREAD_LABEL)]
    if args.expect_vread and not vread_lines:
        errors.append('missing VREAD line')
    if vread_lines:
        if len(vread_lines) != 1:
            errors.append(f'expected one VREAD line, found '
                          f'{len(vread_lines)}')
        elif not args.pre_write:
            try:
                expected = expected_from_image(args.image)
            except (OSError, ValueError, struct.error) as error:
                print(f'FAIL: {error}')
                return 1
            errors += check_vread(vread_lines[0].strip(), args.image,
                                  expected)
    for error in errors:
        print(f'FAIL: {error}')
    if errors:
        return 1
    print('PASS: WINDOWS report matches the image' +
          (' (pre-write)' if args.pre_write else '') +
          (' and VERIFY matches the policies' if verify_lines else '') +
          (' and VREAD matches the image' if vread_lines else ''))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
