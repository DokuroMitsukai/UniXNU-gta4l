#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host regression test for the read-only gta4l APFS userdata probe.

The test builds small synthetic SHNGROOT images with valid and invalid APFS
containers. The controls cover a newer valid checkpoint, a corrupt newest
checkpoint (superblock and map), a stale APSB with the same UUID, a virtual
OID that differs from its physical address, deleted/encrypted/no-header
mappings, missing and duplicate expected volumes, out-of-window pointers,
cyclic and unsupported B-trees, malformed geometry, stale-only candidates,
ambiguous checkpoint xids, the raw/GPT path split and a no-scan fallback.
When the real userdata-root.img is present in the sibling UniXNU checkout,
the test also re-derives its two identities and pinned block locations.
"""
import importlib.util
import struct
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROBE_PATH = ROOT / 'scripts' / 'probe-gta4l-apfs-userdata.py'
REAL_IMAGE = (ROOT.parent / 'UniXNU' / 'out' / 'gta4l' /
              'ipados-17.7.11-21H461' / 'gta4l' / 'userdata' /
              'userdata-root.img')
SPEC = importlib.util.spec_from_file_location('gta4l_apfs_probe', PROBE_PATH)
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)

BLOCK = 4096
OS_UUID = bytes.fromhex('04769d262ca04825b443e2e5f88300f2')
SOS_UUID = bytes.fromhex('8846fe7829104297af97d59bb6f8fc16')
OTHER_UUID = bytes.fromhex('11111111222233334444555566667777')
CONTAINER_UUID = bytes.fromhex('00112233445566778899aabbccddeeff')
GPT_APFS = bytes.fromhex('ef57347c0000aa11aa1100306543ecac')
IMAGE_BYTES = 4 << 20
OS_OFF = 1 << 20
OS_SIZE = 2 << 20
SOS_OFF = 3 << 20
SOS_SIZE = 1 << 20


class Container:
    def __init__(self, block_count=256, uuid=CONTAINER_UUID):
        self.block_count = block_count
        self.uuid = uuid
        self.blocks = [bytearray(BLOCK) for _ in range(block_count)]

    def nxsb(self, paddr, xid, desc_index, desc_len, omap_oid, fs_oids,
             desc_base=4, desc_blocks=4, spaceman_oid=0x50,
             data_base=12, data_blocks=4, block_count=None,
             block_size=BLOCK, incompatible=2):
        block = self.blocks[paddr]
        block[:] = bytes(BLOCK)
        struct.pack_into('<Q', block, 8, 1)
        struct.pack_into('<Q', block, 16, xid)
        struct.pack_into('<I', block, 24, 0x80000001)
        block[32:36] = b'NXSB'
        struct.pack_into('<I', block, 36, block_size)
        struct.pack_into('<Q', block, 40,
                         self.block_count if block_count is None else block_count)
        struct.pack_into('<Q', block, 64, incompatible)
        block[72:88] = self.uuid
        struct.pack_into('<I', block, 104, desc_blocks)
        struct.pack_into('<I', block, 108, data_blocks)
        struct.pack_into('<Q', block, 112, desc_base)
        struct.pack_into('<Q', block, 120, data_base)
        struct.pack_into('<I', block, 136, desc_index)
        struct.pack_into('<I', block, 140, desc_len)
        struct.pack_into('<Q', block, 152, spaceman_oid)
        struct.pack_into('<Q', block, 160, omap_oid)
        struct.pack_into('<I', block, 180, len(fs_oids))
        for index, oid in enumerate(fs_oids):
            struct.pack_into('<Q', block, 184 + index * 8, oid)
        probe.set_checksum(block)

    def cpm(self, paddr, xid, mappings, oid=0x60):
        block = self.blocks[paddr]
        block[:] = bytes(BLOCK)
        struct.pack_into('<Q', block, 8, oid)
        struct.pack_into('<Q', block, 16, xid)
        struct.pack_into('<I', block, 24, 0x4000000c)
        struct.pack_into('<I', block, 36, len(mappings))
        for index, (object_type, size, object_oid, object_paddr) in \
                enumerate(mappings):
            entry = 40 + index * 40
            struct.pack_into('<I', block, entry, object_type)
            struct.pack_into('<I', block, entry + 8, size)
            struct.pack_into('<Q', block, entry + 24, object_oid)
            struct.pack_into('<Q', block, entry + 32, object_paddr)
        probe.set_checksum(block)

    def spaceman(self, paddr, xid, oid=0x50):
        block = self.blocks[paddr]
        block[:] = bytes(BLOCK)
        struct.pack_into('<Q', block, 8, oid)
        struct.pack_into('<Q', block, 16, xid)
        struct.pack_into('<I', block, 24, 0x80000005)
        struct.pack_into('<I', block, 32, BLOCK)
        probe.set_checksum(block)

    def omap(self, paddr, xid, tree_oid, tree_type=0x40000002, flags=1,
             oid=0x60):
        block = self.blocks[paddr]
        block[:] = bytes(BLOCK)
        struct.pack_into('<Q', block, 8, oid)
        struct.pack_into('<Q', block, 16, xid)
        struct.pack_into('<I', block, 24, 0x4000000b)
        struct.pack_into('<I', block, 32, flags)
        struct.pack_into('<I', block, 40, tree_type)
        struct.pack_into('<Q', block, 48, tree_oid)
        probe.set_checksum(block)

    def leaf(self, paddr, xid, entries, root=True, level=0, extra_flags=0,
             oid=0x61, child_oids=None):
        block = self.blocks[paddr]
        block[:] = bytes(BLOCK)
        struct.pack_into('<Q', block, 8, oid)
        struct.pack_into('<Q', block, 16, xid)
        struct.pack_into('<I', block, 24, 0x40000002)
        struct.pack_into('<I', block, 28, 0x0b)
        flags = (probe.BTNODE_ROOT if root else 0) | \
            probe.BTNODE_FIXED_KV_SIZE | extra_flags
        if level == 0:
            flags |= probe.BTNODE_LEAF
        struct.pack_into('<H', block, 32, flags)
        struct.pack_into('<H', block, 34, level)
        struct.pack_into('<I', block, 36, len(entries))
        struct.pack_into('<H', block, 40, 0)
        struct.pack_into('<H', block, 42, 4 * len(entries))
        keys_start = 56 + 4 * len(entries)
        vals_start = BLOCK - 40 if root else BLOCK
        value_size = 16 if level == 0 else 8
        for index, (entry_oid, entry_xid, value) in enumerate(entries):
            struct.pack_into('<H', block, 56 + index * 4, index * 16)
            struct.pack_into('<H', block, 56 + index * 4 + 2,
                             value_size * (index + 1))
            struct.pack_into('<QQ', block, keys_start + index * 16,
                             entry_oid, entry_xid)
            struct.pack_into('<%ds' % value_size,
                             block, vals_start - value_size * (index + 1),
                             value)
        if root:
            struct.pack_into('<IIII', block, BLOCK - 40, 0x12, BLOCK, 16, 16)
            struct.pack_into('<II', block, BLOCK - 24, 16, 16)
            struct.pack_into('<QQ', block, BLOCK - 16, len(entries), 1)
        probe.set_checksum(block)

    def apsb(self, paddr, xid, oid, uuid, role=0, sealed=False,
             name='volume', incompatible_extra=8):
        block = self.blocks[paddr]
        block[:] = bytes(BLOCK)
        struct.pack_into('<Q', block, 8, oid)
        struct.pack_into('<Q', block, 16, xid)
        struct.pack_into('<I', block, 24, 0x0d)
        block[32:36] = b'APSB'
        struct.pack_into('<Q', block, 56,
                         incompatible_extra |
                         (probe.APFS_INCOMPAT_SEALED_VOLUME if sealed else 0))
        block[240:256] = uuid
        struct.pack_into('<Q', block, 264, 1)
        block[704:704 + len(name)] = name.encode()
        struct.pack_into('<H', block, 964, role)
        probe.set_checksum(block)

    def bytes(self):
        return b''.join(bytes(block) for block in self.blocks)


def base_container(label, *, newer=True, block_zero_xid=None,
                   block_zero_old=False, desc_blocks=4, ambiguous=False,
                   duplicate=False, flags=0, paddr_override=None,
                   uuid_override=None, fs_oids=None, leaf_oid=None,
                   tree_type=0x40000002, level=0, child=None, extra_flags=0,
                   block_count=None, block_size=BLOCK,
                   desc_index_override=None, map_xid_override=None,
                   apsb_xid_override=None, no_mapping=False,
                   map_missing_spaceman=False):
    container = Container(block_count=block_count or 256)
    fs_oids = fs_oids or [0x402]
    uuid = uuid_override or (OS_UUID if label == 'OS' else SOS_UUID)
    spaceman_oid = 0x50
    map_oid = 0x60
    old_xid = 11
    new_xid = 12
    if ambiguous:
        desc_blocks = max(desc_blocks, 6)
    if block_zero_old:
        container.nxsb(0, old_xid, 0, 2, omap_oid=30, fs_oids=fs_oids,
                       desc_blocks=desc_blocks, block_count=block_count,
                       block_size=block_size)
    else:
        container.nxsb(0, block_zero_xid or (12 if newer else 11),
                       desc_index_override or (2 if newer else 0), 2,
                       omap_oid=32 if newer else 30, fs_oids=fs_oids,
                       desc_blocks=desc_blocks, block_count=block_count,
                       block_size=block_size)
    container.cpm(4, old_xid, [(0x80000005, BLOCK, spaceman_oid, 20)])
    container.nxsb(5, old_xid, 0, 2, omap_oid=30, fs_oids=fs_oids,
                   desc_blocks=desc_blocks, block_count=block_count,
                   block_size=block_size)
    container.spaceman(20, old_xid)
    container.omap(30, old_xid, 31)
    container.leaf(31, old_xid,
                   [(0x402, old_xid,
                     struct.pack('<IIQ', flags, BLOCK, 40))])
    container.apsb(40, old_xid, 0x402, uuid, role=1 if label == 'OS' else 0,
                   sealed=label == 'SystemOS', name=label)
    if newer:
        map_xid = map_xid_override if map_xid_override is not None else new_xid
        apsb_xid = apsb_xid_override if apsb_xid_override is not None else new_xid
        if map_missing_spaceman:
            mappings = [(0x80000011, BLOCK, 0x401, 22)]
        else:
            mappings = [(0x80000005, BLOCK, spaceman_oid, 21)]
        container.cpm(6, map_xid, mappings)
        container.nxsb(7, new_xid, 2, 2, omap_oid=32, fs_oids=fs_oids,
                       desc_blocks=desc_blocks, block_count=block_count,
                       block_size=block_size)
        container.spaceman(21, new_xid)
        container.omap(32, new_xid, 33, tree_type=tree_type)
        target = paddr_override if paddr_override is not None else 41
        if no_mapping:
            entries = [(0x999, new_xid,
                        struct.pack('<IIQ', flags, BLOCK, target))]
        else:
            entries = [(leaf_oid or 0x402, new_xid,
                        struct.pack('<IIQ', flags, BLOCK, target))]
        if duplicate:
            entries.append((0x403, new_xid,
                            struct.pack('<IIQ', flags, BLOCK, 42)))
        if level:
            child_value = struct.pack('<Q', child if child is not None else 33)
            container.leaf(33, new_xid, [(leaf_oid or 0x402, new_xid,
                                         child_value)],
                           root=True, level=1, extra_flags=extra_flags)
            container.leaf(34, new_xid, entries, root=False, level=0,
                           oid=0x62)
        else:
            container.leaf(33, new_xid, entries, root=True, level=0,
                           extra_flags=extra_flags)
        container.apsb(41, apsb_xid, 0x402, uuid,
                       role=1 if label == 'OS' else 0,
                       sealed=label == 'SystemOS', name=label)
        if duplicate:
            container.apsb(42, new_xid, 0x403, uuid,
                           role=1 if label == 'OS' else 0,
                           sealed=label == 'SystemOS', name=label + '-dup')
    if ambiguous:
        container.cpm(8, new_xid, [(0x80000005, BLOCK, spaceman_oid, 23)])
        container.nxsb(9, new_xid, 4, 2, omap_oid=34, fs_oids=fs_oids,
                       desc_blocks=6, block_count=block_count,
                       block_size=block_size)
        container.spaceman(23, new_xid)
        container.omap(34, new_xid, 35)
        container.leaf(35, new_xid,
                       [(0x402, new_xid,
                         struct.pack('<IIQ', flags, BLOCK, 43))])
        container.apsb(43, new_xid, 0x402, uuid,
                       role=1 if label == 'OS' else 0,
                       sealed=label == 'SystemOS', name=label + '-alt')
    return container


def write_gpt(window, container_bytes, first_lba=34):
    window_size = len(window)
    entries = bytearray(4 * 128)
    entries[0:16] = GPT_APFS
    entries[16:32] = bytes(range(16))
    struct.pack_into('<QQ', entries, 32, first_lba,
                     first_lba + container_bytes // 512 - 1)
    name = 'APFS'.encode('utf-16le')
    entries[56:56 + len(name)] = name
    entries_crc = zlib.crc32(bytes(entries)) & 0xffffffff
    header = bytearray(512)
    header[0:8] = b'EFI PART'
    struct.pack_into('<I', header, 8, 0x10000)
    struct.pack_into('<I', header, 12, 92)
    struct.pack_into('<Q', header, 24, 1)
    struct.pack_into('<Q', header, 32, window_size // 512 - 1)
    struct.pack_into('<Q', header, 40, first_lba)
    struct.pack_into('<Q', header, 48, window_size // 512 - 34)
    header[56:72] = bytes(reversed(range(16)))
    struct.pack_into('<Q', header, 72, 2)
    struct.pack_into('<I', header, 80, 4)
    struct.pack_into('<I', header, 84, 128)
    struct.pack_into('<I', header, 88, entries_crc)
    struct.pack_into('<I', header, 16, 0)
    struct.pack_into('<I', header, 16,
                     zlib.crc32(bytes(header[:92])) & 0xffffffff)
    window[512:1024] = header
    window[1024:1024 + len(entries)] = entries


def write_image(path, os_container, sos_container, *, gpt_os=True,
                gpt_sos=False, sos_size=SOS_SIZE):
    total = max(IMAGE_BYTES, SOS_OFF + sos_size)
    image = bytearray(total)
    header = bytearray(BLOCK)
    header[0:8] = b'SHNGROOT'
    struct.pack_into('<I', header, 8, 1)
    struct.pack_into('<I', header, 12, 2)
    struct.pack_into('<Q', header, 16, total)
    for index, (name, offset, size) in enumerate((
            ('OS', OS_OFF, OS_SIZE), ('SystemOS', SOS_OFF, sos_size))):
        entry = 24 + index * 128
        header[entry:entry + len(name)] = name.encode()
        struct.pack_into('<Q', header, entry + 32, offset)
        struct.pack_into('<Q', header, entry + 40, size)
        struct.pack_into('<I', header, entry + 48, 512)
        struct.pack_into('<I', header, entry + 52, 1)
    header[4064:4096] = __import__('hashlib').sha256(
        bytes(header[:4064])).digest()
    image[0:BLOCK] = header
    os_window = bytearray(OS_SIZE)
    sos_window = bytearray(sos_size)
    if gpt_os:
        write_gpt(os_window, len(os_container))
        os_window[34 * 512:34 * 512 + len(os_container)] = os_container
    else:
        os_window[:len(os_container)] = os_container
    if gpt_sos:
        write_gpt(sos_window, len(sos_container))
        sos_window[34 * 512:34 * 512 + len(sos_container)] = sos_container
    else:
        sos_window[:len(sos_container)] = sos_container
    image[OS_OFF:OS_OFF + OS_SIZE] = os_window
    image[SOS_OFF:SOS_OFF + sos_size] = sos_window
    Path(path).write_bytes(bytes(image))


def build(path, *, sos_window_size=SOS_SIZE, **os_options):
    sos_options = dict(os_options.pop('sos', {}))
    gpt_os = os_options.pop('gpt_os', True)
    gpt_sos = sos_options.pop('gpt_sos', False)
    os_container = base_container('OS', **os_options).bytes()
    sos_container = base_container('SystemOS', **sos_options).bytes()
    write_image(path, os_container, sos_container, gpt_os=gpt_os,
                gpt_sos=gpt_sos, sos_size=sos_window_size)


def expect_error(path, needle, **options):
    try:
        probe.probe_image(path, enforce=True)
    except probe.ProbeError as error:
        assert needle in str(error), (needle, str(error))
        return
    raise AssertionError(f'{needle} was accepted')


def mutate_block(path, window_offset, block, offset, data):
    with open(path, 'r+b') as stream:
        stream.seek(window_offset + block * BLOCK + offset)
        stream.write(data)


def checksum_block(path, window_offset, block):
    with open(path, 'r+b') as stream:
        stream.seek(window_offset + block * BLOCK)
        data = bytearray(stream.read(BLOCK))
        probe.set_checksum(data)
        stream.seek(window_offset + block * BLOCK)
        stream.write(data)


def main():
    with tempfile.TemporaryDirectory(prefix='gta4l-apfs-probe-') as raw:
        work = Path(raw)
        image = work / 'base.img'
        build(image)
        result = probe.probe_image(image)
        assert result['windows'][0]['label'] == 'OS'
        assert result['windows'][1]['label'] == 'SystemOS'
        os_window, sos_window = result['windows']
        assert os_window['checkpoint_xid'] == 12
        assert os_window['volumes'][0]['paddr'] == 41
        assert os_window['volumes'][0]['uuid'] == \
            probe.EXPECTED_VOLUME_UUIDS['OS']
        assert os_window['volumes'][0]['oid'] == 0x402
        assert sos_window['volumes'][0]['paddr'] == 41
        assert sos_window['volumes'][0]['uuid'] == \
            probe.EXPECTED_VOLUME_UUIDS['SystemOS']
        assert sos_window['volumes'][0]['sealed'] is True
        assert os_window['container_offset'] == OS_OFF + 34 * 512
        print('ok: base image resolves both identities through the omap')

        stale = work / 'stale.img'
        build(stale)
        stale_result = probe.probe_image(stale)
        assert len(stale_result['windows'][0]['volumes']) == 1
        assert stale_result['windows'][0]['volumes'][0]['paddr'] == 41
        print('ok: the stale same-UUID APSB at block 40 is ignored')

        corrupt_super = work / 'corrupt-super.img'
        build(corrupt_super, block_zero_xid=11, block_zero_old=True)
        mutate_block(corrupt_super, OS_OFF + 34 * 512, 7, 100, b'\xff')
        fallback = probe.probe_image(corrupt_super)
        assert fallback['windows'][0]['checkpoint_xid'] == 11
        assert fallback['windows'][0]['volumes'][0]['paddr'] == 40
        print('ok: a corrupt newest superblock falls back to the older checkpoint')

        corrupt_map = work / 'corrupt-map.img'
        build(corrupt_map, block_zero_xid=11, block_zero_old=True)
        mutate_block(corrupt_map, OS_OFF + 34 * 512, 6, 100, b'\xff')
        fallback = probe.probe_image(corrupt_map)
        assert fallback['windows'][0]['checkpoint_xid'] == 11
        print('ok: a corrupt newest checkpoint map falls back')

        missing_map = work / 'missing-map.img'
        build(missing_map, block_zero_xid=11, block_zero_old=True, map_missing_spaceman=True)
        fallback = probe.probe_image(missing_map)
        assert fallback['windows'][0]['checkpoint_xid'] == 11
        print('ok: a checkpoint map without the spaceman is rejected')

        deleted = work / 'deleted.img'
        build(deleted, flags=probe.OMAP_VAL_DELETED)
        expect_error(deleted, 'not mapped')
        print('ok: a deleted omap entry is not accepted')

        encrypted = work / 'encrypted.img'
        build(encrypted, flags=probe.OMAP_VAL_ENCRYPTED)
        expect_error(encrypted, 'encrypted/no-header')
        print('ok: an encrypted omap entry is rejected')

        noheader = work / 'noheader.img'
        build(noheader, flags=probe.OMAP_VAL_NOHEADER)
        expect_error(noheader, 'encrypted/no-header')
        print('ok: a no-header omap entry is rejected')

        outside = work / 'outside.img'
        build(outside, paddr_override=300)
        expect_error(outside, 'outside the container')
        print('ok: an out-of-window omap pointer is rejected')

        missing = work / 'missing.img'
        build(missing, uuid_override=OTHER_UUID)
        expect_error(missing, 'expected exactly one')
        print('ok: a missing expected volume is rejected')

        duplicate = work / 'duplicate.img'
        build(duplicate, duplicate=True, fs_oids=[0x402, 0x403])
        expect_error(duplicate, 'expected exactly one')
        print('ok: a duplicate expected volume is rejected')

        across = work / 'across.img'
        build(across, sos={'uuid_override': OS_UUID})
        expect_error(across, 'expected exactly one')
        print('ok: a duplicate expected identity across windows is rejected')

        noscan = work / 'noscan.img'
        build(noscan, no_mapping=True)
        with open(noscan, 'r+b') as stream:
            stream.seek(SOS_OFF + 50 * BLOCK)
            block = bytearray(stream.read(BLOCK))
            probe.set_checksum(block)
            stream.seek(SOS_OFF + 50 * BLOCK)
            stream.write(block)
        expect_error(noscan, 'not mapped')
        print('ok: an unmapped APSB is not found by scanning')

        cycle = work / 'cycle.img'
        build(cycle, level=1, child=33)
        expect_error(cycle, 'cycle')
        print('ok: a cyclic omap B-tree is rejected')

        child_outside = work / 'child-outside.img'
        build(child_outside, level=1, child=300)
        expect_error(child_outside, 'outside the container')
        print('ok: an out-of-window B-tree child is rejected')

        variable = work / 'variable.img'
        build(variable, extra_flags=0, level=0)
        with open(variable, 'r+b') as stream:
            stream.seek(OS_OFF + 34 * 512 + 33 * BLOCK + 32)
            flags = struct.unpack('<H', stream.read(2))[0]
            stream.seek(OS_OFF + 34 * 512 + 33 * BLOCK + 32)
            stream.write(struct.pack('<H', flags & ~probe.BTNODE_FIXED_KV_SIZE))
        checksum_block(variable, OS_OFF + 34 * 512, 33)
        expect_error(variable, 'variable-size')
        print('ok: a variable-size B-tree is rejected')

        hashed = work / 'hashed.img'
        build(hashed, extra_flags=probe.BTNODE_HASHED)
        expect_error(hashed, 'hashed/no-header')
        print('ok: a hashed B-tree is rejected')

        physical = work / 'physical.img'
        build(physical, tree_type=0x2)
        expect_error(physical, 'tree type')
        print('ok: a non-physical omap tree is rejected')

        size = work / 'size.img'
        build(size)
        with open(size, 'r+b') as stream:
            stream.seek(OS_OFF + 34 * 512)
            block = bytearray(stream.read(BLOCK))
            struct.pack_into('<Q', block, 40, 255)
            probe.set_checksum(block)
            stream.seek(OS_OFF + 34 * 512)
            stream.write(block)
        expect_error(size, 'partition is')
        print('ok: a container/partition size mismatch is rejected')

        block_size = work / 'block-size.img'
        build(block_size, block_size=16384)
        expect_error(block_size, 'block size')
        print('ok: a non-4096-byte container is rejected')

        desc = work / 'desc.img'
        build(desc, desc_index_override=4)
        expect_error(desc, 'descriptor range')
        print('ok: an invalid descriptor index is rejected')

        map_xid = work / 'map-xid.img'
        build(map_xid, block_zero_xid=11, block_zero_old=True, map_xid_override=11)
        fallback = probe.probe_image(map_xid)
        assert fallback['windows'][0]['checkpoint_xid'] == 11
        print('ok: a checkpoint map with the wrong xid is skipped')

        apsb_xid = work / 'apsb-xid.img'
        build(apsb_xid, apsb_xid_override=11)
        expect_error(apsb_xid, 'volume xid')
        print('ok: an APSB whose xid disagrees with its mapping is rejected')

        stale_only = work / 'stale-only.img'
        build(stale_only, block_zero_xid=13)
        expect_error(stale_only, 'newer than the checkpoint ring')
        print('ok: a stale-only descriptor ring is rejected')

        ambiguous = work / 'ambiguous.img'
        build(ambiguous, ambiguous=True)
        expect_error(ambiguous, 'ambiguous checkpoint')
        print('ok: two same-xid checkpoints with different identities are rejected')

        gpt_crc = work / 'gpt-crc.img'
        build(gpt_crc)
        mutate_block(gpt_crc, OS_OFF, 0, 512 + 20, b'\xff')
        expect_error(gpt_crc, 'GPT header CRC')
        print('ok: a GPT header CRC mismatch is rejected')

        no_gpt = work / 'no-gpt.img'
        build(no_gpt, gpt_os=False)
        expect_error(no_gpt, 'no GPT header')
        print('ok: an OS window without GPT is rejected')

        sos_gpt = work / 'sos-gpt.img'
        build(sos_gpt, sos={'gpt_sos': True}, sos_window_size=2 << 20)
        expect_error(sos_gpt, 'raw APFS container')
        print('ok: a SystemOS window with GPT is rejected')

        if REAL_IMAGE.is_file():
            real = probe.probe_image(REAL_IMAGE)
            os_real, sos_real = real['windows']
            assert os_real['checkpoint_xid'] == 418
            assert os_real['volumes'][0]['paddr'] == 15896
            assert os_real['omap_paddr'] == 15897
            assert os_real['tree_paddr'] == 15898
            assert os_real['container_uuid'] == \
                '50F8A67F-AFC1-4A1B-BCBB-82162674C67F'
            assert sos_real['checkpoint_xid'] == 14
            assert sos_real['volumes'][0]['paddr'] == 782661
            assert sos_real['omap_paddr'] == 782662
            assert sos_real['tree_paddr'] == 782663
            assert sos_real['container_uuid'] == \
                'E886F82C-EE56-4B52-AF5F-E70BF46320C4'
            print('ok: the real userdata image re-derives both identities')
        else:
            print('SKIP: the real userdata image is not present')
    print('PASS: APFS userdata probe synthetic and real-image controls')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        raise SystemExit(1)
