#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Self-test for scripts/analyze-gta4l-rpm-ldoa.py.

The test is host-only and uses a synthetic ELF32 ARM image. It checks that the
analyzer:

- accepts the expected table layout and reports the L11/L24 reference counts;
- decodes the selector, group table, voltage rows, descriptor and boundary word;
- reports the runtime class table as zero;
- rejects a wrong image hash and a changed name record;
- produces deterministic output.

No private firmware is read or vendored.
"""
import hashlib
import importlib.util
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
BOUNDARY_TABLE = 0x4697e20
BOUNDARY_WORD = 0x4697f10
CLASS_TABLE = 0x4695774
TEXT_VA = 0x4600000
DATA_VA = 0x4690000
TEXT_OFF = 0x100
DATA_OFF = 0x200
TEXT_SIZE = 0x100
DATA_SIZE = 0xc000
SELECTOR_IDS = [6, 8, 1, 2, 3, 4, 7, 17, 18, 9, 10, 11, 12, 13, 14, 16]


def load_module():
    spec = importlib.util.spec_from_file_location('gta4l_rpm_ldoa', SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def put16(block, offset, value):
    struct.pack_into('<H', block, offset, value)


def put32(block, offset, value):
    struct.pack_into('<I', block, offset, value)


def build_image():
    image = bytearray(TEXT_OFF + TEXT_SIZE + DATA_SIZE)
    image[:16] = b'\x7fELF' + bytes([1, 1, 1]) + bytes(9)
    struct.pack_into('<HHIIIIIHHHHHH', image, 16,
                     2, 40, 1, 0, 52, 0, 0, 52, 32, 2, 0, 0, 0)
    struct.pack_into('<IIIIIIII', image, 52,
                     1, TEXT_OFF, TEXT_VA, TEXT_VA, TEXT_SIZE, TEXT_SIZE, 5, 0x1000)
    struct.pack_into('<IIIIIIII', image, 84,
                     1, DATA_OFF, DATA_VA, DATA_VA, DATA_SIZE, DATA_SIZE, 6, 0x1000)

    text = memoryview(image)[TEXT_OFF:TEXT_OFF + TEXT_SIZE]
    put32(text, 0x40, BOUNDARY_TABLE - BASE)

    data = memoryview(image)[DATA_OFF:DATA_OFF + DATA_SIZE]
    for index in range(24):
        offset = NAME_BASE - DATA_VA + index * NAME_STRIDE
        struct.pack_into('<4s', image, DATA_OFF + offset, b'ldoa')
        put32(data, offset + 4, index + 1)

    for index, ident in enumerate(SELECTOR_IDS):
        put32(data, SELECTOR - DATA_VA + 4 * index,
              NAME_BASE + (ident - 1) * NAME_STRIDE - BASE)

    put32(data, GROUP_TABLE - DATA_VA, SELECTOR - BASE)

    for index in range(24):
        offset = VOLTAGE_TABLE - DATA_VA + index * VOLTAGE_STRIDE
        put32(data, offset, 0x62000000)
        put16(data, offset + 4, (index + 1) * 100)
        put16(data, offset + 6, (index + 1) * 100 + 100)
        put32(data, offset + 8, 0x00030000)
    put16(data, VOLTAGE_TABLE - DATA_VA + 10 * VOLTAGE_STRIDE + 4, 1704)
    put16(data, VOLTAGE_TABLE - DATA_VA + 10 * VOLTAGE_STRIDE + 6, 1952)
    put16(data, VOLTAGE_TABLE - DATA_VA + 23 * VOLTAGE_STRIDE + 4, 2704)
    put16(data, VOLTAGE_TABLE - DATA_VA + 23 * VOLTAGE_STRIDE + 6, 3600)

    put32(data, DESCRIPTOR - DATA_VA, VOLTAGE_TABLE - BASE)
    put32(data, BOUNDARY_WORD - DATA_VA, DESCRIPTOR - BASE)
    return bytes(image)


def run(image_path, *extra):
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(image_path), *extra],
        capture_output=True, text=True, check=False)


def expect_failure(image_path, needle, *extra):
    result = run(image_path, *extra)
    assert result.returncode == 1, result
    assert needle in result.stderr, result.stderr


def main():
    module = load_module()
    image = build_image()
    digest = hashlib.sha256(image).hexdigest()

    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'synthetic-rpm.img'
        path.write_bytes(image)

        result = run(path, '--expect-sha256', digest)
        assert result.returncode == 0, result.stderr
        output = result.stdout
        assert f'sha256: {digest}' in output, output
        assert 'ldoa_record: id=11 va=0x46969e4' in output, output
        assert 'ldoa_record: id=24 va=0x4696a4c' in output, output
        assert 'selector: index=11 word=rt:0x969e4->0x46969e4 id=11' in output, output
        assert 'selector: index=15 word=rt:0x96a0c->0x4696a0c id=16' in output, output
        assert 'group_table: index=0 word=rt:0x9667c->0x469667c' in output, output
        assert 'voltage_row: id=11 va=0x4696acc head=0x62000000 min=1704 max=1952' in output, output
        assert 'voltage_row: id=24 va=0x4696b68 head=0x62000000 min=2704 max=3600' in output, output
        assert 'descriptor: va=0x4696c40 word=rt:0x96a54->0x4696a54' in output, output
        assert 'boundary_table: va=0x4697e20 count=82 word_index=60 word=rt:0x96c40->0x4696c40' in output, output
        assert 'class_table: va=0x4695774 count=7 nonzero=0' in output, output
        assert 'reference_count: target=l11_record count=1' in output, output
        assert 'reference: target=l11_record form=rt address=0x46966a8 word=0x969e4' in output, output
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

        second = run(path, '--expect-sha256', digest)
        assert second.returncode == 0, second.stderr
        assert second.stdout == output

        expect_failure(path, 'does not match')

        mutated = bytearray(image)
        mutated[DATA_OFF + (NAME_BASE - DATA_VA) + 4] ^= 0x01
        mutated_path = Path(directory) / 'mutated-rpm.img'
        mutated_path.write_bytes(mutated)
        mutated_digest = hashlib.sha256(mutated).hexdigest()
        expect_failure(mutated_path, 'anchor: FAIL', '--expect-sha256', mutated_digest)

    missing = Path(tempfile.gettempdir()) / 'gta4l-rpm-ldoa-missing.img'
    if missing.exists():
        missing.unlink()
    expect_failure(missing, 'No such file or directory')

    print('PASS: RPM LDOA analysis anchors, reference counts, hash gate and '
          'determinism')


if __name__ == '__main__':
    main()
