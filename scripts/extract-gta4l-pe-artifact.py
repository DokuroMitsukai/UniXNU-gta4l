#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Bound and inspect the pinned PixelExperience gta4l release archive.

The extractor reads only the release members needed to reproduce the storage
provenance record: the boot image, the DTBO table, the dynamic-partition list,
the vendor transfer list and the OTA metadata.  It parses the Android boot
image v2 header, the embedded FDT sequence and the DTBO table with explicit
size, alignment and block bounds, then writes the derived kernel, ramdisk,
device trees, overlays, kernel config and an extraction report to a new
directory.

It never executes archive content, never writes into the archive and never
reuses an existing output directory.  With ``--pinned`` it verifies the
release and output hashes recorded in docs/pe-storage-stack-provenance.md.
Without an expectation it still performs the structural checks.
"""
import argparse
import hashlib
import json
import os
import shutil
import stat
import struct
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

BOOT_MAGIC = b'ANDROID!'
BOOT_HEADER_V1 = 1648
BOOT_HEADER_V2 = 1660
FDT_MAGIC = 0xd00dfeed
DTBO_MAGIC = 0xd7b7ab1e
MAX_ZIP_MEMBER = 256 * 1024 * 1024
MAX_METADATA = 4096
MAX_LIST = 64 * 1024
MAX_IMAGE = 128 * 1024 * 1024
MAX_CONFIG = 4 * 1024 * 1024
MAX_FDT_COUNT = 4096
MAX_DTBO_ENTRIES = 4096

PINNED_ARCHIVE = {
    'size': 1_420_855_324,
    'sha256': '3adbec9c4066b0efe571274346c710162d5692dd184e513d7cb5276379cc7605',
}
PINNED_OUTPUTS = {
    'boot.img': (100_663_296, '8b19ade181f62ca2786881dfbb9d01e19b723e208d00ab310ef7253e03880d22'),
    'kernel.gz': (15_144_947, 'eb3cb68f80646ce0d573e3a29e85ed34cb556b232edfdacc410d1d4f1dc558da'),
    'Image': (33_939_968, '59b1f22091610aeb3b69eb7a25abac11343dcf1073aa5b8b9fb26e6697d11614'),
    'ramdisk': (680_425, 'e3809019bb3eb39ae08e6eede76ec53ba3877d7ecc2bbdf9d8247ac1a03a945f'),
    'dtb.img': (3_950_482, '32414e6f43b5a0bd42b1fb3cd20436f1e16095d4390bdaed151f3fe995b4112b'),
    'dtbo.img': (25_165_824, '430361292528202a0d4ea098d3c5b1b7cca209c4e60057201f49147dc3a69e7b'),
    'kernel.config': (154_244, '0dff1387890d6bf08b021865e9612c38a259b209efc7d28f81ed38c982502280'),
    'dynamic_partitions_op_list': (843, 'ca4a6d785e2961602dc7ec6508a7ad6a4c3b9b3763d87f3b2918812c84ca8201'),
    'vendor.transfer.list': (2444, '96f925999a6a07261216847fc10a94d6dfbcfa75ae375b06ff23e325dd31f1c0'),
    'metadata.txt': (294, '48b7a9c08ede9e6700a6add9bbae492ad1a847c5a3e8c5a2116ba443badbe56d'),
    'dtbs/00.dtb': (316_095, '5c4c0599ff430bec71de6bb75663eb857c30ae368823d55054fd9a90e8eb77f8'),
    'dtbos/00-00000000-00000000.dtbo': (545_807, '7781256875f7efab9e0fd2d6d2e04a55a36df92be500d41c74567c4e0c0ebda5'),
}

SELECTED = {
    'boot.img': MAX_ZIP_MEMBER,
    'dtbo.img': MAX_ZIP_MEMBER,
    'dynamic_partitions_op_list': MAX_LIST,
    'vendor.transfer.list': MAX_LIST,
    'META-INF/com/android/metadata': MAX_METADATA,
}


class ExtractionError(Exception):
    pass


def need(condition, message):
    if not condition:
        raise ExtractionError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def round_up(value, alignment):
    return ((value + alignment - 1) // alignment) * alignment


def validate_member_name(name):
    need(name and '\\' not in name, f'unsafe ZIP member name: {name!r}')
    path = Path(name)
    need(not path.is_absolute(), f'absolute ZIP member name: {name!r}')
    need('..' not in path.parts, f'parent traversal in ZIP member: {name!r}')
    need(name == str(path), f'non-canonical ZIP member name: {name!r}')


def archive_members(archive):
    table = {}
    for info in archive.infolist():
        validate_member_name(info.filename)
        need(info.filename not in table, f'duplicate ZIP member: {info.filename}')
        mode = (info.external_attr >> 16) & 0o170000
        need(mode != stat.S_IFLNK, f'symlink ZIP member: {info.filename}')
        need(not (info.flag_bits & 0x1), f'encrypted ZIP member: {info.filename}')
        table[info.filename] = info
    return table


def read_member(archive, table, name, limit):
    info = table.get(name)
    need(info is not None, f'missing ZIP member: {name}')
    need(info.file_size <= limit, f'{name}: member is {info.file_size} bytes, limit {limit}')
    try:
        data = archive.read(info)
    except (zipfile.BadZipFile, RuntimeError, zlib.error) as error:
        raise ExtractionError(f'{name}: cannot read member: {error}') from None
    need(len(data) == info.file_size, f'{name}: read {len(data)} of {info.file_size} bytes')
    return data


def gunzip(data, limit, label):
    try:
        stream = zlib.decompressobj(16 + zlib.MAX_WBITS)
        output = stream.decompress(data, limit + 1)
        need(len(output) <= limit and not stream.unconsumed_tail,
             f'{label}: decompressed data exceeds {limit} bytes')
        output += stream.flush()
        need(len(output) <= limit, f'{label}: decompressed data exceeds {limit} bytes')
        need(stream.eof, f'{label}: truncated gzip stream')
        need(not stream.unused_data, f'{label}: trailing bytes after gzip stream')
    except zlib.error as error:
        raise ExtractionError(f'{label}: invalid gzip stream: {error}') from None
    return output


def parse_boot(data):
    need(len(data) >= 1680, f'boot.img: {len(data)} bytes is too short')
    need(data[:8] == BOOT_MAGIC, 'boot.img: bad ANDROID! magic')
    fields = struct.unpack_from('<10I', data, 8)
    (kernel_size, kernel_addr, ramdisk_size, ramdisk_addr,
     second_size, second_addr, tags_addr, page_size,
     header_version, os_version) = fields
    need(header_version in (1, 2), f'boot.img: unsupported header version {header_version}')
    need(2048 <= page_size <= 65536 and page_size & (page_size - 1) == 0,
         f'boot.img: invalid page size {page_size}')
    need(len(data) >= 608, 'boot.img: truncated fixed header')
    name = data[48:64].rstrip(b'\0').decode('utf-8', 'replace')
    cmdline = data[64:576].rstrip(b'\0').decode('utf-8', 'replace')

    if header_version == 1:
        header_size = struct.unpack_from('<I', data, 1644)[0]
        need(header_size == BOOT_HEADER_V1, f'boot.img: header_size {header_size} for v1')
        recovery_dtbo_size = struct.unpack_from('<I', data, 1632)[0]
        recovery_dtbo_offset = struct.unpack_from('<Q', data, 1636)[0]
        dtb_size = dtb_addr = 0
    else:
        need(len(data) >= BOOT_HEADER_V2, 'boot.img: truncated v2 header')
        header_size = struct.unpack_from('<I', data, 1644)[0]
        need(header_size == BOOT_HEADER_V2, f'boot.img: header_size {header_size} for v2')
        recovery_dtbo_size = struct.unpack_from('<I', data, 1632)[0]
        recovery_dtbo_offset = struct.unpack_from('<Q', data, 1636)[0]
        dtb_size = struct.unpack_from('<I', data, 1648)[0]
        dtb_addr = struct.unpack_from('<Q', data, 1652)[0]

    kernel_offset = page_size
    ramdisk_offset = kernel_offset + round_up(kernel_size, page_size)
    second_offset = ramdisk_offset + round_up(ramdisk_size, page_size)
    recovery_offset = second_offset + round_up(second_size, page_size)
    if recovery_dtbo_size:
        need(recovery_dtbo_offset == recovery_offset,
             f'boot.img: recovery_dtbo_offset {recovery_dtbo_offset} != {recovery_offset}')
    else:
        need(recovery_dtbo_offset == 0,
             f'boot.img: empty recovery DTBO has nonzero offset {recovery_dtbo_offset}')
    dtb_offset = recovery_offset + round_up(recovery_dtbo_size, page_size)
    end = dtb_offset + dtb_size
    need(end <= len(data), f'boot.img: sections end at {end}, file is {len(data)} bytes')
    for label, offset in (('kernel', kernel_offset), ('ramdisk', ramdisk_offset),
                          ('second', second_offset), ('recovery_dtbo', recovery_offset),
                          ('dtb', dtb_offset)):
        need(offset % page_size == 0, f'boot.img: {label} offset {offset} is not page aligned')

    return {
        'header_version': header_version,
        'header_size': header_size,
        'page_size': page_size,
        'os_version': f'0x{os_version:08x}',
        'name': name,
        'cmdline': cmdline,
        'kernel_size': kernel_size,
        'kernel_addr': f'0x{kernel_addr:x}',
        'ramdisk_size': ramdisk_size,
        'ramdisk_addr': f'0x{ramdisk_addr:x}',
        'second_size': second_size,
        'second_addr': f'0x{second_addr:x}',
        'tags_addr': f'0x{tags_addr:x}',
        'recovery_dtbo_size': recovery_dtbo_size,
        'recovery_dtbo_offset': recovery_dtbo_offset,
        'dtb_size': dtb_size,
        'dtb_addr': f'0x{dtb_addr:x}',
        'sections_end': end,
        'trailing_bytes': len(data) - end,
        'kernel': data[kernel_offset:kernel_offset + kernel_size],
        'ramdisk': data[ramdisk_offset:ramdisk_offset + ramdisk_size],
        'dtb': data[dtb_offset:dtb_offset + dtb_size],
    }


def fdt_images(data, label):
    images = []
    offset = 0
    while offset < len(data):
        need(len(data) - offset >= 40, f'{label}: truncated FDT header at {offset}')
        (magic, size, off_struct, off_strings, off_rsvmap, version,
         last_comp, boot_cpuid, size_strings, size_struct) = struct.unpack_from('>10I', data, offset)
        need(magic == FDT_MAGIC, f'{label}: bad FDT magic at {offset}')
        need(size >= 40, f'{label}: FDT at {offset} has totalsize {size}')
        need(offset + size <= len(data), f'{label}: FDT at {offset} exceeds the file')
        need(off_struct >= 40 and off_strings >= 40 and off_rsvmap >= 40,
             f'{label}: FDT at {offset} has a block inside the header')
        need(off_struct + size_struct <= size,
             f'{label}: FDT at {offset} struct block exceeds totalsize')
        need(off_strings + size_strings <= size,
             f'{label}: FDT at {offset} strings block exceeds totalsize')
        need(off_rsvmap + 16 <= size,
             f'{label}: FDT at {offset} reserve map exceeds totalsize')
        need(version >= 16 and last_comp <= 17,
             f'{label}: FDT at {offset} has unsupported version {version}')
        images.append(data[offset:offset + size])
        offset += size
        need(len(images) <= MAX_FDT_COUNT, f'{label}: too many FDT images')
    need(images, f'{label}: no FDT images')
    return images


def parse_dtbo(data):
    need(len(data) >= 32, f'dtbo.img: {len(data)} bytes is too short')
    (magic, total, header_size, entry_size, count,
     entries_offset, page_size, version) = struct.unpack_from('>8I', data, 0)
    need(magic == DTBO_MAGIC, 'dtbo.img: bad DTBO magic')
    need(header_size >= 32, f'dtbo.img: header_size {header_size}')
    need(entry_size >= 32, f'dtbo.img: entry_size {entry_size}')
    need(2048 <= page_size <= 65536 and page_size & (page_size - 1) == 0,
         f'dtbo.img: invalid page size {page_size}')
    need(count <= MAX_DTBO_ENTRIES, f'dtbo.img: too many entries ({count})')
    table_end = entries_offset + count * entry_size
    need(entries_offset >= header_size, f'dtbo.img: entries offset {entries_offset}')
    need(table_end <= len(data), f'dtbo.img: entry table exceeds the file')
    need(total <= len(data), f'dtbo.img: table total {total} exceeds the file')
    need(total >= table_end, f'dtbo.img: table total {total} is below the entry table')

    entries = []
    ranges = []
    for index in range(count):
        size, offset, identifier, revision = struct.unpack_from(
            '>4I', data, entries_offset + index * entry_size)
        need(size > 0, f'dtbo.img: entry {index} is empty')
        need(offset >= table_end, f'dtbo.img: entry {index} overlaps the table')
        need(offset + size <= total, f'dtbo.img: entry {index} exceeds the table total')
        need(offset + size <= len(data), f'dtbo.img: entry {index} exceeds the file')
        ranges.append((offset, offset + size, index))
        entries.append({
            'index': index,
            'size': size,
            'offset': offset,
            'id': identifier,
            'revision': revision,
            'name': f'{index:02d}-{identifier:08x}-{revision:08x}.dtbo',
            'data': data[offset:offset + size],
        })
    ordered = sorted(ranges)
    for previous, current in zip(ordered, ordered[1:]):
        need(previous[1] <= current[0],
             f'dtbo.img: entries {previous[2]} and {current[2]} overlap')
    return {
        'total': total,
        'header_size': header_size,
        'entry_size': entry_size,
        'entries_offset': entries_offset,
        'page_size': page_size,
        'version': version,
        'entries': entries,
    }


def find_ikconfig(image):
    start = image.find(b'IKCFG_ST')
    need(start >= 0, 'Image: IKCFG_ST marker is missing')
    end = image.find(b'IKCFG_ED', start + 8)
    need(end > start + 8, 'Image: IKCFG_ED marker is missing')
    blob = image[start + 8:end]
    need(blob[:2] == b'\x1f\x8b', 'Image: embedded config is not gzip')
    return start, end, gunzip(blob, MAX_CONFIG, 'Image: embedded config')


def write_bytes(root, relative, data, outputs):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    outputs[relative] = {'size': len(data), 'sha256': sha256(data)}


def expectation(path):
    try:
        document = json.loads(Path(path).read_text())
    except (OSError, ValueError) as error:
        raise ExtractionError(f'{path}: cannot read expectation: {error}') from None
    need(isinstance(document, dict) and document, f'{path}: expected a nonempty object')
    result = {}
    for name, value in document.items():
        validate_member_name(name)
        need(isinstance(value, dict), f'{path}: {name} must be an object')
        need(isinstance(value.get('size'), int) and value['size'] >= 0,
             f'{path}: {name}: invalid size')
        need(isinstance(value.get('sha256'), str) and len(value['sha256']) == 64,
             f'{path}: {name}: invalid sha256')
        result[name] = (value['size'], value['sha256'])
    return result


def check_expectation(outputs, expected, label):
    for name, (size, digest) in sorted(expected.items()):
        need(name in outputs, f'{label}: missing expected output {name}')
        actual = outputs[name]
        need(actual['size'] == size,
             f'{label}: {name} is {actual["size"]} bytes, expected {size}')
        need(actual['sha256'] == digest,
             f'{label}: {name} sha256 {actual["sha256"]} != {digest}')


def extract(archive_path, output_path, expected):
    archive_path = Path(archive_path)
    output_path = Path(output_path)
    need(archive_path.is_file(), f'archive not found: {archive_path}')
    need(not output_path.exists(), f'output already exists: {output_path}')
    output_path.parent.mkdir(parents=True, exist_ok=True)

    archive_digest = sha256_file(archive_path)
    if expected is not None and 'archive' in expected:
        need(archive_digest == expected['archive'][1],
             f'archive sha256 {archive_digest} != {expected["archive"][1]}')

    temporary = Path(tempfile.mkdtemp(prefix=f'.{output_path.name}.', dir=output_path.parent))
    try:
        with zipfile.ZipFile(archive_path) as archive:
            table = archive_members(archive)
            members = {name: read_member(archive, table, name, limit)
                       for name, limit in SELECTED.items()}

        boot = parse_boot(members['boot.img'])
        fdt_images(boot['dtb'], 'boot dtb')
        dtbo = parse_dtbo(members['dtbo.img'])
        for entry in dtbo['entries']:
            images = fdt_images(entry['data'], f'dtbo entry {entry["index"]}')
            need(len(images) == 1, f'dtbo entry {entry["index"]}: expected one FDT')
        image = gunzip(boot['kernel'], MAX_IMAGE, 'kernel.gz')
        config_start, config_end, config = find_ikconfig(image)
        metadata = members['META-INF/com/android/metadata']
        need(b'pre-device=gta4l' in metadata, 'metadata: pre-device is not gta4l')

        outputs = {}
        write_bytes(temporary, 'boot.img', members['boot.img'], outputs)
        write_bytes(temporary, 'dtbo.img', members['dtbo.img'], outputs)
        write_bytes(temporary, 'dynamic_partitions_op_list',
                    members['dynamic_partitions_op_list'], outputs)
        write_bytes(temporary, 'vendor.transfer.list',
                    members['vendor.transfer.list'], outputs)
        write_bytes(temporary, 'metadata.txt', metadata, outputs)
        write_bytes(temporary, 'kernel.gz', boot['kernel'], outputs)
        write_bytes(temporary, 'Image', image, outputs)
        write_bytes(temporary, 'ramdisk', boot['ramdisk'], outputs)
        write_bytes(temporary, 'dtb.img', boot['dtb'], outputs)
        write_bytes(temporary, 'kernel.config', config, outputs)
        dtbs = fdt_images(boot['dtb'], 'boot dtb')
        for index, data in enumerate(dtbs):
            write_bytes(temporary, f'dtbs/{index:02d}.dtb', data, outputs)
        for entry in dtbo['entries']:
            write_bytes(temporary, f'dtbos/{entry["name"]}', entry['data'], outputs)

        report = {
            'archive': {
                'name': archive_path.name,
                'size': archive_path.stat().st_size,
                'sha256': archive_digest,
            },
            'boot': {key: value for key, value in boot.items()
                     if key not in ('kernel', 'ramdisk', 'dtb')},
            'dtb': {'count': len(dtbs), 'bytes': sum(len(data) for data in dtbs)},
            'dtbo': {
                'count': len(dtbo['entries']),
                'total': dtbo['total'],
                'header_size': dtbo['header_size'],
                'entry_size': dtbo['entry_size'],
                'entries_offset': dtbo['entries_offset'],
                'page_size': dtbo['page_size'],
                'version': dtbo['version'],
                'payload_bytes': sum(entry['size'] for entry in dtbo['entries']),
            },
            'ikconfig': {
                'start': config_start,
                'end': config_end,
                'size': len(config),
            },
            'outputs': outputs,
        }
        encoded = (json.dumps(report, indent=2, sort_keys=True) + '\n').encode()
        (temporary / 'extraction.json').write_bytes(encoded)

        if expected is not None:
            check_expectation(outputs, {name: value for name, value in expected.items()
                                        if name != 'archive'}, 'expected')
        os.replace(temporary, output_path)
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise

    outputs['extraction.json'] = {'size': len(encoded), 'sha256': sha256(encoded)}
    return output_path, report, outputs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--archive', required=True, help='the release ZIP')
    parser.add_argument('--output', required=True, help='new output directory')
    parser.add_argument('--pinned', action='store_true',
                        help='verify the pinned release and output hashes')
    parser.add_argument('--expect', metavar='FILE',
                        help='JSON map of output name to size and sha256')
    args = parser.parse_args(argv)
    if args.pinned and args.expect:
        parser.error('--pinned and --expect are mutually exclusive')

    expected = None
    if args.pinned:
        expected = {name: value for name, value in PINNED_OUTPUTS.items()}
        expected['archive'] = (PINNED_ARCHIVE['size'], PINNED_ARCHIVE['sha256'])
    elif args.expect:
        expected = expectation(args.expect)

    try:
        output, report, outputs = extract(args.archive, args.output, expected)
    except ExtractionError as error:
        print(f'[pe-extract] FAIL: {error}', file=sys.stderr)
        return 1

    boot = report['boot']
    print(f'[pe-extract] archive {report["archive"]["name"]} '
          f'{report["archive"]["size"]} bytes sha256 {report["archive"]["sha256"]}')
    print(f'[pe-extract] boot v{boot["header_version"]} page {boot["page_size"]} '
          f'kernel {boot["kernel_size"]} ramdisk {boot["ramdisk_size"]} '
          f'dtb {boot["dtb_size"]} sections end {boot["sections_end"]}')
    print(f'[pe-extract] {report["dtb"]["count"]} FDTs, '
          f'{report["dtbo"]["count"]} DTBO entries, '
          f'config {report["ikconfig"]["size"]} bytes')
    print(f'[pe-extract] wrote {len(outputs)} files to {output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
