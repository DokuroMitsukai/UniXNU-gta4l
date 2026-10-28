#!/usr/bin/env python3
"""Validate a primary/backup GPT pair without writing to the disk.

The parser checks the protective MBR, header CRCs, entry-array CRCs, usable
ranges, overlaps, and equality of the primary and backup partition arrays.
It is intended for read-only recovery captures.
"""
import argparse
import hashlib
import json
import struct
import uuid
import zlib
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--primary', type=Path, required=True)
parser.add_argument('--backup', type=Path, required=True)
parser.add_argument('--sector-size', type=int, default=512)
parser.add_argument('--backup-skip-lba', type=int, required=True,
                    help='first LBA included in the backup capture')
parser.add_argument('--output', type=Path)
args = parser.parse_args()


def parse(path, skip_lba, check_mbr):
    data = path.read_bytes()
    if check_mbr:
        if data[510:512] != b'\x55\xaa':
            raise SystemExit(f'{path}: missing MBR signature')
        if data[446 + 4] != 0xee:
            raise SystemExit(f'{path}: no protective GPT MBR entry')

    header_at = 512
    if data[header_at:header_at + 8] != b'EFI PART':
        header_at = 33 * args.sector_size
    header = data[header_at:header_at + 92]
    if header[:8] != b'EFI PART':
        raise SystemExit(f'{path}: missing GPT header')
    (signature, revision, header_size, header_crc, _, current_lba,
     backup_lba, first_usable, last_usable, disk_guid, entries_lba,
     entry_count, entry_size, entries_crc) = struct.unpack_from(
        '<8sIIIIQQQQ16sQIII', header, 0)
    if header_size != 92:
        raise SystemExit(f'{path}: unexpected GPT header size {header_size}')
    calculated = zlib.crc32(header[:16] + b'\0\0\0\0' + header[20:]) & 0xffffffff
    if calculated != header_crc:
        raise SystemExit(f'{path}: header CRC mismatch')
    if not 0 < entry_count <= 4096 or entry_size < 128 or entry_size % 8:
        raise SystemExit(f'{path}: invalid entry array geometry')

    entries_at = entries_lba * args.sector_size - skip_lba * args.sector_size
    entries = data[entries_at:entries_at + entry_count * entry_size]
    if len(entries) != entry_count * entry_size:
        raise SystemExit(f'{path}: truncated entry array')
    if zlib.crc32(entries) & 0xffffffff != entries_crc:
        raise SystemExit(f'{path}: entry-array CRC mismatch')

    partitions = []
    for index in range(entry_count):
        entry = entries[index * entry_size:(index + 1) * entry_size]
        type_id = uuid.UUID(bytes_le=entry[:16])
        if not type_id.int:
            continue
        first, last, attributes = struct.unpack_from('<QQQ', entry, 32)
        if first > last or first < first_usable or last > last_usable:
            raise SystemExit(f'{path}: partition {index + 1} outside usable range')
        name = entry[56:128].decode('utf-16le', 'replace').rstrip('\0')
        partitions.append({
            'index': index + 1,
            'name': name,
            'type': str(type_id),
            'first_lba': first,
            'last_lba': last,
            'sectors': last - first + 1,
            'attributes': f'{attributes:#x}',
        })
    partitions.sort(key=lambda item: item['first_lba'])
    for previous, current in zip(partitions, partitions[1:]):
        if previous['last_lba'] >= current['first_lba']:
            raise SystemExit(f'{path}: overlapping partitions')

    return {
        'file': str(path),
        'sha256': hashlib.sha256(data).hexdigest(),
        'header_offset': header_at,
        'revision': f'{revision:#x}',
        'current_lba': current_lba,
        'backup_lba': backup_lba,
        'first_usable_lba': first_usable,
        'last_usable_lba': last_usable,
        'disk_guid': str(uuid.UUID(bytes_le=disk_guid)),
        'entries_lba': entries_lba,
        'entry_count': entry_count,
        'entry_size': entry_size,
        'partitions': partitions,
    }


primary = parse(args.primary, 0, True)
backup = parse(args.backup, args.backup_skip_lba, False)
if primary['disk_guid'] != backup['disk_guid']:
    raise SystemExit('primary/backup disk GUID differs')
if primary['partitions'] != backup['partitions']:
    raise SystemExit('primary/backup partition arrays differ')
report = {
    'primary': primary,
    'backup': backup,
    'partitions_equal': True,
    'partition_count': len(primary['partitions']),
    'capacity_sectors': primary['last_usable_lba'] - primary['first_usable_lba'] + 1,
}
text = json.dumps(report, indent=2) + '\n'
if args.output:
    args.output.write_text(text)
print(text)
