#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Self-test for scripts/analyze-gta4l-rpm-ldoa.py.

The test is host-only and uses a synthetic ELF32 ARM image. It checks that the
analyzer:

- accepts the expected table layout and reports the L11/L24 reference counts;
- decodes the selector, group table, voltage rows, descriptor and boundary word;
- reports the linked class table as zero;
- excludes a matching word in a PT_NULL segment from the reference scan;
- recognizes a Thumb-tagged reference;
- verifies the documented Thumb call sites and literals with `--code`,
  including literal-load destination registers, the builder type byte and the
  registration argument setup and stack stores;
- decodes independently specified Thumb BL and LDR-literal vectors;
- rejects a wrong image hash, a changed name record, a wrong mapping, a changed
  selector, a changed voltage row, a changed boundary terminator or entry, an
  altered callback literal, a class misattribution, an altered call edge, an
  altered literal-load destination and an altered registration setup
  instruction;
- produces deterministic output.

No private firmware is read or vendored.
"""
import hashlib
import runpy
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts' / 'analyze-gta4l-rpm-ldoa.py'

BASE = 0x4600000
NAME_BASE = 0x4696994
NAME_STRIDE = 8
SELECTOR = 0x469667c
GROUP_TABLE = 0x46966fc
VOLTAGE_TABLE = 0x4696a54
VOLTAGE_STRIDE = 12
DESCRIPTOR = 0x4696c40
DESCRIPTOR_SECOND = 0x4696b74
BOUNDARY_TABLE = 0x4697e20
BOUNDARY_WORD = 0x4697f10
CLASS_TABLE = 0x4695774
TEXT_VA = 0x4600000
DATA_VA = 0x4690000
TEXT_OFF = 0x100
TEXT_SIZE = 0x28000
DATA_OFF = TEXT_OFF + TEXT_SIZE
DATA_SIZE = 0xc000
SELECTOR_IDS = [6, 8, 1, 2, 3, 4, 7, 17, 18, 9, 10, 11, 12, 13, 14, 16]
THUMB_REFERENCE = 0x4696900


def put16(block, offset, value):
    struct.pack_into('<H', block, offset, value)


def put32(block, offset, value):
    struct.pack_into('<I', block, offset, value)


def va_offset(va, width=4):
    if TEXT_VA <= va and va + width <= TEXT_VA + TEXT_SIZE:
        return TEXT_OFF + va - TEXT_VA
    if DATA_VA <= va and va + width <= DATA_VA + DATA_SIZE:
        return DATA_OFF + va - DATA_VA
    raise ValueError(f'{va:#x} is outside the synthetic segments')


def put_code16(image, va, value):
    put16(image, va_offset(va, 2), value)


def put_code32(image, va, value):
    put32(image, va_offset(va), value)


def put_code_bytes(image, va, data):
    offset = va_offset(va, len(data))
    image[offset:offset + len(data)] = data


def thumb_bl(site, target):
    offset = target - (site + 4)
    assert -(1 << 24) <= offset < (1 << 24) and not offset & 1
    sign = 1 if offset < 0 else 0
    if sign:
        offset += 1 << 25
    i1 = (offset >> 23) & 1
    i2 = (offset >> 22) & 1
    imm10 = (offset >> 12) & 0x3FF
    imm11 = (offset >> 1) & 0x7FF
    j1 = ((~i1) & 1) ^ sign
    j2 = ((~i2) & 1) ^ sign
    first = 0xF000 | (sign << 10) | imm10
    second = 0xD000 | (j1 << 13) | (1 << 12) | (j2 << 11) | imm11
    return first, second


def put_bl(image, site, target):
    first, second = thumb_bl(site, target)
    put_code16(image, site, first)
    put_code16(image, site + 2, second)


def build_code(image):
    put_code16(image, 0x4617468, 0x4770)
    put_code16(image, 0x461746a, 0x4770)
    put_code16(image, 0x4617488, 0x9501)
    put_code16(image, 0x461748e, 0x2109)
    put_code16(image, 0x4617490, 0x220c)
    put_code16(image, 0x4617492, 0x9600)
    put_code16(image, 0x4617494, 0x4623)
    put_code16(image, 0x461749a, 0xe9cd)
    put_code16(image, 0x461749c, 0x6500)
    put_code16(image, 0x461749e, 0x2119)
    put_code16(image, 0x46174a0, 0x2210)
    put_code16(image, 0x46174a2, 0x4623)
    put_code16(image, 0x46174a8, 0xe9cd)
    put_code16(image, 0x46174aa, 0x6500)
    put_code16(image, 0x46174ac, 0x210f)
    put_code16(image, 0x46174ae, 0x220c)
    put_code16(image, 0x46174b0, 0x4623)
    for site in (0x4617498, 0x46174a6, 0x46174b4):
        put_code16(image, site, 0x47b8)
    put_code16(image, 0x4617486, 0x4E0F)
    put_code16(image, 0x461748a, 0x4C10)
    put_code16(image, 0x461748c, 0x4F10)
    put_code16(image, 0x4617496, 0x480C)
    put_code16(image, 0x46174a4, 0x480B)
    put_code16(image, 0x46174b2, 0x4809)
    put_code32(image, 0x46174c4, 0x17469)
    put_code32(image, 0x46174cc, 0x1746b)
    put_code32(image, 0x46174d0, 0xae15)
    put_code_bytes(image, 0x46174c8, b'smpa')
    put_code_bytes(image, 0x46174d4, b'ldoa')
    put_code_bytes(image, 0x46174d8, b'clka')
    put_code16(image, 0x4698ad0, 0x4B0A)
    put_code16(image, 0x4698ace, 0x980a)
    put_code16(image, 0x4698ad2, 0x1c71)
    put_code16(image, 0x4698ad4, 0x2210)
    put_code16(image, 0x4698ad6, 0x9001)
    put_code16(image, 0x4698ad8, 0x4807)
    put_code16(image, 0x4698ada, 0x9000)
    put_code16(image, 0x4698adc, 0x9803)
    put_code32(image, 0x4698af8, 0x153b9)
    put_code32(image, 0x4698afc, 0x151f9)
    put_bl(image, 0x4615502, 0x4615906)
    put_bl(image, 0x4615970, 0x4614920)
    put_bl(image, 0x4616e8a, 0x4615d08)
    put_bl(image, 0x4615d7c, 0x4617b48)
    put_bl(image, 0x4698ade, 0x460ae14)
    put_code16(image, 0x4600100, 0x4810)
    put_code16(image, 0x4600102, 0x4F04)
    put_code16(image, 0x4600104, 0x4770)
    put_code16(image, 0x4600200, 0xF000)
    put_code16(image, 0x4600202, 0xF87E)
    put_code16(image, 0x4600240, 0xF7FF)
    put_code16(image, 0x4600242, 0xFF7E)


def build_image():
    image = bytearray(DATA_OFF + DATA_SIZE)
    image[:16] = b'\x7fELF' + bytes([1, 1, 1]) + bytes(9)
    struct.pack_into('<HHIIIIIHHHHHH', image, 16,
                     2, 40, 1, 0x4600001, 52, 0, 0, 52, 32, 3, 0, 0, 0)
    struct.pack_into('<IIIIIIII', image, 52,
                     1, TEXT_OFF, TEXT_VA, TEXT_VA, TEXT_SIZE, TEXT_SIZE, 5, 4)
    struct.pack_into('<IIIIIIII', image, 84,
                     1, DATA_OFF, DATA_VA, DATA_VA, DATA_SIZE, DATA_SIZE, 6, 4)
    struct.pack_into('<IIIIIIII', image, 116,
                     0, 0x98, 0x469d000, 0x469d000, 4, 4, 0, 4)
    put32(image, 0x98, SELECTOR)

    put_code32(image, TEXT_VA + 0x40, BOUNDARY_TABLE - BASE)

    for index in range(24):
        offset = va_offset(NAME_BASE + index * NAME_STRIDE, NAME_STRIDE)
        struct.pack_into('<4s', image, offset, b'ldoa')
        put32(image, offset + 4, index + 1)

    for index, ident in enumerate(SELECTOR_IDS):
        put32(image, va_offset(SELECTOR + 4 * index),
              NAME_BASE + (ident - 1) * NAME_STRIDE - BASE)
    put32(image, va_offset(GROUP_TABLE), SELECTOR - BASE)
    put32(image, va_offset(GROUP_TABLE + 12), SELECTOR + 8 - BASE)
    put32(image, va_offset(GROUP_TABLE + 24), SELECTOR + 0x24 - BASE)

    for index in range(24):
        offset = va_offset(VOLTAGE_TABLE + index * VOLTAGE_STRIDE, VOLTAGE_STRIDE)
        put32(image, offset, 0x62000000)
        put16(image, offset + 4, (index + 1) * 100)
        put16(image, offset + 6, (index + 1) * 100 + 100)
        put32(image, offset + 8, 0x00030000)
    l11 = va_offset(VOLTAGE_TABLE + 10 * VOLTAGE_STRIDE, VOLTAGE_STRIDE)
    put32(image, l11, 0x62018000)
    put16(image, l11 + 4, 1704)
    put16(image, l11 + 6, 1952)
    put32(image, l11 + 8, 0x00020000)
    l24 = va_offset(VOLTAGE_TABLE + 23 * VOLTAGE_STRIDE, VOLTAGE_STRIDE)
    put32(image, l24, 0x62156000)
    put16(image, l24 + 4, 2704)
    put16(image, l24 + 6, 3600)
    put32(image, l24 + 8, 0x00020000)

    put32(image, va_offset(DESCRIPTOR), VOLTAGE_TABLE - BASE)
    put32(image, va_offset(DESCRIPTOR + 4), DESCRIPTOR_SECOND - BASE)
    for index in range(82):
        put32(image, va_offset(BOUNDARY_TABLE + 4 * index), 0x1000 + 4 * index)
    put32(image, va_offset(BOUNDARY_WORD), DESCRIPTOR - BASE)
    put32(image, va_offset(THUMB_REFERENCE),
          ((NAME_BASE + 10 * NAME_STRIDE) | 1) - BASE)
    build_code(image)
    return bytes(image)


def run(image_path, *extra):
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(image_path), *extra],
        capture_output=True, text=True, check=False)


def expect_failure(image_path, needle, *extra):
    result = run(image_path, *extra)
    assert result.returncode == 1, result
    assert needle in result.stderr, result.stderr


def check_decoders(image):
    module = runpy.run_path(str(SCRIPT))
    _, loads, _ = module['parse_segments'](image)
    decode_bl = module['thumb_bl_target']
    decode_ldr = module['thumb_ldr_literal_target']
    assert decode_ldr(image, loads, 0x4600100) == 0x4600144
    assert decode_ldr(image, loads, 0x4600102) == 0x4600114
    assert decode_ldr(image, loads, 0x4600104) is None
    assert decode_bl(image, loads, 0x4600200) == 0x4600300
    assert decode_bl(image, loads, 0x4600240) == 0x4600140
    assert decode_bl(image, loads, 0x4600104) is None
    return module


def write_mutation(directory, image, name, offset, value, width=4):
    mutated = bytearray(image)
    if width == 4:
        put32(mutated, offset, value)
    elif width == 2:
        put16(mutated, offset, value)
    else:
        mutated[offset:offset + len(value)] = value
    path = Path(directory) / name
    path.write_bytes(mutated)
    return path, hashlib.sha256(mutated).hexdigest()


def main():
    image = build_image()
    digest = hashlib.sha256(image).hexdigest()
    module = check_decoders(image)

    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'synthetic-rpm.img'
        path.write_bytes(image)

        result = run(path, '--expect-sha256', digest)
        assert result.returncode == 0, result.stderr
        output = result.stdout
        assert f'sha256: {digest}' in output, output
        assert 'pinned: 0' in output, output
        assert 'nonload_segment: index=2' in output, output
        assert 'ldoa_record: id=11 va=0x46969e4' in output, output
        assert 'ldoa_record: id=24 va=0x4696a4c' in output, output
        assert 'selector: index=11 word=rt:0x969e4->0x46969e4 id=11' in output, output
        assert 'selector: index=15 word=rt:0x96a0c->0x4696a0c id=16' in output, output
        assert 'selector_terminator: va=0x46966bc word=0x0' in output, output
        assert 'group_table: index=0 word=rt:0x9667c->0x469667c' in output, output
        assert 'voltage_row: id=11 va=0x4696acc head=0x62018000 min=1704 max=1952' in output, output
        assert 'voltage_row: id=24 va=0x4696b68 head=0x62156000 min=2704 max=3600' in output, output
        assert 'descriptor: va=0x4696c40 word=rt:0x96a54->0x4696a54' in output, output
        assert 'descriptor_second: va=0x4696c44 word=rt:0x96b74->0x4696b74' in output, output
        assert 'boundary_table: va=0x4697e20 count=82 word_index=60 word=rt:0x96c40->0x4696c40' in output, output
        assert 'boundary_terminator: va=0x4697f68 word=0x0' in output, output
        assert 'class_table: va=0x4695774 count=7 nonzero=0' in output, output
        assert 'reference_count: target=l11_record count=2' in output, output
        assert 'reference: target=l11_record form=rt address=0x46966a8 word=0x969e4' in output, output
        assert 'reference: target=l11_record form=thumb_rt address=0x4696900 word=0x969e5' in output, output
        assert 'reference_count: target=l24_record count=0' in output, output
        assert 'reference_count: target=selector count=1' in output, output
        assert 'reference_count: target=group_table count=0' in output, output
        assert 'reference_count: target=voltage_table count=1' in output, output
        assert 'reference_count: target=descriptor count=1' in output, output
        assert 'reference_count: target=boundary_table count=1' in output, output
        assert 'reference: target=boundary_table form=rt address=0x4600040 word=0x97e20' in output, output
        assert 'reference_count: target=boundary_word count=0' in output, output
        assert 'entry_fields: target=l11 va=0x4696acc min=1704' in output, output
        assert 'entry_fields: target=l24 va=0x4696b68 min=2704' in output, output
        assert 'result: PASS' in output, output

        code_result = run(path, '--expect-sha256', digest, '--code')
        assert code_result.returncode == 0, code_result.stderr
        code_output = code_result.stdout
        assert 'code_edge: site=0x4615502 target=0x4615906 kind=bl' in code_output, code_output
        assert 'code_edge: site=0x4698ade target=0x460ae14 kind=bl' in code_output, code_output
        assert 'code_class: site=0x46174a0 label="ldoa type 0x10" value=0x2210' in code_output, code_output
        assert 'code_class_literal: site=0x46174c4 value=0x17469 target=0x4617468' in code_output, code_output
        assert 'code_stub: site=0x4617468 instruction=bx_lr' in code_output, code_output
        assert 'code_builder_literal: site=0x4698af8 value=0x153b9 target=0x46153b8' in code_output, code_output
        assert 'code_literal_load: site=0x461748c dest=r7 target=0x46174d0' in code_output, code_output
        assert 'code_literal_load: site=0x4698ad0 dest=r3 target=0x4698afc' in code_output, code_output
        assert 'code_setup: site=0x4617494 label="smpa registration argument r3 from r4" value=0x4623' in code_output, code_output
        assert 'code_setup: site=0x461749a label="ldoa registration stack pair low" value=0xe9cd' in code_output, code_output
        assert 'code_setup: site=0x46174a2 label="ldoa registration argument r3 from r4" value=0x4623' in code_output, code_output
        assert 'code_setup: site=0x46174a8 label="clka registration stack pair low" value=0xe9cd' in code_output, code_output
        assert 'code_setup: site=0x4698ace label="builder object argument [sp,#0x28]" value=0x980a' in code_output, code_output
        assert 'code_setup: site=0x4698ad2 label="builder instance count r6+1" value=0x1c71' in code_output, code_output
        assert 'code_setup: site=0x4698ad4 label="builder ldoa type 0x10" value=0x2210' in code_output, code_output
        assert 'code_setup: site=0x4698ad6 label="builder object stack argument [sp,#4]" value=0x9001' in code_output, code_output
        assert 'code_setup: site=0x4698ada label="builder callback stack argument [sp]" value=0x9000' in code_output, code_output
        assert 'code_setup: site=0x4698adc label="builder registration argument [sp,#0xc]" value=0x9803' in code_output, code_output
        assert 'result: PASS' in code_output, code_output

        second = run(path, '--expect-sha256', digest)
        assert second.returncode == 0, second.stderr
        assert second.stdout == output

        expect_failure(path, 'does not match')

        mutated = bytearray(image)
        mutated[va_offset(NAME_BASE) + 4] ^= 0x01
        mutated_path = Path(directory) / 'mutated-rpm.img'
        mutated_path.write_bytes(mutated)
        mutated_digest = hashlib.sha256(mutated).hexdigest()
        expect_failure(mutated_path, 'anchor: FAIL', '--expect-sha256', mutated_digest)

        mapping = bytearray(image)
        put32(mapping, 84 + 8, DATA_VA + 0x20000)
        put32(mapping, 84 + 12, DATA_VA + 0x20000)
        mapping_path = Path(directory) / 'mapped-rpm.img'
        mapping_path.write_bytes(mapping)
        mapping_digest = hashlib.sha256(mapping).hexdigest()
        expect_failure(mapping_path, 'not mapped', '--expect-sha256', mapping_digest)

        selector, selector_digest = write_mutation(
            directory, image, 'selector-rpm.img', va_offset(SELECTOR + 4), 0)
        expect_failure(selector, 'anchor: FAIL', '--expect-sha256', selector_digest)

        voltage, voltage_digest = write_mutation(
            directory, image, 'voltage-rpm.img', va_offset(VOLTAGE_TABLE + 10 * VOLTAGE_STRIDE + 4),
            1700, width=2)
        expect_failure(voltage, 'anchor: FAIL', '--expect-sha256', voltage_digest)

        boundary, boundary_digest = write_mutation(
            directory, image, 'boundary-rpm.img', va_offset(BOUNDARY_TABLE + 4 * 82), 1)
        expect_failure(boundary, 'anchor: FAIL', '--expect-sha256', boundary_digest)

        zero_boundary, zero_boundary_digest = write_mutation(
            directory, image, 'zero-boundary-rpm.img',
            va_offset(BOUNDARY_TABLE + 4 * 5), 0)
        expect_failure(zero_boundary, 'zero entry', '--expect-sha256', zero_boundary_digest)

        duplicate_boundary, duplicate_boundary_digest = write_mutation(
            directory, image, 'duplicate-boundary-rpm.img',
            va_offset(BOUNDARY_TABLE + 4 * 6), 0x1000 + 4 * 5)
        expect_failure(duplicate_boundary, 'not unique', '--expect-sha256',
                       duplicate_boundary_digest)

        group_duplicate, group_duplicate_digest = write_mutation(
            directory, image, 'group-rpm.img', va_offset(GROUP_TABLE + 12), SELECTOR - BASE)
        expect_failure(group_duplicate, 'group table entry 1', '--expect-sha256',
                       group_duplicate_digest)

        callback, callback_digest = write_mutation(
            directory, image, 'callback-rpm.img', va_offset(0x4698af8), 0x153b8)
        expect_failure(callback, 'builder literal', '--expect-sha256', callback_digest, '--code')

        class_type, class_digest = write_mutation(
            directory, image, 'class-rpm.img', va_offset(0x46174a0, 2), 0x220c, width=2)
        expect_failure(class_type, 'ldoa type 0x10', '--expect-sha256', class_digest, '--code')

        call_edge, edge_digest = write_mutation(
            directory, image, 'edge-rpm.img', va_offset(0x4615502, 2), 0xF001, width=2)
        expect_failure(call_edge, 'expected 0x4615906', '--expect-sha256', edge_digest, '--code')

        for site, _, label in module['CODE_SETUP']:
            setup, setup_digest = write_mutation(
                directory, image, f'setup-{site:x}-rpm.img', va_offset(site, 2),
                0x46c0, width=2)
            expect_failure(setup, label, '--expect-sha256', setup_digest, '--code')

        for site, dest, _ in module['LITERAL_LOADS']:
            new_dest = 0 if dest else 1
            original = struct.unpack_from('<H', image, va_offset(site, 2))[0]
            literal_dest, literal_dest_digest = write_mutation(
                directory, image, f'load-{site:x}-rpm.img', va_offset(site, 2),
                original ^ ((dest ^ new_dest) << 8), width=2)
            expect_failure(literal_dest, f'loads into r{new_dest}, expected r{dest}',
                           '--expect-sha256', literal_dest_digest, '--code')

        missing = Path(directory) / 'missing-rpm.img'
        expect_failure(missing, 'No such file or directory')

    print('PASS: RPM LDOA analysis anchors, table invariants, Thumb references, '
          'PT_NULL exclusion, code attribution and decoder vectors, hash gate, '
          'negative controls and determinism')


if __name__ == '__main__':
    main()
