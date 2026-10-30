#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Static host check for the SPMIDxe descriptor-base override.

The booted SPMIDxe PE is pinned by SHA-256. The test maps the documented virtual
addresses to file offsets through the PE section table, checks the instruction
words that read the 0x8878 override and conditionally write the descriptor base,
checks the initial .data layout, and scans the image for direct or bounded
ADRP/ADR-relative stores to 0x8878.

The scan is a static, image-local check. It does not execute firmware, touch a
device, or prove that no runtime agent, corrupted pointer, DMA engine or
different firmware revision can write the writable global. It does not
establish any electrical behaviour.
"""
import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = (ROOT.parent / 'Mu-Silicium' / 'Binaries' / 'gta4l' / 'QcomPkg' /
            'Drivers' / 'SPMIDxe' / 'SPMIDxe.efi')
VOLUME = ROOT / 'SILICIUM_UEFI.fd'
EXTRACTOR = ROOT / 'scripts' / 'extract-gta4l-firmware.py'

EXPECTED_SHA256 = '50be2e1a0972843af5de67d4d0b80aba68634259849459897922716a25d3e254'

TEXT_VMA = 0x1000
TEXT_SIZE = 0x7000
DATA_VMA = 0x8000
DATA_SIZE = 0x2000

CONFIG_PTR = 0x8178
DESCRIPTOR_ARRAY = 0x8140
DESCRIPTOR_BASE = 0x01C00000
CACHED_CONFIG = 0x8868
EVENT_HANDLE = 0x8870
OVERRIDE = 0x8878


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def sha256(data):
    import hashlib
    return hashlib.sha256(data).hexdigest()


def load_extractor():
    spec = importlib.util.spec_from_file_location('gta4l_firmware_extract',
                                                  EXTRACTOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_firmware():
    if FIRMWARE.is_file():
        return FIRMWARE.read_bytes(), str(FIRMWARE)
    module = load_extractor()
    prefix, outer = module.load_outer(VOLUME)
    inner, _ = module.inner_fv(outer)
    check(inner is not None, f'{VOLUME}: no inner firmware volume')
    for offset, file_bytes in module.walk_ffs(inner, strict=True):
        for typ, section in module.sections(file_bytes, strict=True):
            if typ in module.PE_TYPES:
                pe = section[4:]
                if sha256(pe) == EXPECTED_SHA256:
                    return pe, f'{VOLUME} inner FV module at 0x{offset:x}'
    raise AssertionError(
        f'{VOLUME}: no module with SPMIDxe SHA-256 {EXPECTED_SHA256}')


def pe_sections(data):
    e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
    check(data[e_lfanew:e_lfanew + 4] == b'PE\0\0',
          'firmware is not a PE image')
    count = struct.unpack_from('<H', data, e_lfanew + 6)[0]
    optional_size = struct.unpack_from('<H', data, e_lfanew + 20)[0]
    table = e_lfanew + 24 + optional_size
    sections = []
    for index in range(count):
        name, vsize, va, raw_size, raw = struct.unpack_from(
            '<8sIIII', data, table + 40 * index)
        sections.append({
            'name': name.rstrip(b'\0'),
            'vsize': vsize,
            'va': va,
            'raw_size': raw_size,
            'raw': raw,
        })
    return sections


def section(data, name):
    for item in pe_sections(data):
        if item['name'] == name:
            return item
    raise AssertionError(f'firmware has no {name!r} section')


def pe_offset(data, rva):
    for item in pe_sections(data):
        span = max(item['vsize'], item['raw_size'])
        if item['va'] <= rva < item['va'] + span:
            offset = item['raw'] + (rva - item['va'])
            check(offset + 4 <= len(data),
                  f'RVA 0x{rva:x} maps outside the file')
            return offset
    raise AssertionError(f'RVA 0x{rva:x} is outside every PE section')


def word_at(data, rva):
    return struct.unpack_from('<I', data, pe_offset(data, rva))[0]


def qword_at(data, rva):
    return struct.unpack_from('<Q', data, pe_offset(data, rva))[0]


def sign_extend(value, bits):
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def check_word(data, rva, expected, label):
    actual = word_at(data, rva)
    check(actual == expected,
          f'{label} at 0x{rva:x}: 0x{actual:08x} != 0x{expected:08x}')


def check_bl(data, rva, target):
    word = word_at(data, rva)
    check(word >> 26 == 0x25, f'0x{rva:x}: not a BL instruction')
    actual = rva + (sign_extend(word & 0x03ffffff, 26) << 2)
    check(actual == target, f'0x{rva:x}: BL target 0x{actual:x} != 0x{target:x}')


def check_override_path(data):
    check_word(data, 0x38b0, 0xb0000028, 'adrp x8, 0x8000')
    check_word(data, 0x38b4, 0xf9443d08, 'ldr x8, [x8, #0x878]')
    check_word(data, 0x38b8, 0xb4000048, 'cbz x8, 0x38c0')
    check_word(data, 0x38bc, 0xf9000008, 'str x8, [x0]')
    check_word(data, 0x38c0, 0x2a1f03e0, 'mov w0, wzr')
    check_word(data, 0x38c4, 0xd65f03c0, 'ret')

    check_word(data, 0x38c8, 0xa9be4ff4, 'stp x20, x19, [sp, #-0x20]!')
    check_word(data, 0x38d4, 0xb0000034, 'adrp x20, 0x8000')
    check_word(data, 0x38dc, 0xf940ce88, 'ldr x8, [x20, #0x198]')
    check_word(data, 0x38e0, 0xf9400d08, 'ldr x8, [x8, #0x18]')
    check_word(data, 0x38e4, 0xaa0803e0, 'mov x0, x8')
    check_bl(data, 0x38e8, 0x38b0)
    check_word(data, 0x38ec, 0xf940ce89, 'ldr x9, [x20, #0x198]')
    check_word(data, 0x38f0, 0x2a1f03e0, 'mov w0, wzr')
    check_word(data, 0x38f4, 0xf9000269, 'str x9, [x19]')

    check_word(data, 0x267c, 0xd0000033, 'adrp x19, 0x8000')
    check_word(data, 0x26a4, 0xd0000034, 'adrp x20, 0x8000')
    check_word(data, 0x26a8, 0x9113e294, 'add x20, x20, #0x4f8')
    check_word(data, 0x26ac, 0x910dc280, 'add x0, x20, #0x370')
    check_bl(data, 0x26b0, 0x38c8)

    check_word(data, 0x2704, 0xd0000021, 'adrp x1, 0x8000')
    check_word(data, 0x270c, 0x9121c021, 'add x1, x1, #0x870')
    check_bl(data, 0x2710, 0x20f0)
    check_word(data, 0x210c, 0xf900027f, 'str xzr, [x19]')
    check_word(data, 0x2188, 0xf9000260, 'str x0, [x19]')

    check_word(data, 0x2a48, 0x321c07e2, 'orr w2, wzr, #0x30')
    check_word(data, 0x2a6c, 0x8b111893, 'add x19, x4, x17, lsl #6')
    check_word(data, 0x2a70, 0x9107a260, 'add x0, x19, #0x1e8')
    check_bl(data, 0x2a78, 0x3b20)


def check_data_layout(data):
    data_section = section(data, b'.data')
    check(data_section['va'] == DATA_VMA and
          data_section['vsize'] >= DATA_SIZE,
          '.data section layout changed')
    check(qword_at(data, OVERRIDE) == 0,
          f'[0x{OVERRIDE:x}] is not zero at image load')
    check(qword_at(data, CONFIG_PTR + 0x18) == DESCRIPTOR_ARRAY,
          f'[0x{CONFIG_PTR + 0x18:x}] is not 0x{DESCRIPTOR_ARRAY:x}')
    check(qword_at(data, 0x8198) == CONFIG_PTR,
          '[0x8198] is not the config pointer')
    check(qword_at(data, DESCRIPTOR_ARRAY) == DESCRIPTOR_BASE,
          'descriptor base is not 0x01C00000')
    check(qword_at(data, DESCRIPTOR_ARRAY + 0x10) == 0x8880,
          'descriptor + 0x10 is not 0x8880')
    check(qword_at(data, DESCRIPTOR_ARRAY + 0x18) == 0x8a80,
          'descriptor + 0x18 is not 0x8a80')
    check(qword_at(data, DESCRIPTOR_ARRAY + 0x28) == 0x811e,
          'descriptor + 0x28 is not 0x811e')
    check(qword_at(data, CACHED_CONFIG) == 0,
          'cached config pointer is not zero at image load')
    check(qword_at(data, EVENT_HANDLE) == 0,
          'event handle is not zero at image load')


def decode_adrp(word, pc):
    if (word & 0x9f000000) != 0x90000000:
        return None
    immlo = (word >> 29) & 0x3
    immhi = (word >> 5) & 0x7ffff
    imm = sign_extend((immhi << 2) | immlo, 21)
    return (pc & ~0xfff) + (imm << 12), word & 0x1f


def decode_adr(word, pc):
    if (word & 0x9f000000) != 0x10000000:
        return None
    immlo = (word >> 29) & 0x3
    immhi = (word >> 5) & 0x7ffff
    imm = sign_extend((immhi << 2) | immlo, 21)
    return pc + imm, word & 0x1f


def decode_addsub(word):
    encoding = word & 0xff800000
    if encoding not in (0x91000000, 0xd1000000, 0x91400000, 0xd1400000):
        return None
    subtract = encoding in (0xd1000000, 0xd1400000)
    shift = 12 if (word >> 22) & 1 else 0
    immediate = ((word >> 10) & 0xfff) << shift
    return ('sub' if subtract else 'add'), (word >> 5) & 0x1f, word & 0x1f, immediate


def decode_mov(word):
    if (word & 0xffe0ffe0) == 0xaa0003e0:
        return (word >> 16) & 0x1f, word & 0x1f
    return None


def decode_mem(word):
    for mask, base, size, store in (
            (0xffc00000, 0xf9400000, 8, False),
            (0xffc00000, 0xf9000000, 8, True),
            (0xffc00000, 0xb9400000, 4, False),
            (0xffc00000, 0xb9000000, 4, True),
            (0xffc00000, 0x39400000, 1, False),
            (0xffc00000, 0x39000000, 1, True),
            (0xffc00000, 0x79400000, 2, False),
            (0xffc00000, 0x79000000, 2, True),
            (0xffc00000, 0xb9800000, 4, False)):
        if (word & mask) == base:
            immediate = ((word >> 10) & 0xfff) * size
            return ('store' if store else 'load', size, store,
                    (word >> 5) & 0x1f, word & 0x1f, immediate, None)
    for mask, base, size, store, index in (
            (0xffe00c00, 0xf8000000, 8, True, 0),
            (0xffe00c00, 0xf8400000, 8, False, 0),
            (0xffe00c00, 0xb8000000, 4, True, 0),
            (0xffe00c00, 0xb8400000, 4, False, 0),
            (0xffe00c00, 0x38000000, 1, True, 0),
            (0xffe00c00, 0x38400000, 1, False, 0),
            (0xffe00c00, 0x78000000, 2, True, 0),
            (0xffe00c00, 0x78400000, 2, False, 0),
            (0xffe00c00, 0xf8000c00, 8, True, 3),
            (0xffe00c00, 0xf8000400, 8, True, 1),
            (0xffe00c00, 0xf8400c00, 8, False, 3),
            (0xffe00c00, 0xf8400400, 8, False, 1)):
        if (word & mask) == base:
            immediate = sign_extend((word >> 12) & 0x1ff, 9)
            return ('store' if store else 'load', size, store,
                    (word >> 5) & 0x1f, word & 0x1f, immediate, index)
    return None


def decode_pair(word):
    for mask, base, store, index in (
            (0xffc00000, 0xa9000000, True, 0),
            (0xffc00000, 0xa9400000, False, 0),
            (0xffc00000, 0xa9800000, True, 3),
            (0xffc00000, 0xa9c00000, False, 3),
            (0xffc00000, 0xa8800000, True, 1),
            (0xffc00000, 0xa8c00000, False, 1)):
        if (word & mask) == base:
            immediate = sign_extend((word >> 15) & 0x7f, 7) * 8
            return ('store' if store else 'load', (word >> 5) & 0x1f,
                    word & 0x1f, (word >> 10) & 0x1f, immediate, index)
    return None


def is_ret(word):
    return word == 0xd65f03c0


def is_unconditional_branch(word):
    return (word & 0xfc000000) == 0x14000000


def decode_mov_immediate(word):
    encoding = word & 0x7f800000
    if encoding in (0x52800000, 0x72800000, 0xd2800000, 0xf2800000):
        return ((word >> 5) & 0xffff), ((word >> 21) & 0x3) * 16
    return None


def data_pointer(data_section_bytes, address):
    if DATA_VMA <= address < DATA_VMA + len(data_section_bytes) - 7:
        value = struct.unpack_from(
            '<Q', data_section_bytes, address - DATA_VMA)[0]
        if value:
            return value
    return None


def scan_references(data):
    text_section = section(data, b'.text')
    text = data[text_section['raw']:
                text_section['raw'] + text_section['raw_size']]
    data_section = section(data, b'.data')
    data_bytes = data[data_section['raw']:
                      data_section['raw'] + data_section['raw_size']]
    accesses = []
    stores = []
    for offset in range(0, len(text), 4):
        pc = TEXT_VMA + offset
        word = struct.unpack_from('<I', text, offset)[0]
        adrp = decode_adrp(word, pc)
        adr = decode_adr(word, pc)
        if not adrp and not adr:
            continue
        target, register = adrp if adrp else adr
        values = {register: target}
        for step in range(1, 28):
            current = pc + 4 * step
            if current >= TEXT_VMA + len(text):
                break
            current_word = struct.unpack_from(
                '<I', text, current - TEXT_VMA)[0]
            mem = decode_mem(current_word)
            if mem:
                kind, size, store, base, rt, immediate, index = mem
                if base in values:
                    address = values[base] + immediate
                    accesses.append((current, kind, size, address))
                    if store:
                        stores.append((current, size, address))
                if not store and rt in values:
                    address = (values[base] + immediate
                               if base in values else None)
                    pointer = (data_pointer(data_bytes, address)
                               if address is not None else None)
                    if pointer is not None:
                        values[rt] = pointer
                    elif rt != register:
                        del values[rt]
                if index in (1, 3) and base in values:
                    del values[base]
                if is_ret(current_word) or is_unconditional_branch(current_word):
                    break
                continue
            pair = decode_pair(current_word)
            if pair:
                kind, base, rt, rt2, immediate, index = pair
                if base in values:
                    address = values[base] + immediate
                    accesses.append((current, kind, 8, address))
                    if kind == 'store':
                        stores.append((current, 8, address))
                        stores.append((current, 8, address + 8))
                    else:
                        for item in (rt, rt2):
                            if item in values:
                                del values[item]
                if index in (1, 3) and base in values:
                    del values[base]
                if is_ret(current_word) or is_unconditional_branch(current_word):
                    break
                continue
            addsub = decode_addsub(current_word)
            if addsub:
                operation, base, destination, immediate = addsub
                if base in values:
                    values[destination] = values[base] + (
                        immediate if operation == 'add' else -immediate)
                elif destination in values and destination != register:
                    del values[destination]
                if is_ret(current_word) or is_unconditional_branch(current_word):
                    break
                continue
            mov = decode_mov(current_word)
            if mov:
                source, destination = mov
                if source in values:
                    values[destination] = values[source]
                elif destination in values and destination != register:
                    del values[destination]
                if is_ret(current_word) or is_unconditional_branch(current_word):
                    break
                continue
            if is_ret(current_word) or is_unconditional_branch(current_word):
                break
    return accesses, stores


def count_adrp(data, page):
    text_section = section(data, b'.text')
    text = data[text_section['raw']:
                text_section['raw'] + text_section['raw_size']]
    count = 0
    for offset in range(0, len(text), 4):
        word = struct.unpack_from('<I', text, offset)[0]
        decoded = decode_adrp(word, TEXT_VMA + offset)
        if decoded and decoded[0] == page:
            count += 1
    return count


def check_scan(data):
    check(count_adrp(data, 0x8000) == 64,
          'ADRP-to-0x8000 site count changed')
    accesses, stores = scan_references(data)
    hits = [item for item in accesses if item[3] == OVERRIDE]
    check(len(hits) == 1,
          f'expected one reference to 0x{OVERRIDE:x}, found {len(hits)}')
    check(hits[0][0] == 0x38b4 and hits[0][1] == 'load' and hits[0][2] == 8,
          f'0x{OVERRIDE:x} reference is not the 0x38b4 8-byte load')
    check(not [item for item in stores if item[2] == OVERRIDE],
          f'static scan found a store to 0x{OVERRIDE:x}')
    resolved = [item[2] for item in stores
                if DATA_VMA <= item[2] < DATA_VMA + DATA_SIZE]
    check(resolved, 'static scan resolved no .data stores')
    check(max(resolved) < OVERRIDE,
          'static scan resolved a .data store at or above the override')
    check(not [item for item in accesses
               if item[3] == OVERRIDE and item[1] == 'store'],
          'override was resolved as a store access')


def check_negative_control(data):
    mutated = bytearray(data)
    struct.pack_into('<I', mutated, pe_offset(mutated, 0x38b4), 0xf9043d08)
    accesses, stores = scan_references(bytes(mutated))
    check([item for item in stores if item[2] == OVERRIDE],
          'scanner did not detect a synthetic store to 0x8878')
    check([item for item in accesses
           if item[3] == OVERRIDE and item[1] == 'store'],
          'scanner did not classify the synthetic 0x8878 access as a store')


def check_constant_absence(data):
    raw = data
    for width in (8, 4):
        needle = OVERRIDE.to_bytes(width, 'little')
        check(needle not in raw,
              f'{width}-byte literal 0x{OVERRIDE:x} appears in the image')

    text_section = section(data, b'.text')
    text = data[text_section['raw']:
                text_section['raw'] + text_section['raw_size']]
    for offset in range(0, len(text), 4):
        word = struct.unpack_from('<I', text, offset)[0]
        immediate = decode_mov_immediate(word)
        if immediate is not None:
            value, shift = immediate
            check(not (value == OVERRIDE and shift == 0),
                  f'MOVZ/MOVK builds 0x{OVERRIDE:x} at 0x{TEXT_VMA + offset:x}')

    reloc = section(data, b'.reloc')
    block = data[reloc['raw']:reloc['raw'] + reloc['raw_size']]
    position = 0
    targets = []
    while position + 8 <= len(block):
        page, size = struct.unpack_from('<II', block, position)
        if page == 0 and size == 0:
            break
        check(size >= 8 and position + size <= len(block),
              'malformed .reloc block')
        for index in range((size - 8) // 2):
            entry = struct.unpack_from(
                '<H', block, position + 8 + 2 * index)[0]
            targets.append(page + (entry & 0xfff))
        position += size
    check(OVERRIDE not in targets,
          f'.reloc contains an entry for 0x{OVERRIDE:x}')


def apply_override(base, override):
    return override if override else base


def check_model():
    check(apply_override(DESCRIPTOR_BASE, 0) == DESCRIPTOR_BASE,
          'zero override changed the descriptor base')
    check(apply_override(DESCRIPTOR_BASE, 0x1234) == 0x1234,
          'nonzero override did not replace the descriptor base')


def main():
    data, source = load_firmware()
    actual = sha256(data)
    check(actual == EXPECTED_SHA256,
          f'{source}: SHA-256 {actual} != {EXPECTED_SHA256}')
    text = section(data, b'.text')
    check(text['va'] == TEXT_VMA and text['vsize'] == TEXT_SIZE,
          '.text section layout changed')
    check_override_path(data)
    check_data_layout(data)
    check_scan(data)
    check_negative_control(data)
    check_constant_absence(data)
    check_model()
    print(f'spmidxe descriptor base: sha256={actual} source={source} PASS')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except AssertionError as error:
        sys.exit(f'test-gta4l-spmidxe-descriptor-base: {error}')
