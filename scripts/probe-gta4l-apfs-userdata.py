#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Read-only APFS identity probe for the gta4l userdata windows.

The S4 WINDOWS rung publishes the SHNGROOT OS and SystemOS windows and
reports one volume UUID per window. This host-only probe re-derives those
identities from the image without mounting or rewriting it. It parses the
SHNGROOT manifest, the OS window's GPT, each window's APFS container
superblock, the checkpoint descriptor ring, the container object map and
each volume superblock. Every inspected block is checksum-verified and
bounds-checked, traversal is bounded, and the parser rejects unsupported
forms (hashed or variable B-trees, encrypted or no-header mappings, Fusion
containers, non-4096-byte blocks) instead of guessing. It never scans for
an APSB block as a fallback.

Expected identities are the build constants' provenance: OS
04769D26-2CA0-4825-B443-E2E5F88300F2 and SystemOS
8846FE78-2910-4297-AF97-D59BB6F8FC16. `probe_image` requires each window
to contain exactly one matching volume and rejects a duplicate expected
identity anywhere in the image. The evidence checker imports this module
and compares the device report against these image-derived values, so the
constants cannot validate themselves.
"""
import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

BLOCK_BYTES = 4096
MANIFEST_BYTES = 4096
WINDOW_ALIGN = 1 << 20
MAX_WINDOWS = 4
MAX_VOLUMES = 100
MAX_DESC_BLOCKS = 1024
MAX_TREE_DEPTH = 16
MAX_TREE_NODES = 200000
GPT_ENTRY_BYTES = 128
APFS_TYPE = bytes.fromhex('ef57347c0000aa11aa1100306543ecac')
SHNGROOT_MAGIC = b'SHNGROOT'
NX_MAGIC = b'NXSB'
APSB_MAGIC = b'APSB'
NX_INCOMPAT_VERSION1 = 0x1
NX_INCOMPAT_VERSION2 = 0x2
NX_INCOMPAT_FUSION = 0x100
OBJECT_TYPE_MASK = 0x0000ffff
OBJECT_TYPE_NX_SUPERBLOCK = 0x1
OBJECT_TYPE_BTREE = 0x2
OBJECT_TYPE_BTREE_NODE = 0x3
OBJECT_TYPE_SPACEMAN = 0x5
OBJECT_TYPE_OMAP = 0xb
OBJECT_TYPE_CHECKPOINT_MAP = 0xc
OBJECT_TYPE_FS = 0xd
OBJ_STORAGETYPE_MASK = 0xc0000000
OBJ_VIRTUAL = 0x00000000
OBJ_PHYSICAL = 0x40000000
OBJ_EPHEMERAL = 0x80000000
OBJ_NOHEADER = 0x20000000
OBJ_ENCRYPTED = 0x10000000
BTREE_PHYSICAL = 0x10
BTREE_EPHEMERAL = 0x8
BTREE_NONPERSISTENT = 0x20
BTREE_KV_NONALIGNED = 0x40
BTREE_HASHED = 0x80
BTREE_NOHEADER = 0x100
BTNODE_ROOT = 0x0001
BTNODE_LEAF = 0x0002
BTNODE_FIXED_KV_SIZE = 0x0004
BTNODE_HASHED = 0x0008
BTNODE_NOHEADER = 0x0010
BTOFF_INVALID = 0xffff
OMAP_VAL_DELETED = 0x00000001
OMAP_VAL_SAVED = 0x00000002
OMAP_VAL_ENCRYPTED = 0x00000004
OMAP_VAL_NOHEADER = 0x00000008
OMAP_VAL_CRYPTO_GENERATION = 0x00000010
OMAP_VALID_FLAGS = 0x0000001f
OMAP_ENCRYPTING = 0x00000002
OMAP_DECRYPTING = 0x00000004
OMAP_KEYROLLING = 0x00000008
APFS_INCOMPAT_SEALED_VOLUME = 0x20
APFS_VOL_ROLE_NAMES = {
    0x0000: 'none',
    0x0001: 'system',
    0x0002: 'user',
    0x0004: 'recovery',
    0x0008: 'vm',
    0x0010: 'preboot',
    0x0020: 'installer',
    0x0040: 'data',
    0x0080: 'baseband',
    0x00c0: 'update',
    0x0100: 'xart',
    0x0140: 'hardware',
    0x0180: 'backup',
    0x01c0: 'reserved-7',
    0x0200: 'reserved-8',
    0x0240: 'enterprise',
    0x0280: 'reserved-10',
    0x02c0: 'prelogin',
}
EXPECTED_VOLUME_UUIDS = {
    'OS': '04769D26-2CA0-4825-B443-E2E5F88300F2',
    'SystemOS': '8846FE78-2910-4297-AF97-D59BB6F8FC16',
}
UNSEEN_MS = 0xffffffff


class ProbeError(Exception):
    """The image does not match a supported, self-consistent APFS layout."""


def u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def u64(data, offset):
    return struct.unpack_from('<Q', data, offset)[0]


def uuid_text(raw):
    hexed = raw.hex()
    return (f'{hexed[0:8]}-{hexed[8:12]}-{hexed[12:16]}-'
            f'{hexed[16:20]}-{hexed[20:32]}').upper()


def fletcher64(words, init=0):
    """Fletcher-64 with apfs-fuse's modulo-0xffffffff sum reduction."""
    sum1 = init & 0xffffffff
    sum2 = init >> 32
    for word in words:
        sum1 += word
        sum2 += sum1
    return (((sum2 % 0xffffffff) << 32) |
            (sum1 % 0xffffffff))


def verify_block(data, what):
    """Require a 4096-byte block whose stored checksum verifies to zero."""
    if len(data) != BLOCK_BYTES:
        raise ProbeError(f'{what}: short block ({len(data)} bytes)')
    stored = u64(data, 0)
    if stored in (0, 0xffffffffffffffff):
        raise ProbeError(f'{what}: block checksum is unset')
    words = list(struct.unpack_from('<%dI' % (BLOCK_BYTES // 4), data))
    checksum = fletcher64(words[2:], 0)
    checksum = fletcher64(words[:2], checksum)
    if checksum != 0:
        raise ProbeError(f'{what}: block checksum mismatch')


def set_checksum(block):
    words = list(struct.unpack_from('<%dI' % (BLOCK_BYTES // 4), block))
    sum1 = 0
    sum2 = 0
    for word in words[2:]:
        sum1 += word
        sum2 += sum1
    low = (-(sum1 + sum2)) % 0xffffffff
    high = sum2 % 0xffffffff
    struct.pack_into('<II', block, 0, low, high)


def parse_manifest(stream, image_bytes):
    stream.seek(0)
    header = stream.read(MANIFEST_BYTES)
    if len(header) != MANIFEST_BYTES:
        raise ProbeError('short SHNGROOT header')
    if header[0:8] != SHNGROOT_MAGIC:
        raise ProbeError('no SHNGROOT magic')
    if u32(header, 8) != 1:
        raise ProbeError('unsupported SHNGROOT version')
    count = u32(header, 12)
    if not 2 <= count <= MAX_WINDOWS:
        raise ProbeError(f'unsupported window count {count}')
    if u64(header, 16) != image_bytes:
        raise ProbeError(
            f'manifest image_bytes {u64(header, 16)} != file size '
            f'{image_bytes}')
    if hashlib.sha256(header[:4064]).digest() != header[4064:4096]:
        raise ProbeError('manifest header digest mismatch')
    windows = []
    names = set()
    for index in range(count):
        entry = header[24 + index * 128:24 + (index + 1) * 128]
        name_bytes = entry[:32].split(b'\0', 1)[0]
        try:
            name = name_bytes.decode('ascii')
        except UnicodeDecodeError as error:
            raise ProbeError(f'window {index} name is not ASCII') from error
        if not name or name in names:
            raise ProbeError(f'window {index} has an empty or duplicate name')
        names.add(name)
        offset = u64(entry, 32)
        size = u64(entry, 40)
        if offset % WINDOW_ALIGN or size < WINDOW_ALIGN:
            raise ProbeError(f'window {name} is not 1 MiB aligned/sized')
        if offset < WINDOW_ALIGN or offset + size > image_bytes:
            raise ProbeError(f'window {name} is outside the image')
        if u32(entry, 48) != 512 or u32(entry, 52) != 1:
            raise ProbeError(f'window {name} has unsupported geometry/flags')
        if any(entry[88:128]):
            raise ProbeError(f'window {name} reserved bytes are nonzero')
        for other in windows:
            if offset < other['offset'] + other['size'] and \
                    other['offset'] < offset + size:
                raise ProbeError(f'window {name} overlaps {other["label"]}')
        windows.append({'label': name, 'offset': offset, 'size': size})
    return windows


def read_block(stream, base, block, block_count, what):
    if block >= block_count:
        raise ProbeError(f'{what}: block {block} outside the container')
    stream.seek(base + block * BLOCK_BYTES)
    data = stream.read(BLOCK_BYTES)
    verify_block(data, f'{what} at block {block}')
    return data


def parse_gpt(stream, window):
    """Return the single Apple_APFS partition as (base, size, first_lba)."""
    stream.seek(window['offset'] + 512)
    header = stream.read(512)
    if len(header) != 512 or header[0:8] != b'EFI PART':
        raise ProbeError('OS window has no GPT header')
    header_size = u32(header, 12)
    if not 92 <= header_size <= 512:
        raise ProbeError('unexpected GPT header size')
    stored_crc = u32(header, 16)
    zeroed = bytearray(header)
    zeroed[16:20] = b'\0\0\0\0'
    if zlib.crc32(bytes(zeroed[:header_size])) & 0xffffffff != stored_crc:
        raise ProbeError('GPT header CRC mismatch')
    current = u64(header, 24)
    if current != 1:
        raise ProbeError(f'GPT header current LBA {current}, expected 1')
    backup = u64(header, 32)
    if not 1 < backup < window['size'] // 512:
        raise ProbeError('GPT backup LBA is outside the window')
    first_usable = u64(header, 40)
    last_usable = u64(header, 48)
    entries_lba = u64(header, 72)
    entry_count = u32(header, 80)
    entry_size = u32(header, 84)
    if entry_size != GPT_ENTRY_BYTES or not 1 <= entry_count <= 128:
        raise ProbeError('unsupported GPT entry geometry')
    entries_bytes = entry_count * entry_size
    if entries_lba * 512 + entries_bytes > window['size']:
        raise ProbeError('GPT entry array is outside the window')
    stream.seek(window['offset'] + entries_lba * 512)
    entries = stream.read(entries_bytes)
    if len(entries) != entries_bytes:
        raise ProbeError('short GPT entry array')
    if zlib.crc32(entries) & 0xffffffff != u32(header, 88):
        raise ProbeError('GPT entry array CRC mismatch')
    apfs = []
    for index in range(entry_count):
        entry = entries[index * entry_size:(index + 1) * entry_size]
        if entry[0:16] != APFS_TYPE:
            continue
        first = u64(entry, 32)
        last = u64(entry, 40)
        if first > last:
            raise ProbeError('Apple_APFS entry has an inverted extent')
        if first < first_usable or last > last_usable:
            raise ProbeError('Apple_APFS entry is outside the usable area')
        apfs.append((first, last))
    if len(apfs) != 1:
        raise ProbeError(
            f'OS window has {len(apfs)} Apple_APFS partitions, expected 1')
    first, last = apfs[0]
    return (window['offset'] + first * 512,
            (last - first + 1) * 512, first)


def parse_nxsb(block, what):
    if block[32:36] != NX_MAGIC:
        raise ProbeError(f'{what}: no NXSB magic')
    block_size = u32(block, 36)
    if block_size != BLOCK_BYTES:
        raise ProbeError(f'{what}: unsupported block size {block_size}')
    block_count = u64(block, 40)
    if block_count == 0:
        raise ProbeError(f'{what}: zero container block count')
    incompatible = u64(block, 64)
    if incompatible & ~(NX_INCOMPAT_VERSION1 | NX_INCOMPAT_VERSION2):
        raise ProbeError(
            f'{what}: unsupported incompatible features 0x{incompatible:x}')
    max_fs = u32(block, 180)
    if max_fs > MAX_VOLUMES:
        raise ProbeError(f'{what}: unsupported volume count {max_fs}')
    desc_blocks = u32(block, 104)
    desc_base = u64(block, 112)
    desc_index = u32(block, 136)
    desc_len = u32(block, 140)
    if not 1 <= desc_blocks <= MAX_DESC_BLOCKS:
        raise ProbeError(f'{what}: implausible descriptor area')
    if desc_base + desc_blocks > block_count:
        raise ProbeError(f'{what}: descriptor area outside the container')
    if desc_index >= desc_blocks or not 1 <= desc_len <= desc_blocks:
        raise ProbeError(f'{what}: implausible checkpoint descriptor range')
    data_blocks = u32(block, 108)
    data_base = u64(block, 120)
    if data_blocks and data_base + data_blocks > block_count:
        raise ProbeError(f'{what}: checkpoint data area outside the container')
    return {
        'block_size': block_size,
        'block_count': block_count,
        'incompatible': incompatible,
        'max_fs': max_fs,
        'fs_oid': [u64(block, 184 + index * 8) for index in range(max_fs)],
        'desc_blocks': desc_blocks,
        'desc_base': desc_base,
        'desc_index': desc_index,
        'desc_len': desc_len,
        'omap_oid': u64(block, 160),
        'spaceman_oid': u64(block, 152),
        'uuid': block[72:88].hex(),
        'xid': u64(block, 16),
        'paddr': 0,
    }


def checkpoint_identity(nxsb):
    return (nxsb['block_count'], nxsb['omap_oid'],
            tuple(nxsb['fs_oid']), nxsb['uuid'])


def select_checkpoint(stream, base, block_zero, nxsb):
    """Return the latest valid, self-consistent checkpoint from the ring."""
    candidates = []
    for position in range(nxsb['desc_blocks']):
        paddr = nxsb['desc_base'] + position
        stream.seek(base + paddr * BLOCK_BYTES)
        block = stream.read(BLOCK_BYTES)
        try:
            verify_block(block, f'checkpoint descriptor {position}')
        except ProbeError:
            continue
        if block[32:36] != NX_MAGIC:
            continue
        if (u32(block, 24) & OBJECT_TYPE_MASK) != OBJECT_TYPE_NX_SUPERBLOCK:
            continue
        if u32(block, 36) != BLOCK_BYTES or u64(block, 40) != nxsb['block_count']:
            continue
        try:
            candidate = parse_nxsb(block, f'checkpoint descriptor {position}')
        except ProbeError:
            continue
        candidate['paddr'] = paddr
        if (candidate['desc_index'] + candidate['desc_len'] - 1) % \
                nxsb['desc_blocks'] != position:
            continue
        try:
            candidate['mappings'] = read_checkpoint_map(
                stream, base, nxsb, candidate)
        except ProbeError:
            continue
        candidates.append(candidate)
    if not candidates:
        raise ProbeError('no valid self-consistent checkpoint in the descriptor ring')
    newest = max(candidate['xid'] for candidate in candidates)
    winners = [candidate for candidate in candidates
               if candidate['xid'] == newest]
    identities = {checkpoint_identity(candidate) for candidate in winners}
    if len(identities) != 1:
        raise ProbeError(
            f'ambiguous checkpoint xid {newest}: {len(winners)} candidates')
    chosen = winners[0]
    if block_zero['xid'] > chosen['xid']:
        raise ProbeError(
            f'container superblock xid {block_zero["xid"]} is newer than the '
            f'checkpoint ring xid {chosen["xid"]}')
    if block_zero['xid'] == chosen['xid'] and \
            checkpoint_identity(block_zero) != checkpoint_identity(chosen):
        raise ProbeError('container superblock and checkpoint disagree')
    return chosen


def read_checkpoint_map(stream, base, nxsb, chosen):
    """Validate the chosen checkpoint's map blocks and its spaceman object."""
    mappings = {}
    for offset in range(chosen['desc_len'] - 1):
        position = (chosen['desc_index'] + offset) % nxsb['desc_blocks']
        paddr = nxsb['desc_base'] + position
        block = read_block(stream, base, paddr,
                           nxsb['block_count'], 'checkpoint map')
        if (u32(block, 24) & OBJECT_TYPE_MASK) != OBJECT_TYPE_CHECKPOINT_MAP:
            raise ProbeError('checkpoint descriptor is not a map')
        if u64(block, 16) != chosen['xid']:
            raise ProbeError('checkpoint map xid does not match the checkpoint')
        count = u32(block, 36)
        if count > (BLOCK_BYTES - 40) // 40:
            raise ProbeError('checkpoint map entry count is too large')
        for index in range(count):
            entry = 40 + index * 40
            object_type = u32(block, entry)
            size = u32(block, entry + 8)
            oid = u64(block, entry + 24)
            object_paddr = u64(block, entry + 32)
            if oid == 0 or size == 0:
                raise ProbeError('checkpoint map has a zero oid or size')
            if (object_type & OBJ_STORAGETYPE_MASK) != OBJ_EPHEMERAL:
                raise ProbeError('checkpoint map entry is not ephemeral')
            if object_type & (OBJ_NOHEADER | OBJ_ENCRYPTED):
                raise ProbeError('checkpoint map entry is encrypted/no-header')
            blocks = (size + BLOCK_BYTES - 1) // BLOCK_BYTES
            if object_paddr + blocks > nxsb['block_count']:
                raise ProbeError('checkpoint map entry is outside the container')
            record = (object_type, size, object_paddr)
            if oid in mappings and mappings[oid] != record:
                raise ProbeError('checkpoint map has conflicting entries')
            mappings[oid] = record
    spaceman = mappings.get(chosen['spaceman_oid'])
    if spaceman is None:
        raise ProbeError('checkpoint map does not map the spaceman')
    object_type, size, object_paddr = spaceman
    if size != BLOCK_BYTES:
        raise ProbeError('unsupported multi-block spaceman object')
    block = read_block(stream, base, object_paddr,
                       nxsb['block_count'], 'spaceman')
    if (u32(block, 24) & OBJECT_TYPE_MASK) != OBJECT_TYPE_SPACEMAN:
        raise ProbeError('checkpoint-mapped spaceman has the wrong type')
    if u64(block, 16) != chosen['xid']:
        raise ProbeError('spaceman xid does not match the checkpoint')
    return mappings


def parse_btree_node(stream, base, block_count, paddr, root):
    block = read_block(stream, base, paddr, block_count, 'B-tree node')
    object_type = u32(block, 24) & OBJECT_TYPE_MASK
    if root:
        if object_type != OBJECT_TYPE_BTREE:
            raise ProbeError('omap root is not a B-tree object')
    elif object_type != OBJECT_TYPE_BTREE_NODE:
        raise ProbeError('omap child is not a B-tree node object')
    flags = u16(block, 32)
    level = u16(block, 34)
    nkeys = u32(block, 36)
    if flags & (BTNODE_HASHED | BTNODE_NOHEADER):
        raise ProbeError('unsupported hashed/no-header B-tree node')
    if not flags & BTNODE_FIXED_KV_SIZE:
        raise ProbeError('unsupported variable-size B-tree node')
    if bool(flags & BTNODE_ROOT) != root:
        raise ProbeError('B-tree root flag mismatch')
    if level > MAX_TREE_DEPTH:
        raise ProbeError('B-tree depth exceeds the limit')
    table_off = u16(block, 40)
    table_len = u16(block, 42)
    if table_off != 0 or table_len % 4 or table_len > BLOCK_BYTES - 56:
        raise ProbeError('unsupported B-tree table space')
    if nkeys > table_len // 4:
        raise ProbeError('B-tree key count exceeds the table space')
    keys_start = 56 + table_len
    if root:
        info = block[BLOCK_BYTES - 40:BLOCK_BYTES]
        tree_flags = u32(info, 0)
        node_size = u32(info, 4)
        key_size = u32(info, 8)
        val_size = u32(info, 12)
        if tree_flags & (BTREE_HASHED | BTREE_NOHEADER |
                         BTREE_EPHEMERAL | BTREE_NONPERSISTENT |
                         BTREE_KV_NONALIGNED):
            raise ProbeError('unsupported omap B-tree flags')
        if not tree_flags & BTREE_PHYSICAL:
            raise ProbeError('omap B-tree is not physical')
        if node_size != BLOCK_BYTES or key_size != 16 or val_size != 16:
            raise ProbeError('unsupported omap B-tree geometry')
        vals_start = BLOCK_BYTES - 40
    else:
        vals_start = BLOCK_BYTES
    entries = []
    for index in range(nkeys):
        key_offset = u16(block, 56 + index * 4)
        value_offset = u16(block, 56 + index * 4 + 2)
        key_start = keys_start + key_offset
        if key_start + 16 > vals_start:
            raise ProbeError('B-tree key is outside the key area')
        key = block[key_start:key_start + 16]
        if value_offset == BTOFF_INVALID:
            raise ProbeError('omap B-tree entry has no value')
        value_length = 16 if flags & BTNODE_LEAF else 8
        value_start = vals_start - value_offset
        if value_start < keys_start or \
                value_start + value_length > vals_start:
            raise ProbeError('B-tree value is outside the value area')
        value = block[value_start:value_start + value_length]
        entries.append((u64(key, 0), u64(key, 8), value))
    return {'level': level, 'leaf': bool(flags & BTNODE_LEAF),
            'entries': entries, 'paddr': paddr}


def lookup_omap(stream, base, nxsb, tree_oid, oid, checkpoint_xid):
    """Resolve a virtual OID through the container omap at the checkpoint."""
    if tree_oid == 0 or tree_oid >= nxsb['block_count']:
        raise ProbeError('omap tree root is outside the container')
    node = parse_btree_node(stream, base, nxsb['block_count'], tree_oid, True)
    visited = {tree_oid}
    nodes = 1
    while not node['leaf']:
        chosen = None
        for entry_oid, entry_xid, value in node['entries']:
            if (entry_oid, entry_xid) <= (oid, checkpoint_xid):
                chosen = (entry_oid, entry_xid, value)
            else:
                break
        if chosen is None:
            return None
        child = u64(chosen[2], 0)
        if child == 0 or child >= nxsb['block_count']:
            raise ProbeError('omap child is outside the container')
        if child in visited:
            raise ProbeError('omap B-tree cycle')
        visited.add(child)
        nodes += 1
        if nodes > MAX_TREE_NODES:
            raise ProbeError('omap B-tree node limit exceeded')
        node = parse_btree_node(stream, base, nxsb['block_count'], child, False)
    chosen = None
    for entry_oid, entry_xid, value in node['entries']:
        if (entry_oid, entry_xid) <= (oid, checkpoint_xid):
            chosen = (entry_oid, entry_xid, value)
        else:
            break
    if chosen is None or chosen[0] != oid:
        return None
    flags = u32(chosen[2], 0)
    size = u32(chosen[2], 4)
    paddr = u64(chosen[2], 8)
    if flags & ~OMAP_VALID_FLAGS:
        raise ProbeError(f'omap mapping has unknown flags 0x{flags:x}')
    if flags & OMAP_VAL_DELETED:
        return None
    if flags & (OMAP_VAL_ENCRYPTED | OMAP_VAL_NOHEADER):
        raise ProbeError('unsupported encrypted/no-header omap mapping')
    if size != BLOCK_BYTES:
        raise ProbeError(f'unsupported mapped object size {size}')
    if paddr >= nxsb['block_count']:
        raise ProbeError('omap mapping is outside the container')
    return {'xid': chosen[1], 'flags': flags, 'paddr': paddr}


def probe_container(stream, base, size, label, enforce):
    block_zero_raw = read_block(stream, base, 0, size // BLOCK_BYTES,
                                f'{label} container superblock')
    block_zero = parse_nxsb(block_zero_raw, f'{label} container superblock')
    if block_zero['block_count'] * BLOCK_BYTES != size:
        raise ProbeError(
            f'{label} container is {block_zero["block_count"]} blocks, '
            f'partition is {size} bytes')
    chosen = select_checkpoint(stream, base, block_zero, block_zero)
    omap_paddr = chosen['omap_oid']
    if omap_paddr == 0 or omap_paddr >= block_zero['block_count']:
        raise ProbeError('container omap is outside the container')
    omap = read_block(stream, base, omap_paddr, block_zero['block_count'],
                      'container omap')
    omap_type = u32(omap, 24)
    if (omap_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_OMAP:
        raise ProbeError('container omap has the wrong type')
    if (omap_type & OBJ_STORAGETYPE_MASK) != OBJ_PHYSICAL:
        raise ProbeError('container omap is not a physical object')
    if omap_type & (OBJ_NOHEADER | OBJ_ENCRYPTED):
        raise ProbeError('unsupported encrypted/no-header container omap')
    if u64(omap, 16) != chosen['xid']:
        raise ProbeError('container omap xid does not match the checkpoint')
    omap_flags = u32(omap, 32)
    if omap_flags & (OMAP_ENCRYPTING | OMAP_DECRYPTING | OMAP_KEYROLLING):
        raise ProbeError('unsupported in-progress omap conversion')
    tree_type = u32(omap, 40)
    if (tree_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_BTREE or \
            (tree_type & OBJ_STORAGETYPE_MASK) != OBJ_PHYSICAL or \
            tree_type & (OBJ_NOHEADER | OBJ_ENCRYPTED):
        raise ProbeError('unsupported container omap tree type')
    tree_oid = u64(omap, 48)
    volumes = []
    for index, oid in enumerate(chosen['fs_oid']):
        if oid == 0:
            continue
        mapping = lookup_omap(stream, base, block_zero, tree_oid, oid,
                              chosen['xid'])
        if mapping is None:
            raise ProbeError(f'{label}: volume OID {oid} is not mapped')
        apsb = read_block(stream, base, mapping['paddr'],
                          block_zero['block_count'], 'volume superblock')
        if apsb[32:36] != APSB_MAGIC:
            raise ProbeError(f'{label}: mapped OID {oid} is not an APSB')
        if (u32(apsb, 24) & OBJECT_TYPE_MASK) != OBJECT_TYPE_FS:
            raise ProbeError(f'{label}: volume superblock has the wrong type')
        if u64(apsb, 16) != mapping['xid']:
            raise ProbeError(f'{label}: volume xid does not match its mapping')
        raw_uuid = apsb[240:256]
        if not any(raw_uuid):
            raise ProbeError(f'{label}: volume has a zero UUID')
        incompat = u64(apsb, 56)
        role = u16(apsb, 964)
        name = apsb[704:960].split(b'\0', 1)[0]
        try:
            name_text = name.decode('ascii')
        except UnicodeDecodeError:
            raise ProbeError(f'{label}: volume name is not ASCII')
        volumes.append({
            'index': index,
            'oid': oid,
            'xid': mapping['xid'],
            'paddr': mapping['paddr'],
            'uuid': uuid_text(raw_uuid),
            'uuid_bytes': raw_uuid.hex(),
            'role': role,
            'role_name': APFS_VOL_ROLE_NAMES.get(role, f'unknown-{role}'),
            'sealed': bool(incompat & APFS_INCOMPAT_SEALED_VOLUME),
            'fs_flags': u64(apsb, 264),
            'incompatible': incompat,
            'name': name_text,
        })
    if enforce:
        expected = EXPECTED_VOLUME_UUIDS[label]
        matches = [volume for volume in volumes
                   if volume['uuid'] == expected]
        if len(matches) != 1:
            raise ProbeError(
                f'{label}: expected exactly one {expected}, found '
                f'{len(matches)}')
    return {
        'block_size': block_zero['block_size'],
        'block_count': block_zero['block_count'],
        'container_uuid': uuid_text(bytes.fromhex(block_zero['uuid'])),
        'checkpoint_xid': chosen['xid'],
        'checkpoint_paddr': chosen['paddr'],
        'omap_paddr': omap_paddr,
        'tree_paddr': tree_oid,
        'volumes': volumes,
    }


def probe_image(path, enforce=True):
    """Probe both SHNGROOT windows and return their APFS identities."""
    path = Path(path)
    image_bytes = path.stat().st_size
    with path.open('rb') as stream:
        stream.seek(0)
        manifest_sha256 = hashlib.sha256(
            stream.read(MANIFEST_BYTES)).hexdigest()
        windows = parse_manifest(stream, image_bytes)
        labels = {window['label'] for window in windows}
        if labels != set(EXPECTED_VOLUME_UUIDS):
            raise ProbeError(
                f'windows {sorted(labels)} do not match the expected labels '
                f'{sorted(EXPECTED_VOLUME_UUIDS)}')
        probed = []
        for window in windows:
            label = window['label']
            if label == 'OS':
                base, size, _ = parse_gpt(stream, window)
            elif label == 'SystemOS':
                base, size = window['offset'], window['size']
                stream.seek(base + 32)
                if stream.read(4) != NX_MAGIC:
                    raise ProbeError('SystemOS window is not a raw APFS container')
            else:
                raise ProbeError(f'unsupported window label {label}')
            container = probe_container(stream, base, size, label, enforce)
            probed.append({**window, 'container_offset': base,
                           'container_size': size, **container})
        if enforce:
            seen = {}
            for window in probed:
                for volume in window['volumes']:
                    if volume['uuid'] in EXPECTED_VOLUME_UUIDS.values():
                        if volume['uuid'] in seen:
                            raise ProbeError(
                                f'duplicate expected identity {volume["uuid"]} '
                                f'in {seen[volume["uuid"]]} and '
                                f'{window["label"]}')
                        seen[volume['uuid']] = window['label']
    return {
        'image': str(path),
        'image_bytes': image_bytes,
        'manifest_sha256': manifest_sha256,
        'windows': probed,
    }


def report(probe):
    for window in probe['windows']:
        print(f'{window["label"]}: off={window["offset"]} '
              f'size={window["size"]} container=0x'
              f'{window["container_offset"]:x}+{window["container_size"]}')
        print(f'  block={window["block_size"]} '
              f'blocks={window["block_count"]} '
              f'cid={window["container_uuid"]} '
              f'xid={window["checkpoint_xid"]} '
              f'omap={window["omap_paddr"]} tree={window["tree_paddr"]}')
        for volume in window['volumes']:
            print(f'  volume[{volume["index"]}] oid=0x{volume["oid"]:x} '
                  f'xid={volume["xid"]} paddr={volume["paddr"]} '
                  f'uuid={volume["uuid"]} role={volume["role"]}'
                  f'({volume["role_name"]}) sealed={int(volume["sealed"])} '
                  f'name={volume["name"]}')
        expected = EXPECTED_VOLUME_UUIDS[window['label']]
        print(f'  expected={expected}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--json', type=Path)
    parser.add_argument('--no-enforce', action='store_true')
    args = parser.parse_args()
    try:
        probe = probe_image(args.image, enforce=not args.no_enforce)
    except (OSError, ProbeError, struct.error) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    report(probe)
    if args.json is not None:
        args.json.write_text(json.dumps(probe, indent=2) + '\n')
    print('PASS: APFS identities match the expected window UUIDs')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
