#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only static reference analysis of the captured RPM LDOA tables.

The script reads the captured `rpm.img` read-only, verifies its SHA-256, and
reports the static table anchors and every aligned stored pointer to them. It
does not load, execute or write firmware, and it does not touch a device, bus,
rail or clock.

The analysis is deliberately bounded. A target with no reported reference means
no aligned 4-byte word in a `PT_LOAD` segment equals the target virtual address,
its link-base-relative form, or either Thumb-tagged form. It does not exclude
computed addresses, `movw`/`movt` construction, runtime relocation, aliasing or
dynamic objects.

The optional `--code` checks decode only the documented Thumb sites and direct
call instructions, including literal-load destination registers, the builder
type byte and the callback argument setup and stack stores. They are mechanical
instruction checks, not control-flow reachability or proof that a callback
executes at runtime.

Usage:
    python3 scripts/analyze-gta4l-rpm-ldoa.py IMAGE
    python3 scripts/analyze-gta4l-rpm-ldoa.py IMAGE --code
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
EXPECTED_ENTRY = 0x4623e51
EXPECTED_LOADS = (
    (0x4600000, 0x3000, 0x27e04),
    (0x4690000, 0x2ae10, 0xc4d0),
)
EXPECTED_NULLS = (
    (0x0, 0x0, 0xb4),
    (0x469d000, 0x1000, 0x1a68),
)

NAME_BASE = 0x4696994
NAME_COUNT = 24
NAME_STRIDE = 8
SELECTOR = 0x469667c
SELECTOR_COUNT = 16
SELECTOR_IDS = (6, 8, 1, 2, 3, 4, 7, 17, 18, 9, 10, 11, 12, 13, 14, 16)
GROUP_TABLE = 0x46966fc
GROUP_TABLE_COUNT = 3
GROUP_TABLE_STRIDE = 12
GROUP_TABLE_EXPECTED = (
    SELECTOR - BASE,
    SELECTOR + 8 - BASE,
    SELECTOR + 0x24 - BASE,
)
VOLTAGE_TABLE = 0x4696a54
VOLTAGE_COUNT = 24
VOLTAGE_STRIDE = 12
DESCRIPTOR = 0x4696c40
DESCRIPTOR_SECOND = 0x4696b74
BOUNDARY_TABLE = 0x4697e20
BOUNDARY_COUNT = 82
BOUNDARY_WORD = 0x4697f10
CLASS_TABLE = 0x4695774
CLASS_COUNT = 7

L11 = 11
L24 = 24
L11_ENTRY = VOLTAGE_TABLE + (L11 - 1) * VOLTAGE_STRIDE
L24_ENTRY = VOLTAGE_TABLE + (L24 - 1) * VOLTAGE_STRIDE
VOLTAGE_EXPECTED = {
    L11: (0x62018000, 1704, 1952, 0x00020000),
    L24: (0x62156000, 2704, 3600, 0x00020000),
}

PARSER = 0x46151f8
AGGREGATOR = 0x46153b8
APPLY = 0x4615906
COMMIT = 0x4614920
CLASS_SETTER = 0x4615d08
GENERIC_SETTER = 0x4617b48
SMPA_BRIDGE = 0x4616e10
BUILDER = 0x4698944
REGISTRATION = 0x460ae14

CALL_EDGES = (
    (0x4615502, AGGREGATOR, APPLY),
    (0x4615970, APPLY, COMMIT),
    (0x4616e8a, SMPA_BRIDGE, CLASS_SETTER),
    (0x4615d7c, CLASS_SETTER, GENERIC_SETTER),
    (0x4698ade, BUILDER, REGISTRATION),
)

CLASS_REGISTRATION = 0x461746c
CLASS_STUBS = (0x4617468, 0x461746a)
CLASS_IMMEDIATES = (
    (0x461748e, 0x2109, 'smpa count 9'),
    (0x4617490, 0x220c, 'smpa type 0xc'),
    (0x461749e, 0x2119, 'ldoa count 25'),
    (0x46174a0, 0x2210, 'ldoa type 0x10'),
    (0x46174ac, 0x210f, 'clka count 15'),
    (0x46174ae, 0x220c, 'clka type 0xc'),
)
CLASS_BLX = (0x4617498, 0x46174a6, 0x46174b4)
CLASS_STRINGS = (
    (0x46174c8, b'smpa'),
    (0x46174d4, b'ldoa'),
    (0x46174d8, b'clka'),
)
CLASS_LITERALS = (
    (0x46174c4, 0x17469, 0x4617468),
    (0x46174cc, 0x1746b, 0x461746a),
    (0x46174d0, 0xae15, REGISTRATION),
)
BUILDER_LITERALS = (
    (0x4698af8, 0x153b9, AGGREGATOR),
    (0x4698afc, 0x151f9, PARSER),
)
LITERAL_LOADS = (
    (0x4617486, 6, 0x46174c4),
    (0x461748a, 4, 0x46174cc),
    (0x461748c, 7, 0x46174d0),
    (0x4617496, 0, 0x46174c8),
    (0x46174a4, 0, 0x46174d4),
    (0x46174b2, 0, 0x46174d8),
    (0x4698ad0, 3, 0x4698afc),
    (0x4698ad8, 0, 0x4698af8),
)
CODE_SETUP = (
    (0x4617488, 0x9501, 'smpa bridge stack argument r5'),
    (0x4617492, 0x9600, 'smpa callback stack argument r6'),
    (0x461749a, 0xe9cd, 'smpa callback stack pair low'),
    (0x461749c, 0x6500, 'smpa callback stack pair high'),
    (0x46174a2, 0x4623, 'ldoa callback argument mov r3,r4'),
    (0x46174a8, 0xe9cd, 'ldoa callback stack pair low'),
    (0x46174aa, 0x6500, 'ldoa callback stack pair high'),
    (0x46174b0, 0x4623, 'clka callback argument mov r3,r4'),
    (0x4698ad4, 0x2210, 'builder ldoa type 0x10'),
    (0x4698ad6, 0x9001, 'builder class stack argument'),
    (0x4698ada, 0x9000, 'builder callback stack argument'),
)


class AnalysisError(Exception):
    pass


def u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def parse_segments(image):
    if len(image) < 52:
        raise AnalysisError('image is too short for an ELF32 header')
    if image[:4] != b'\x7fELF' or image[4] != 1 or image[5] != 1:
        raise AnalysisError('not a little-endian ELF32 image')
    if u16(image, 16) != 2:
        raise AnalysisError('not an ELF executable')
    if u16(image, 18) != 40:
        raise AnalysisError('not an ARM ELF image')
    if u32(image, 20) != 1:
        raise AnalysisError('unexpected ELF version')
    entry = u32(image, 24)
    if not entry & 1:
        raise AnalysisError('entry is not Thumb-tagged')
    phoff = u32(image, 28)
    phentsize = u16(image, 42)
    phnum = u16(image, 44)
    if phentsize < 32 or phoff + phentsize * phnum > len(image):
        raise AnalysisError('program header table is out of range')
    segments = []
    for index in range(phnum):
        entry_offset = phoff + index * phentsize
        p_type = u32(image, entry_offset)
        p_offset = u32(image, entry_offset + 4)
        p_vaddr = u32(image, entry_offset + 8)
        p_filesz = u32(image, entry_offset + 16)
        p_memsz = u32(image, entry_offset + 20)
        p_flags = u32(image, entry_offset + 24)
        p_align = u32(image, entry_offset + 28)
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
            'memsz': p_memsz,
            'flags': p_flags,
            'align': p_align,
        })
    loads = [segment for segment in segments if segment['type'] == 1]
    if not loads:
        raise AnalysisError('image has no file-backed LOAD segments')
    for segment in loads:
        if segment['size'] > segment['memsz']:
            raise AnalysisError(f'LOAD segment {segment["index"]} has filesz above memsz')
        if segment['align'] > 1 and \
                (segment['vaddr'] - segment['offset']) % segment['align']:
            raise AnalysisError(f'LOAD segment {segment["index"]} violates its alignment')
    loads.sort(key=lambda segment: segment['vaddr'])
    for left, right in zip(loads, loads[1:]):
        if left['vaddr'] + left['size'] > right['vaddr']:
            raise AnalysisError('LOAD segments overlap')
    base = loads[0]['vaddr']
    if base != BASE:
        raise AnalysisError(f'link base {base:#x} is not the expected {BASE:#x}')
    return segments, loads, {'entry': entry, 'base': base}


def va_to_offset(loads, va, width=4):
    for segment in loads:
        start = segment['vaddr']
        end = start + segment['size']
        if start <= va and va + width <= end:
            return segment['offset'] + va - start
    raise AnalysisError(f'virtual address {va:#x} is not mapped by a LOAD segment')


def read(image, loads, va, width):
    offset = va_to_offset(loads, va, width)
    return image[offset:offset + width]


def read_u16(image, loads, va):
    return u16(read(image, loads, va, 2), 0)


def read_u32(image, loads, va):
    return u32(read(image, loads, va, 4), 0)


def read_bytes(image, loads, va, width):
    return read(image, loads, va, width)


def format_pointer(value):
    if BASE <= value < BASE + 0x100000:
        return f'va:{value:#x}'
    if value < 0x100000:
        return f'rt:{value:#x}->{value + BASE:#x}'
    return f'other:{value:#x}'


def selector_id(value):
    if value & 1 or value >= BASE:
        return 0
    va = value + BASE
    if not NAME_BASE <= va < NAME_BASE + NAME_COUNT * NAME_STRIDE:
        return 0
    if (va - NAME_BASE) % NAME_STRIDE:
        return 0
    return (va - NAME_BASE) // NAME_STRIDE + 1


def scan_references(image, loads, targets):
    references = {name: [] for name in targets}
    for segment in loads:
        block = image[segment['offset']:segment['offset'] + segment['size']]
        for offset in range(0, len(block) - 3, 4):
            value = u32(block, offset)
            address = segment['vaddr'] + offset
            for name, target in targets.items():
                candidates = (
                    ('va', target),
                    ('rt', target - BASE),
                    ('thumb_va', target | 1),
                    ('thumb_rt', (target - BASE) | 1),
                )
                seen = set()
                for form, candidate in candidates:
                    if candidate in seen:
                        continue
                    seen.add(candidate)
                    if value == candidate:
                        references[name].append((address, form, value))
                        break
    return references


def describe_layout_failures(segments, header):
    failures = []
    if header['entry'] != EXPECTED_ENTRY:
        failures.append(f"entry {header['entry']:#x} is not {EXPECTED_ENTRY:#x}")
    loads = [segment for segment in segments if segment['type'] == 1]
    if len(loads) != len(EXPECTED_LOADS):
        failures.append(f'expected {len(EXPECTED_LOADS)} LOAD segments, found {len(loads)}')
    else:
        for segment, expected in zip(loads, EXPECTED_LOADS):
            actual = (segment['vaddr'], segment['offset'], segment['size'])
            if actual != expected:
                failures.append(f'LOAD segment {segment["index"]} is {actual}, expected {expected}')
    nulls = [segment for segment in segments if segment['type'] == 0]
    actual_nulls = tuple((segment['vaddr'], segment['offset'], segment['size'])
                         for segment in nulls)
    if actual_nulls != EXPECTED_NULLS:
        failures.append(f'PT_NULL segments are {actual_nulls}, expected {EXPECTED_NULLS}')
    return failures


def describe_anchor_failures(image, loads):
    failures = []
    for index in range(NAME_COUNT):
        va = NAME_BASE + index * NAME_STRIDE
        fourcc = read_bytes(image, loads, va, 4)
        ident = read_u32(image, loads, va + 4)
        if fourcc != b'ldoa' or ident != index + 1:
            failures.append(f'name record {index + 1} at {va:#x} is not ldoa/{index + 1}')
    ids = [selector_id(read_u32(image, loads, SELECTOR + 4 * index))
           for index in range(SELECTOR_COUNT)]
    if tuple(ids) != SELECTOR_IDS:
        failures.append(f'selector ids {ids} do not match {list(SELECTOR_IDS)}')
    if read_u32(image, loads, SELECTOR + 4 * SELECTOR_COUNT) != 0:
        failures.append('selector array is not NULL-terminated')
    for index, expected in enumerate(GROUP_TABLE_EXPECTED):
        value = read_u32(image, loads, GROUP_TABLE + GROUP_TABLE_STRIDE * index)
        if value != expected:
            failures.append(f'group table entry {index} is {value:#x}, expected {expected:#x}')
    for ident, expected in VOLTAGE_EXPECTED.items():
        va = VOLTAGE_TABLE + (ident - 1) * VOLTAGE_STRIDE
        actual = (read_u32(image, loads, va), read_u16(image, loads, va + 4),
                  read_u16(image, loads, va + 6), read_u32(image, loads, va + 8))
        if actual != expected:
            failures.append(f'voltage row {ident} is {actual}, expected {expected}')
    if read_u32(image, loads, DESCRIPTOR) != VOLTAGE_TABLE - BASE:
        failures.append('descriptor does not point at the voltage table')
    if read_u32(image, loads, DESCRIPTOR + 4) != DESCRIPTOR_SECOND - BASE:
        failures.append('descriptor second word does not point at 0x4696b74')
    if read_u32(image, loads, BOUNDARY_TABLE + 4 * BOUNDARY_COUNT) != 0:
        failures.append('boundary table is not NULL-terminated')
    boundary_values = [read_u32(image, loads, BOUNDARY_TABLE + 4 * index)
                       for index in range(BOUNDARY_COUNT)]
    if any(value == 0 for value in boundary_values):
        failures.append('boundary table contains a zero entry')
    if len(set(boundary_values)) != BOUNDARY_COUNT:
        failures.append('boundary table entries are not unique')
    for index, value in enumerate(boundary_values):
        if value >= BASE:
            failures.append(f'boundary table entry {index} is not a runtime offset')
    boundary_offset = BOUNDARY_WORD - BOUNDARY_TABLE
    if boundary_offset % 4 or boundary_offset >= BOUNDARY_COUNT * 4:
        failures.append('boundary word is outside the boundary table')
    elif read_u32(image, loads, BOUNDARY_WORD) != DESCRIPTOR - BASE:
        failures.append('boundary word does not point at the descriptor')
    for index in range(CLASS_COUNT):
        if read_u32(image, loads, CLASS_TABLE + 4 * index):
            failures.append(f'linked class table word {index} is nonzero')
    return failures


def thumb_bl_target(image, loads, site):
    first = read_u16(image, loads, site)
    second = read_u16(image, loads, site + 2)
    if (first & 0xF800) != 0xF000 or (second & 0xD000) != 0xD000:
        return None
    sign = (first >> 10) & 1
    imm10 = first & 0x3FF
    j1 = (second >> 13) & 1
    j2 = (second >> 11) & 1
    imm11 = second & 0x7FF
    i1 = 1 - (j1 ^ sign)
    i2 = 1 - (j2 ^ sign)
    offset = (sign << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
    if sign:
        offset -= 1 << 25
    return site + 4 + offset


def thumb_ldr_literal_target(image, loads, site):
    instruction = read_u16(image, loads, site)
    if (instruction & 0xF800) != 0x4800:
        return None
    return ((site + 4) & ~3) + ((instruction & 0xFF) << 2)


def verify_code(image, loads):
    def check_call(site, target):
        actual = thumb_bl_target(image, loads, site)
        if actual != target:
            raise AnalysisError(f'{site:#x} calls {actual}, expected {target:#x}')
        print(f'code_edge: site={site:#x} target={target:#x} kind=bl')

    def check_literal_load(site, dest, target):
        resolved = thumb_ldr_literal_target(image, loads, site)
        if resolved != target:
            raise AnalysisError(f'{site:#x} loads {resolved}, expected {target:#x}')
        actual = (read_u16(image, loads, site) >> 8) & 0x7
        if actual != dest:
            raise AnalysisError(f'{site:#x} loads into r{actual}, expected r{dest}')
        print(f'code_literal_load: site={site:#x} dest=r{dest} target={target:#x}')

    def check_u16(va, expected, label):
        actual = read_u16(image, loads, va)
        if actual != expected:
            raise AnalysisError(f'{label} at {va:#x} is {actual:#x}, expected {expected:#x}')

    for site, source, target in CALL_EDGES:
        if source not in (PARSER, AGGREGATOR, APPLY, COMMIT, CLASS_SETTER,
                          GENERIC_SETTER, SMPA_BRIDGE, BUILDER):
            raise AnalysisError(f'unknown call-edge source {source:#x}')
        check_call(site, target)
    for site, dest, target in LITERAL_LOADS:
        check_literal_load(site, dest, target)
    for va, expected, label in CODE_SETUP:
        check_u16(va, expected, label)
        print(f'code_setup: site={va:#x} label="{label}" value={expected:#x}')
    for va, expected, label in CLASS_IMMEDIATES:
        check_u16(va, expected, label)
        print(f'code_class: site={va:#x} label="{label}" value={expected:#x}')
    for site in CLASS_BLX:
        check_u16(site, 0x47b8, 'class registration call')
        print(f'code_class_call: site={site:#x} instruction=blx_r7 literal=0xae15')
    for va, text in CLASS_STRINGS:
        actual = read_bytes(image, loads, va, len(text))
        if actual != text:
            raise AnalysisError(f'class string at {va:#x} is {actual!r}, expected {text!r}')
        print(f'code_class_string: site={va:#x} text={text.decode()}')
    for va, literal, target in CLASS_LITERALS:
        actual = read_u32(image, loads, va)
        if actual != literal or (actual + BASE) & ~1 != target:
            raise AnalysisError(f'class literal at {va:#x} is {actual:#x}, expected {literal:#x}')
        print(f'code_class_literal: site={va:#x} value={literal:#x} target={target:#x}')
    for va in CLASS_STUBS:
        check_u16(va, 0x4770, 'callback stub')
        print(f'code_stub: site={va:#x} instruction=bx_lr')
    for va, literal, target in BUILDER_LITERALS:
        actual = read_u32(image, loads, va)
        if actual != literal or (actual + BASE) & ~1 != target:
            raise AnalysisError(f'builder literal at {va:#x} is {actual:#x}, expected {literal:#x}')
        print(f'code_builder_literal: site={va:#x} value={literal:#x} target={target:#x}')


def report(image, segments, loads, header, pinned):
    print(f'sha256: {hashlib.sha256(image).hexdigest()}')
    print(f'pinned: {1 if pinned else 0}')
    for segment in segments:
        label = 'segment' if segment['type'] == 1 else 'nonload_segment'
        print(f'{label}: index={segment["index"]} type={segment["type"]:#x} '
              f'va={segment["vaddr"]:#x} size={segment["size"]:#x} '
              f'offset={segment["offset"]:#x} flags={segment["flags"]:#x}')
    print(f"link_base: {header['base']:#x}")
    print(f"entry: {header['entry']:#x} thumb=1")

    print(f'ldoa_record: id={L11} va={NAME_BASE + (L11 - 1) * NAME_STRIDE:#x}')
    print(f'ldoa_record: id={L24} va={NAME_BASE + (L24 - 1) * NAME_STRIDE:#x}')

    for index in range(SELECTOR_COUNT):
        value = read_u32(image, loads, SELECTOR + 4 * index)
        ident = selector_id(value)
        print(f'selector: index={index} word={format_pointer(value)} id={ident}')
    print(f'selector_terminator: va={SELECTOR + 4 * SELECTOR_COUNT:#x} '
          f'word={read_u32(image, loads, SELECTOR + 4 * SELECTOR_COUNT):#x}')

    for index in range(GROUP_TABLE_COUNT):
        value = read_u32(image, loads, GROUP_TABLE + GROUP_TABLE_STRIDE * index)
        print(f'group_table: index={index} word={format_pointer(value)}')

    for index in range(VOLTAGE_COUNT):
        va = VOLTAGE_TABLE + index * VOLTAGE_STRIDE
        head = read_u32(image, loads, va)
        low = read_u16(image, loads, va + 4)
        high = read_u16(image, loads, va + 6)
        tail = read_u32(image, loads, va + 8)
        print(f'voltage_row: id={index + 1} va={va:#x} head={head:#010x} '
              f'min={low} max={high} tail={tail:#010x}')

    print(f'descriptor: va={DESCRIPTOR:#x} word={format_pointer(read_u32(image, loads, DESCRIPTOR))}')
    print(f'descriptor_second: va={DESCRIPTOR + 4:#x} '
          f'word={format_pointer(read_u32(image, loads, DESCRIPTOR + 4))}')
    boundary_index = (BOUNDARY_WORD - BOUNDARY_TABLE) // 4
    print(f'boundary_table: va={BOUNDARY_TABLE:#x} count={BOUNDARY_COUNT} '
          f'word_index={boundary_index} word={format_pointer(read_u32(image, loads, BOUNDARY_WORD))}')
    print(f'boundary_terminator: va={BOUNDARY_TABLE + 4 * BOUNDARY_COUNT:#x} '
          f'word={read_u32(image, loads, BOUNDARY_TABLE + 4 * BOUNDARY_COUNT):#x}')

    nonzero = 0
    for index in range(CLASS_COUNT):
        if read_u32(image, loads, CLASS_TABLE + 4 * index):
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
    references = scan_references(image, loads, targets)
    for name in targets:
        print(f'reference_count: target={name} count={len(references[name])}')
        for address, form, value in references[name]:
            print(f'reference: target={name} form={form} address={address:#x} word={value:#x}')

    print(f'ldoa_entries: l11={L11_ENTRY:#x} l24={L24_ENTRY:#x}')
    for label, va in (('l11', L11_ENTRY), ('l24', L24_ENTRY)):
        print(f'entry_fields: target={label} va={va:#x} '
              f'min={read_u16(image, loads, va + 4)} '
              f'byte6={read_bytes(image, loads, va + 6, 1)[0]:#x} '
              f'byte9={read_bytes(image, loads, va + 9, 1)[0]:#x} '
              f'byte10={read_bytes(image, loads, va + 10, 1)[0]:#x}')
    return references


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', help='captured rpm.img')
    parser.add_argument('--expect-sha256', default=EXPECTED_SHA256,
                        help='required image SHA-256 (default: captured RPM)')
    parser.add_argument('--code', action='store_true',
                        help='also verify the documented Thumb sites, literals and setup')
    args = parser.parse_args(argv)

    try:
        with open(args.image, 'rb') as handle:
            image = handle.read()
        actual = hashlib.sha256(image).hexdigest()
        if actual != args.expect_sha256.lower():
            raise AnalysisError(f'image SHA-256 {actual} does not match {args.expect_sha256}')
        segments, loads, header = parse_segments(image)
        pinned = actual == EXPECTED_SHA256
        failures = describe_anchor_failures(image, loads)
        if pinned:
            failures.extend(describe_layout_failures(segments, header))
        if failures:
            for failure in failures:
                print(f'anchor: FAIL {failure}', file=sys.stderr)
            raise AnalysisError('image does not match the expected LDOA layout')
        report(image, segments, loads, header, pinned)
        if args.code:
            verify_code(image, loads)
        print('result: PASS')
    except (AnalysisError, OSError, struct.error) as error:
        print(f'analyze: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
