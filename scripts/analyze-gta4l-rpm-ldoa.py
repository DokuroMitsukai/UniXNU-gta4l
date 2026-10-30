#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only static reference analysis of the captured RPM LDOA tables.

The script reads the captured `rpm.img` read-only, verifies its SHA-256, and
reports the static table anchors and every aligned stored pointer to them. It
does not load, execute or write firmware, and it does not touch a device, bus,
rail or clock.

The analysis is deliberately bounded. A target with no reported reference means
no aligned 4-byte word in the image equals the target virtual address or its
link-base-relative form. It does not exclude computed addresses, `movw`/`movt`
construction, runtime relocation, aliasing or dynamic objects.

Usage:
    python3 scripts/analyze-gta4l-rpm-ldoa.py IMAGE
    python3 scripts/analyze-gta4l-rpm-ldoa.py IMAGE --expect-sha256 HEX

The expected image is the private device dump. The script is host-only and the
image is not vendored.
"""
import argparse
import hashlib
import struct
import sys

BASE = 0x4600000
EXPECTED_SHA256 = '272bebd3ddd7ef223d6e95e9b4cd30385a3df0eb4fa7beae6b622f457b4937c6'

NAME_BASE = 0x4696994
NAME_COUNT = 24
NAME_STRIDE = 8
SELECTOR = 0x469667c
SELECTOR_COUNT = 16
GROUP_TABLE = 0x46966fc
GROUP_TABLE_COUNT = 3
GROUP_TABLE_STRIDE = 12
VOLTAGE_TABLE = 0x4696a54
VOLTAGE_COUNT = 24
VOLTAGE_STRIDE = 12
DESCRIPTOR = 0x4696c40
BOUNDARY_TABLE = 0x4697e20
BOUNDARY_COUNT = 82
BOUNDARY_WORD = 0x4697f10
CLASS_TABLE = 0x4695774
CLASS_COUNT = 7

L11 = 11
L24 = 24
L11_ENTRY = VOLTAGE_TABLE + (L11 - 1) * VOLTAGE_STRIDE
L24_ENTRY = VOLTAGE_TABLE + (L24 - 1) * VOLTAGE_STRIDE


class AnalysisError(Exception):
    pass


def u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def parse_segments(image):
    if image[:4] != b'\x7fELF' or image[4] != 1 or image[5] != 1:
        raise AnalysisError('not a little-endian ELF32 image')
    phoff = u32(image, 28)
    phentsize = u16(image, 42)
    phnum = u16(image, 44)
    if phentsize < 32 or phoff + phentsize * phnum > len(image):
        raise AnalysisError('program header table is out of range')
    segments = []
    for index in range(phnum):
        entry = phoff + index * phentsize
        p_type = u32(image, entry)
        p_offset = u32(image, entry + 4)
        p_vaddr = u32(image, entry + 8)
        p_filesz = u32(image, entry + 16)
        if not p_filesz:
            continue
        if p_offset + p_filesz > len(image):
            raise AnalysisError(f'segment {index} exceeds the file')
        segments.append({
            'index': index,
            'type': p_type,
            'offset': p_offset,
            'vaddr': p_vaddr,
            'size': p_filesz,
        })
    if not segments:
        raise AnalysisError('image has no file-backed segments')
    return segments


def va_to_offset(segments, va, width=4):
    for segment in segments:
        start = segment['vaddr']
        end = start + segment['size']
        if start <= va and va + width <= end:
            return segment['offset'] + va - start
    raise AnalysisError(f'virtual address {va:#x} is not mapped')


def read(image, segments, va, width):
    offset = va_to_offset(segments, va, width)
    return image[offset:offset + width]


def read_u16(image, segments, va):
    return u16(read(image, segments, va, 2), 0)


def read_u32(image, segments, va):
    return u32(read(image, segments, va, 4), 0)


def read_bytes(image, segments, va, width):
    return read(image, segments, va, width)


def format_pointer(value):
    if BASE <= value < BASE + 0x100000:
        return f'va:{value:#x}'
    if value < 0x100000:
        return f'rt:{value:#x}->{value + BASE:#x}'
    return f'other:{value:#x}'


def scan_references(image, segments, targets):
    references = {name: [] for name in targets}
    for segment in segments:
        block = image[segment['offset']:segment['offset'] + segment['size']]
        for offset in range(0, len(block) - 3, 4):
            value = u32(block, offset)
            address = segment['vaddr'] + offset
            for name, target in targets.items():
                if value == target:
                    references[name].append((address, 'va', value))
                elif value == target - BASE:
                    references[name].append((address, 'rt', value))
    return references


def describe_anchor_failures(image, segments):
    failures = []
    for index in range(NAME_COUNT):
        va = NAME_BASE + index * NAME_STRIDE
        fourcc = read_bytes(image, segments, va, 4)
        ident = read_u32(image, segments, va + 4)
        if fourcc != b'ldoa' or ident != index + 1:
            failures.append(f'name record {index + 1} at {va:#x} is not ldoa/{index + 1}')
    selector = [read_u32(image, segments, SELECTOR + 4 * i) for i in range(SELECTOR_COUNT)]
    if selector[0] != NAME_BASE + 5 * NAME_STRIDE - BASE:
        failures.append('selector does not start at the id-6 record')
    if read_u32(image, segments, GROUP_TABLE) != SELECTOR - BASE:
        failures.append('group table does not point at the selector array')
    if read_u32(image, segments, DESCRIPTOR) != VOLTAGE_TABLE - BASE:
        failures.append('descriptor does not point at the voltage table')
    boundary_offset = BOUNDARY_WORD - BOUNDARY_TABLE
    if boundary_offset % 4 or boundary_offset >= BOUNDARY_COUNT * 4:
        failures.append('boundary word is outside the boundary table')
    elif read_u32(image, segments, BOUNDARY_WORD) != DESCRIPTOR - BASE:
        failures.append('boundary word does not point at the descriptor')
    return failures


def report(image, segments):
    print(f'sha256: {hashlib.sha256(image).hexdigest()}')
    for segment in segments:
        print('segment: index={index} type={type:#x} va={vaddr:#x} size={size:#x} '
              'offset={offset:#x}'.format(**segment))

    print(f'ldoa_record: id={L11} va={NAME_BASE + (L11 - 1) * NAME_STRIDE:#x}')
    print(f'ldoa_record: id={L24} va={NAME_BASE + (L24 - 1) * NAME_STRIDE:#x}')

    selector = []
    for index in range(SELECTOR_COUNT):
        value = read_u32(image, segments, SELECTOR + 4 * index)
        ident = (value + BASE - NAME_BASE) // NAME_STRIDE + 1 if value < BASE else 0
        selector.append(ident)
        print(f'selector: index={index} word={format_pointer(value)} id={ident}')

    for index in range(GROUP_TABLE_COUNT):
        value = read_u32(image, segments, GROUP_TABLE + GROUP_TABLE_STRIDE * index)
        print(f'group_table: index={index} word={format_pointer(value)}')

    for index in range(VOLTAGE_COUNT):
        va = VOLTAGE_TABLE + index * VOLTAGE_STRIDE
        head = read_u32(image, segments, va)
        low = read_u16(image, segments, va + 4)
        high = read_u16(image, segments, va + 6)
        tail = read_u32(image, segments, va + 8)
        print(f'voltage_row: id={index + 1} va={va:#x} head={head:#010x} '
              f'min={low} max={high} tail={tail:#010x}')

    print(f'descriptor: va={DESCRIPTOR:#x} word={format_pointer(read_u32(image, segments, DESCRIPTOR))}')
    boundary_index = (BOUNDARY_WORD - BOUNDARY_TABLE) // 4
    print(f'boundary_table: va={BOUNDARY_TABLE:#x} count={BOUNDARY_COUNT} '
          f'word_index={boundary_index} word={format_pointer(read_u32(image, segments, BOUNDARY_WORD))}')

    nonzero = 0
    for index in range(CLASS_COUNT):
        if read_u32(image, segments, CLASS_TABLE + 4 * index):
            nonzero += 1
    print(f'class_table: va={CLASS_TABLE:#x} count={CLASS_COUNT} nonzero={nonzero}')

    targets = {
        'l11_record': NAME_BASE + (L11 - 1) * NAME_STRIDE,
        'l24_record': NAME_BASE + (L24 - 1) * NAME_STRIDE,
        'selector': SELECTOR,
        'group_table': GROUP_TABLE,
        'voltage_table': VOLTAGE_TABLE,
        'descriptor': DESCRIPTOR,
        'boundary_table': BOUNDARY_TABLE,
        'boundary_word': BOUNDARY_WORD,
    }
    references = scan_references(image, segments, targets)
    for name in targets:
        print(f'reference_count: target={name} count={len(references[name])}')
        for address, form, value in references[name]:
            print(f'reference: target={name} form={form} address={address:#x} word={value:#x}')

    print(f'ldoa_entries: l11={L11_ENTRY:#x} l24={L24_ENTRY:#x}')
    for label, va in (('l11', L11_ENTRY), ('l24', L24_ENTRY)):
        print(f'entry_fields: target={label} va={va:#x} '
              f'min={read_u16(image, segments, va + 4)} '
              f'byte6={read_bytes(image, segments, va + 6, 1)[0]:#x} '
              f'byte9={read_bytes(image, segments, va + 9, 1)[0]:#x} '
              f'byte10={read_bytes(image, segments, va + 10, 1)[0]:#x}')
    return references


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', help='captured rpm.img')
    parser.add_argument('--expect-sha256', default=EXPECTED_SHA256,
                        help='required image SHA-256 (default: captured RPM)')
    args = parser.parse_args(argv)

    try:
        with open(args.image, 'rb') as handle:
            image = handle.read()
        actual = hashlib.sha256(image).hexdigest()
        if actual != args.expect_sha256.lower():
            raise AnalysisError(f'image SHA-256 {actual} does not match {args.expect_sha256}')
        segments = parse_segments(image)
        failures = describe_anchor_failures(image, segments)
        if failures:
            for failure in failures:
                print(f'anchor: FAIL {failure}', file=sys.stderr)
            raise AnalysisError('image does not match the expected LDOA layout')
        report(image, segments)
    except (AnalysisError, OSError, struct.error) as error:
        print(f'analyze: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
