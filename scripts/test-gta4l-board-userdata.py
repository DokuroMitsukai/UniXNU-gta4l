#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Check board.toml [userdata] against the captured device GPT.

The board configuration names the `userdata` window that the host-build
userdata image and the driver's publication bounds depend on. The captured
GPT header and entry array in `device-dump/private/` are the authoritative
device values. This host-only check compares first_lba, last_lba and
lba_bytes with the captured `userdata` entry, validates both GPT CRCs, and
never prints partition GUIDs.

When the private capture is absent the check prints SKIP and exits 0, so a
clean checkout without the device dump still passes. Use --gpt to point at
another copy, or --self-test to run the synthetic mutation controls.
"""
import argparse
import struct
import sys
import tempfile
import tomllib
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'board.toml'
CAPTURE = (ROOT.parents[1] / 'device-dump' / 'private' / 'gta4l-dump' /
           'gta4l-gpt.bin')
SECTOR = 512
ENTRY_NAME = 'userdata'


def gpt_offset(data):
    if data[:8] == b'EFI PART':
        return 0
    if data[512:520] == b'EFI PART':
        return 512
    raise ValueError('no GPT header signature at LBA 0 or LBA 1')


def parse_gpt(path):
    data = Path(path).read_bytes()
    offset = gpt_offset(data)
    header = data[offset:offset + 92]
    if len(header) != 92:
        raise ValueError(f'{path}: truncated GPT header')
    header_size, header_crc = struct.unpack('<II', header[12:20])
    if not 92 <= header_size <= SECTOR:
        raise ValueError(f'{path}: implausible header size {header_size}')
    full = bytearray(data[offset:offset + header_size])
    if len(full) != header_size:
        raise ValueError(f'{path}: truncated GPT header')
    full[16:20] = b'\0\0\0\0'
    if zlib.crc32(full) & 0xffffffff != header_crc:
        raise ValueError(f'{path}: GPT header CRC does not match')
    entry_lba, count, entry_size = struct.unpack('<QII', header[72:88])
    if not 128 <= entry_size <= 4096 or entry_size % 8:
        raise ValueError(f'{path}: implausible entry size {entry_size}')
    if not 1 <= count <= 1024:
        raise ValueError(f'{path}: implausible entry count {count}')
    start = entry_lba * SECTOR
    end = start + count * entry_size
    if end > len(data):
        raise ValueError(f'{path}: entry array extends past the dump')
    array = data[start:end]
    entries_crc = struct.unpack('<I', header[88:92])[0]
    if zlib.crc32(array) & 0xffffffff != entries_crc:
        raise ValueError(f'{path}: GPT entry CRC does not match')
    entries = []
    for index in range(count):
        entry = array[index * entry_size:(index + 1) * entry_size]
        if entry[:16] == bytes(16):
            continue
        first, last = struct.unpack('<QQ', entry[32:48])
        name = entry[56:128].decode('utf-16le', 'replace').rstrip('\0')
        entries.append({'index': index, 'first_lba': first,
                        'last_lba': last, 'name': name})
    return entries


def load_board(path):
    data = tomllib.loads(Path(path).read_text())
    if 'userdata' not in data:
        raise ValueError(f'{path}: missing [userdata]')
    return data['userdata']


def check(board, entries):
    errors = []
    for key in ('first_lba', 'last_lba', 'lba_bytes'):
        if key not in board:
            errors.append(f'[userdata] is missing {key}')
        elif not isinstance(board[key], int) or board[key] < 0:
            errors.append(f'[userdata] {key} is not an unsigned integer')
    windows = board.get('windows')
    if not isinstance(windows, list) or not windows:
        errors.append('[userdata] windows is not a non-empty array')
    if board.get('lba_bytes') != SECTOR:
        errors.append(f'[userdata] lba_bytes={board.get("lba_bytes")} '
                      f'expected {SECTOR}')
    matches = [entry for entry in entries if entry['name'] == ENTRY_NAME]
    if len(matches) != 1:
        errors.append(f'expected one GPT {ENTRY_NAME} entry, '
                      f'found {len(matches)}')
        return errors
    entry = matches[0]
    first = board.get('first_lba')
    last = board.get('last_lba')
    if first != entry['first_lba']:
        errors.append(f'[userdata] first_lba={first} '
                      f'expected {entry["first_lba"]}')
    if last != entry['last_lba']:
        errors.append(f'[userdata] last_lba={last} '
                      f'expected {entry["last_lba"]}')
    if isinstance(first, int) and isinstance(last, int) and first > last:
        errors.append(f'[userdata] first_lba={first} is after last_lba={last}')
    for other in entries:
        if other is entry:
            continue
        if (isinstance(first, int) and isinstance(last, int) and
                first <= other['last_lba'] and other['first_lba'] <= last):
            errors.append(f'[userdata] overlaps GPT entry '
                          f'{other["name"] or other["index"]}')
    return errors


def build_gpt(entries, count=16, entry_size=128):
    array = bytearray(count * entry_size)
    for index, (name, first, last) in enumerate(entries):
        entry = bytearray(entry_size)
        entry[0:16] = bytes(range(16))
        struct.pack_into('<QQQQ', entry, 16, 1, last + 1, first, last)
        entry[56:56 + 2 * len(name)] = name.encode('utf-16le')
        array[index * entry_size:(index + 1) * entry_size] = entry
    array_crc = zlib.crc32(array) & 0xffffffff
    header = bytearray(92)
    header[0:8] = b'EFI PART'
    struct.pack_into('<IIII', header, 8, 0x00010000, 92, 0, 0)
    struct.pack_into('<QQQQ', header, 24, 1, 0, 34, 0)
    struct.pack_into('<Q', header, 72, 2)
    struct.pack_into('<II', header, 80, count, entry_size)
    struct.pack_into('<I', header, 88, array_crc)
    header[16:20] = b'\0\0\0\0'
    header[16:20] = struct.pack(
        '<I', zlib.crc32(header) & 0xffffffff)
    data = bytearray(512 * 34)
    data[510:512] = b'\x55\xaa'
    data[512:512 + 92] = header
    data[1024:1024 + len(array)] = array
    return bytes(data)


def self_test():
    first, last = 15203328, 61071326
    entries = [('userdata', first, last), ('bota', 15110144, 15182847)]
    image = build_gpt(entries)
    board = {'first_lba': first, 'last_lba': last, 'lba_bytes': 512,
             'windows': [{'name': 'OS', 'component': 'OS'}]}
    with tempfile.TemporaryDirectory(prefix='gta4l-board-userdata-') as tmp:
        path = Path(tmp) / 'gpt.bin'
        path.write_bytes(image)
        parsed = parse_gpt(path)
        assert len(parsed) == 2, parsed
        assert not check(board, parsed), check(board, parsed)
        assert check(dict(board, first_lba=0xe74000), parsed), \
            'the pre-audit first_lba was accepted'
        assert check(dict(board, last_lba=last + 1), parsed)
        assert check(dict(board, lba_bytes=4096), parsed)
        assert check({key: value for key, value in board.items()
                      if key != 'first_lba'}, parsed)
        assert check(board, parsed[1:]), 'a missing userdata entry was accepted'
        changed = bytearray(image)
        changed[1024] ^= 0xff
        path.write_bytes(changed)
        try:
            parse_gpt(path)
        except ValueError as error:
            assert 'entry CRC' in str(error), error
        else:
            raise AssertionError('a changed entry array was accepted')
    print('PASS: board userdata GPT comparison and mutation controls')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board', type=Path, default=BOARD)
    parser.add_argument('--gpt', type=Path, default=CAPTURE)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.gpt.is_file():
        print(f'SKIP: GPT capture not found at {args.gpt}; '
              'board userdata was not compared')
        return 0
    try:
        board = load_board(args.board)
        entries = parse_gpt(args.gpt)
    except (OSError, ValueError, tomllib.TOMLDecodeError) as error:
        print(f'FAIL: {error}')
        return 1
    errors = check(board, entries)
    if errors:
        for error in errors:
            print(f'FAIL: {error}')
        return 1
    print('PASS: board userdata LBA window matches the captured GPT')
    return 0


if __name__ == '__main__':
    sys.exit(main())
