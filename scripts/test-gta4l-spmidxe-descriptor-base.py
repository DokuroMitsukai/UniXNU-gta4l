#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Static host check for the SPMIDxe descriptor-base override.

The booted SPMIDxe PE is pinned by SHA-256. The test maps the documented virtual
addresses to file offsets through the PE section table, checks the instruction
words that read the 0x8878 override and conditionally write the descriptor base,
checks the initial .data layout, and runs a bounded scan for writes that
intersect the 8-byte override.

The scan models direct ADRP/ADR page references, ADD/SUB immediate arithmetic,
general-purpose loads and stores with immediate or pre/post-index addressing,
pointer values loaded from the initial .data image, caller-saved clobbers at
calls, direct conditional/unconditional branches, and bounded direct BL
recursion into .text. It models a conservative whitelist of register-only
instructions; any other instruction stops the path and is reported. ADR/ADRP
with an XZR destination do not seed a tracked register. Stores invalidate the
.data pointer slots they overlap, and modeled calls propagate that invalidation
to the caller; indirect or truncated calls conservatively invalidate every
.data pointer slot. Conditional-branch paths are scanned independently, so a
store on one path does not invalidate pointer values on its sibling; the union
of path invalidations is propagated when the function returns. The scan reports
unresolved non-stack loads and stores separately, and reports every
revisited-state, step, budget, call-depth, indirect-branch and
unsupported-instruction truncation.

The scan is a static, image-local check. It does not execute firmware, touch a
device, or prove that no runtime agent, corrupted pointer, DMA engine,
unmodeled instruction or different firmware revision can write the writable
global. It does not establish any electrical behaviour.
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
OVERRIDE_END = OVERRIDE + 8

MAX_STEPS = 48
MAX_CALL_DEPTH = 2
MAX_BUDGET = 64
CALLER_SAVED = frozenset(range(19))
STACK_BASES = frozenset((29, 31))
MASK64 = (1 << 64) - 1


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


def firmware_data(data):
    data_section = section(data, b'.data')
    return data[data_section['raw']:
                data_section['raw'] + data_section['raw_size']]


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


def write_intersects(address, size, start, end):
    address &= MASK64
    limit = address + size
    if address < end and start < min(limit, 1 << 64):
        return True
    return limit > (1 << 64) and start < limit - (1 << 64)


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
    check_word(data, 0x38d8, 0xaa0003f3, 'mov x19, x0')
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
    return ((pc & ~0xfff) + (imm << 12)) & MASK64, word & 0x1f


def decode_adr(word, pc):
    if (word & 0x9f000000) != 0x10000000:
        return None
    immlo = (word >> 29) & 0x3
    immhi = (word >> 5) & 0x7ffff
    imm = sign_extend((immhi << 2) | immlo, 21)
    return (pc + imm) & MASK64, word & 0x1f


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
    for mask, base, size, store, index in (
            (0xffc00000, 0xa9000000, 8, True, 0),
            (0xffc00000, 0xa9400000, 8, False, 0),
            (0xffc00000, 0xa9800000, 8, True, 3),
            (0xffc00000, 0xa9c00000, 8, False, 3),
            (0xffc00000, 0xa8800000, 8, True, 1),
            (0xffc00000, 0xa8c00000, 8, False, 1),
            (0xffc00000, 0x29000000, 4, True, 0),
            (0xffc00000, 0x29400000, 4, False, 0),
            (0xffc00000, 0x29800000, 4, True, 3),
            (0xffc00000, 0x29c00000, 4, False, 3),
            (0xffc00000, 0x28800000, 4, True, 1),
            (0xffc00000, 0x28c00000, 4, False, 1)):
        if (word & mask) == base:
            immediate = sign_extend((word >> 15) & 0x7f, 7) * size
            return ('store' if store else 'load', size, (word >> 5) & 0x1f,
                    word & 0x1f, (word >> 10) & 0x1f, immediate, index)
    return None


def is_ret(word):
    return word == 0xd65f03c0


def is_unconditional_branch(word):
    return (word & 0xfc000000) == 0x14000000


def is_bl(word):
    return (word & 0xfc000000) == 0x94000000


def is_blr(word):
    return (word & 0xfffffc1f) == 0xd63f0000


def is_br(word):
    return (word & 0xfffffc1f) == 0xd61f0000


def is_conditional_branch(word):
    if (word & 0xff000010) == 0x54000000:
        return True
    if (word & 0x7e000000) in (0x34000000, 0x35000000):
        return True
    if (word & 0x7f000000) in (0x36000000, 0x37000000):
        return True
    return False


def branch_target(word, pc):
    if (word & 0xfc000000) in (0x14000000, 0x94000000):
        return pc + (sign_extend(word & 0x03ffffff, 26) << 2)
    if (word & 0xff000010) == 0x54000000:
        return pc + (sign_extend((word >> 5) & 0x7ffff, 19) << 2)
    if (word & 0x7e000000) in (0x34000000, 0x35000000):
        return pc + (sign_extend((word >> 5) & 0x7ffff, 19) << 2)
    if (word & 0x7f000000) in (0x36000000, 0x37000000):
        return pc + (sign_extend((word >> 5) & 0x3fff, 14) << 2)
    return None


def decode_mov_immediate(word):
    encoding = word & 0x7f800000
    if encoding in (0x52800000, 0x72800000, 0xd2800000, 0xf2800000):
        return ((word >> 5) & 0xffff), ((word >> 21) & 0x3) * 16
    return None


def decode_register_only(word):
    if word == 0xd503201f:
        return -1
    if (word & 0x1f800000) == 0x12800000:
        return word & 0x1f
    if (word & 0x1f000000) == 0x0a000000:
        return word & 0x1f
    if (word & 0x1f800000) == 0x12000000:
        return word & 0x1f
    if (word & 0x1f000000) == 0x11000000:
        return word & 0x1f
    if (word & 0x1f000000) == 0x0b000000:
        return word & 0x1f
    if (word & 0x1f000000) == 0x1b000000:
        return word & 0x1f
    if (word & 0x1f800000) == 0x13000000:
        return word & 0x1f
    if (word & 0x1f800000) == 0x13800000:
        return word & 0x1f
    return None


def slot_mask(address, size, data_len):
    address &= MASK64
    data_end = DATA_VMA + data_len
    limit = address + size
    ranges = [(address, min(limit, 1 << 64))]
    if limit > (1 << 64):
        ranges.append((0, limit - (1 << 64)))
    mask = 0
    for start, end in ranges:
        start = max(start, DATA_VMA)
        end = min(end, data_end)
        if start >= end:
            continue
        first = (start - DATA_VMA) // 8
        last = (end - 1 - DATA_VMA) // 8
        mask |= ((1 << (last - first + 1)) - 1) << first
    return mask


def data_pointer(data_bytes, address, invalid_mask):
    data_len = len(data_bytes)
    if DATA_VMA <= address <= DATA_VMA + data_len - 8:
        if invalid_mask & slot_mask(address, 8, data_len):
            return None
        value = struct.unpack_from(
            '<Q', data_bytes, address - DATA_VMA)[0]
        if value:
            return value
    return None


class ScanResult:
    def __init__(self):
        self.accesses = []
        self.stores = []
        self.calls = []
        self.boundaries = []
        self.unresolved = []
        self.clobbers = []
        self._access_keys = set()
        self._store_keys = set()
        self._call_keys = set()
        self._boundary_keys = set()
        self._unresolved_keys = set()
        self._clobber_keys = set()

    def add_access(self, pc, kind, size, address):
        key = (pc, kind, size, address)
        if key not in self._access_keys:
            self._access_keys.add(key)
            self.accesses.append(key)

    def add_store(self, pc, size, address):
        key = (pc, size, address)
        if key not in self._store_keys:
            self._store_keys.add(key)
            self.stores.append(key)

    def add_call(self, pc, target, args):
        key = (pc, target, tuple(sorted(args.items())))
        if key not in self._call_keys:
            self._call_keys.add(key)
            self.calls.append((pc, target, args))

    def add_boundary(self, pc, reason):
        key = (pc, reason)
        if key not in self._boundary_keys:
            self._boundary_keys.add(key)
            self.boundaries.append(key)

    def add_unresolved(self, pc, kind, size):
        key = (pc, kind, size)
        if key not in self._unresolved_keys:
            self._unresolved_keys.add(key)
            self.unresolved.append(key)

    def add_clobber(self, pc, register):
        key = (pc, register)
        if key not in self._clobber_keys:
            self._clobber_keys.add(key)
            self.clobbers.append(key)


def text_word(text, pc):
    return struct.unpack_from('<I', text, pc - TEXT_VMA)[0]


def scan_path(result, text, data_bytes, pc, values, invalid, depth,
              call_stack, steps, budget, visited, all_invalid,
              may_invalid=None):
    if may_invalid is None:
        may_invalid = invalid
    key = (pc, tuple(sorted(values.items())), invalid, depth)
    if key in visited:
        result.add_boundary(pc, 'visited')
        return all_invalid
    visited[key] = True
    return _scan_path(result, text, data_bytes, pc, values, invalid,
                      depth, call_stack, steps, budget, visited,
                      all_invalid, may_invalid)


def _scan_path(result, text, data_bytes, pc, values, invalid, depth,
               call_stack, steps, budget, visited, all_invalid, may_invalid):
    while steps > 0:
        if not (TEXT_VMA <= pc < TEXT_VMA + len(text)):
            result.add_boundary(pc, 'outside text')
            return all_invalid
        word = text_word(text, pc)
        if is_ret(word):
            return may_invalid
        if is_unconditional_branch(word):
            target = branch_target(word, pc)
            if target is None or not (TEXT_VMA <= target < TEXT_VMA + len(text)):
                result.add_boundary(pc, 'branch')
                return all_invalid
            if budget <= 0:
                result.add_boundary(pc, 'budget')
                return all_invalid
            return scan_path(result, text, data_bytes, target, dict(values),
                             invalid, depth, call_stack, steps - 1,
                             budget - 1, visited, all_invalid, may_invalid)
        if is_bl(word):
            target = branch_target(word, pc)
            args = {register: values[register]
                    for register in range(8) if register in values}
            result.add_call(pc, target, args)
            callee_invalid = all_invalid
            if target is None or not (TEXT_VMA <= target < TEXT_VMA + len(text)):
                result.add_boundary(pc, 'call')
            elif budget <= 0:
                result.add_boundary(pc, 'budget')
            elif depth >= MAX_CALL_DEPTH or target in call_stack:
                result.add_boundary(pc, 'call-depth')
            else:
                callee_invalid = scan_path(
                    result, text, data_bytes, target, dict(args), invalid,
                    depth + 1, call_stack | {target}, MAX_STEPS,
                    budget - 1, visited, all_invalid, invalid)
            invalid |= callee_invalid
            may_invalid |= callee_invalid
            for register in CALLER_SAVED:
                values.pop(register, None)
            pc += 4
            steps -= 1
            continue
        if is_blr(word) or is_br(word):
            result.add_boundary(pc, 'indirect branch')
            if is_br(word):
                return all_invalid
            invalid = all_invalid
            may_invalid = all_invalid
            for register in CALLER_SAVED:
                values.pop(register, None)
            pc += 4
            steps -= 1
            continue
        if is_conditional_branch(word):
            target = branch_target(word, pc)
            if target is None or not (TEXT_VMA <= target < TEXT_VMA + len(text)):
                result.add_boundary(pc, 'branch')
                may_invalid |= all_invalid
            elif budget <= 0:
                result.add_boundary(pc, 'budget')
                may_invalid |= all_invalid
            else:
                may_invalid |= scan_path(
                    result, text, data_bytes, target, dict(values), invalid,
                    depth, call_stack, steps - 1, budget - 1, visited,
                    all_invalid, invalid)
            pc += 4
            steps -= 1
            continue
        adrp = decode_adrp(word, pc)
        adr = decode_adr(word, pc)
        if adrp or adr:
            target, register = adrp if adrp else adr
            if register != 31:
                values[register] = target
            pc += 4
            steps -= 1
            continue
        mem = decode_mem(word)
        if mem:
            kind, size, store, base, rt, immediate, index = mem
            if base not in values:
                if store:
                    if base not in STACK_BASES:
                        result.add_unresolved(pc, kind, size)
                        invalid = all_invalid
                        may_invalid = all_invalid
                else:
                    if base not in STACK_BASES:
                        result.add_unresolved(pc, kind, size)
                    if rt != 31:
                        values.pop(rt, None)
                pc += 4
                steps -= 1
                continue
            base_value = values[base] & MASK64
            if index == 1:
                address = base_value
                new_base = (base_value + immediate) & MASK64
            else:
                address = (base_value + immediate) & MASK64
                new_base = address if index == 3 else base_value
            result.add_access(pc, kind, size, address)
            if store:
                result.add_store(pc, size, address)
                mask = slot_mask(address, size, len(data_bytes))
                invalid |= mask
                may_invalid |= mask
            else:
                pointer = (data_pointer(data_bytes, address, invalid)
                           if size == 8 else None)
                if rt != 31:
                    if pointer is not None:
                        values[rt] = pointer
                    else:
                        values.pop(rt, None)
            if index in (1, 3) and base != 31:
                values[base] = new_base
            pc += 4
            steps -= 1
            continue
        pair = decode_pair(word)
        if pair:
            kind, size, base, rt, rt2, immediate, index = pair
            if base not in values:
                if kind == 'store':
                    if base not in STACK_BASES:
                        result.add_unresolved(pc, kind, size)
                        invalid = all_invalid
                        may_invalid = all_invalid
                else:
                    if base not in STACK_BASES:
                        result.add_unresolved(pc, kind, size)
                    if rt != 31:
                        values.pop(rt, None)
                    if rt2 != 31:
                        values.pop(rt2, None)
                pc += 4
                steps -= 1
                continue
            base_value = values[base] & MASK64
            if index == 1:
                address = base_value
                new_base = (base_value + immediate) & MASK64
            else:
                address = (base_value + immediate) & MASK64
                new_base = address if index == 3 else base_value
            result.add_access(pc, kind, size, address)
            result.add_access(pc, kind, size, (address + size) & MASK64)
            if kind == 'store':
                result.add_store(pc, size, address)
                result.add_store(pc, size, (address + size) & MASK64)
                mask = slot_mask(address, 2 * size, len(data_bytes))
                invalid |= mask
                may_invalid |= mask
            else:
                for offset, register in ((0, rt), (size, rt2)):
                    if register == 31:
                        continue
                    pointer = (data_pointer(data_bytes, address + offset,
                                            invalid)
                               if size == 8 else None)
                    if pointer is not None:
                        values[register] = pointer
                    else:
                        values.pop(register, None)
            if index in (1, 3) and base != 31:
                values[base] = new_base
            pc += 4
            steps -= 1
            continue
        addsub = decode_addsub(word)
        if addsub:
            operation, base, destination, immediate = addsub
            if destination != 31:
                if base in values:
                    values[destination] = (values[base] + (
                        immediate if operation == 'add' else -immediate)) & MASK64
                else:
                    values.pop(destination, None)
            pc += 4
            steps -= 1
            continue
        mov = decode_mov(word)
        if mov:
            source, destination = mov
            if destination != 31:
                if source != 31 and source in values:
                    values[destination] = values[source]
                else:
                    values.pop(destination, None)
            pc += 4
            steps -= 1
            continue
        register_only = decode_register_only(word)
        if register_only is not None:
            if register_only >= 0 and register_only != 31:
                if register_only in values:
                    values.pop(register_only, None)
                    result.add_clobber(pc, register_only)
            pc += 4
            steps -= 1
            continue
        result.add_boundary(pc, 'unsupported')
        return all_invalid
    result.add_boundary(pc, 'steps')
    return all_invalid


def scan_references(data):
    text_section = section(data, b'.text')
    text = data[text_section['raw']:
                text_section['raw'] + text_section['raw_size']]
    data_section = section(data, b'.data')
    data_bytes = data[data_section['raw']:
                      data_section['raw'] + data_section['raw_size']]
    all_invalid = (1 << (len(data_bytes) // 8)) - 1
    result = ScanResult()
    for offset in range(0, len(text), 4):
        pc = TEXT_VMA + offset
        word = struct.unpack_from('<I', text, offset)[0]
        adrp = decode_adrp(word, pc)
        adr = decode_adr(word, pc)
        if not adrp and not adr:
            continue
        target, register = adrp if adrp else adr
        if register == 31:
            continue
        scan_path(result, text, data_bytes, pc, {register: target}, 0,
                  0, frozenset(), MAX_STEPS, MAX_BUDGET, {}, all_invalid)
    return result


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


def stores_intersecting_override(result):
    return [item for item in result.stores
            if write_intersects(item[2], item[1], OVERRIDE, OVERRIDE_END)]


def check_scan(data):
    check(count_adrp(data, 0x8000) == 64,
          'ADRP-to-0x8000 site count changed')
    result = scan_references(data)
    hits = [item for item in result.accesses if item[3] == OVERRIDE]
    check(len(hits) == 1,
          f'expected one reference to 0x{OVERRIDE:x}, found {len(hits)}')
    check(hits[0][0] == 0x38b4 and hits[0][1] == 'load' and hits[0][2] == 8,
          f'0x{OVERRIDE:x} reference is not the 0x38b4 8-byte load')
    writes = stores_intersecting_override(result)
    check(not writes,
          f'static scan found a write intersecting 0x{OVERRIDE:x}: {writes}')
    check(not [item for item in result.accesses
               if item[3] == OVERRIDE and item[1] == 'store'],
          'override was resolved as a store access')
    check((0x38bc, 8, DESCRIPTOR_ARRAY) in result.stores,
          'descriptor-base store at 0x38bc was not resolved')
    check((0x38f4, 8, CACHED_CONFIG) in result.stores,
          'cached-config store at 0x38f4 was not resolved')
    check((0x210c, 8, EVENT_HANDLE) in result.stores,
          'event-handle store at 0x210c was not resolved')
    check((0x2188, 8, EVENT_HANDLE) in result.stores,
          'event-handle store at 0x2188 was not resolved')
    check(any(pc == 0x38e8 and target == 0x38b0 and
              args.get(0) == DESCRIPTOR_ARRAY
              for pc, target, args in result.calls),
          '0x38e8 call argument is not the descriptor array')
    check(any(pc == 0x26b0 and target == 0x38c8 and
              args.get(0) == CACHED_CONFIG
              for pc, target, args in result.calls),
          '0x26b0 call argument is not the cached config')
    check(any(pc == 0x2710 and target == 0x20f0 and
              args.get(1) == EVENT_HANDLE
              for pc, target, args in result.calls),
          '0x2710 call argument is not the event handle')
    check(any(item[1] == 'load' for item in result.unresolved),
          'scan did not report an unresolved load')
    check(any(item[1] == 'store' for item in result.unresolved),
          'scan did not report an unresolved store')
    check(any(reason == 'unsupported' for _, reason in result.boundaries),
          'scan did not report an unsupported instruction')
    check(any(reason == 'steps' for _, reason in result.boundaries),
          'scan did not report step exhaustion')
    check(any(reason == 'visited' for _, reason in result.boundaries),
          'scan did not report a revisited state')
    return result


def encode_adrp(pc, target, register):
    delta = target - (pc & ~0xfff)
    check(delta % 0x1000 == 0, 'ADRP target is not page aligned')
    immediate = delta >> 12
    return (0x90000000 | ((immediate & 0x3) << 29) |
            (((immediate >> 2) & 0x7ffff) << 5) | register)


def encode_add_imm(destination, base, immediate):
    return (0x91000000 | ((immediate & 0xfff) << 10) |
            (base << 5) | destination)


def encode_sub_imm(destination, base, immediate):
    return (0xd1000000 | ((immediate & 0xfff) << 10) |
            (base << 5) | destination)


def encode_adr(pc, target, register):
    delta = target - pc
    check(-(1 << 20) <= delta < (1 << 20), 'ADR target is out of range')
    immediate = delta & 0x1fffff
    return (0x10000000 | ((immediate & 0x3) << 29) |
            (((immediate >> 2) & 0x7ffff) << 5) | register)


def encode_stp(target, target2, base, immediate, index):
    bases = {0: 0xa9000000, 1: 0xa8800000, 3: 0xa9800000}
    check(index in bases, 'unsupported pair index mode')
    check(immediate % 8 == 0, 'STP immediate is not scaled by 8')
    return (bases[index] | (((immediate // 8) & 0x7f) << 15) |
            (target2 << 10) | (base << 5) | target)


def encode_ldr_unsigned(target, base, immediate):
    check(immediate % 8 == 0, 'LDR immediate is not scaled by 8')
    return (0xf9400000 | (((immediate // 8) & 0xfff) << 10) |
            (base << 5) | target)


def encode_str_unsigned(target, base, immediate):
    check(immediate % 8 == 0, 'STR immediate is not scaled by 8')
    return (0xf9000000 | (((immediate // 8) & 0xfff) << 10) |
            (base << 5) | target)


def encode_str_unscaled(target, base, immediate, index):
    bases = {0: 0xf8000000, 1: 0xf8000400, 3: 0xf8000c00}
    check(index in bases, 'unsupported unscaled index mode')
    return (bases[index] | ((immediate & 0x1ff) << 12) |
            (base << 5) | target)


def encode_strb_unsigned(target, base, immediate):
    return (0x39000000 | ((immediate & 0xfff) << 10) |
            (base << 5) | target)


def encode_mov(destination, source):
    return 0xaa0003e0 | (source << 16) | destination


def encode_bl(pc, target):
    delta = target - pc
    check(delta % 4 == 0, 'BL target is not word aligned')
    return 0x94000000 | ((delta >> 2) & 0x03ffffff)


def encode_b(pc, target):
    delta = target - pc
    check(delta % 4 == 0, 'B target is not word aligned')
    return 0x14000000 | ((delta >> 2) & 0x03ffffff)


def encode_b_cond(pc, target, condition):
    delta = target - pc
    check(delta % 4 == 0, 'B.cond target is not word aligned')
    return 0x54000000 | (((delta >> 2) & 0x7ffff) << 5) | condition


def encode_blr(register):
    return 0xd63f0000 | (register << 5)


def check_decoders():
    check(decode_adrp(encode_adrp(0x1100, 0x8000, 8), 0x1100) == (0x8000, 8),
          'ADRP decode failed')
    check(decode_adrp(0x10000000, 0x1100) is None,
          'ADR word decoded as ADRP')
    check(decode_adr(0x10000080, 0x1000) == (0x1010, 0),
          'ADR decode failed')
    check(decode_adr(encode_adrp(0x1100, 0x8000, 8), 0x1100) is None,
          'ADRP word decoded as ADR')
    check(decode_addsub(encode_add_imm(8, 8, 0x878)) ==
          ('add', 8, 8, 0x878), 'ADD decode failed')
    check(decode_addsub(0xd1000000 | (0x10 << 10) | (8 << 5) | 8) ==
          ('sub', 8, 8, 0x10), 'SUB decode failed')
    check(decode_mem(encode_str_unscaled(31, 8, -8, 1)) ==
          ('store', 8, True, 8, 31, -8, 1), 'post-index store decode failed')
    check(decode_mem(encode_strb_unsigned(31, 8, 0x87b)) ==
          ('store', 1, True, 8, 31, 0x87b, None),
          'byte store decode failed')
    check(decode_mem(encode_ldr_unsigned(9, 8, 0x198)) ==
          ('load', 8, False, 8, 9, 0x198, None), 'LDR decode failed')


def run_sequence(data, words, start=0x1100):
    mutated = bytearray(data)
    for index, word in enumerate(words):
        struct.pack_into('<I', mutated, pe_offset(mutated, start + 4 * index),
                         word)
    return scan_references(bytes(mutated)), start, start + 4 * len(words)


def run_direct(data_bytes, words, values=None, steps=MAX_STEPS,
               budget=MAX_BUDGET, invalid=0):
    text = struct.pack(f'<{len(words)}I', *words)
    result = ScanResult()
    all_invalid = (1 << (len(data_bytes) // 8)) - 1
    scan_path(result, text, data_bytes, TEXT_VMA, dict(values or {}), invalid,
              0, frozenset(), steps, budget, {}, all_invalid)
    return result


def sequence_stores(result, start, end):
    return [item for item in result.stores if start <= item[0] < end]


def check_scanner_controls(data):
    adrp = encode_adrp(0x1100, 0x8000, 8)
    add = encode_add_imm(8, 8, 0x878)
    store = encode_str_unsigned(31, 8, 0)
    ret = 0xd65f03c0

    result, start, end = run_sequence(data, [adrp, add, store, ret])
    stores = sequence_stores(result, start, end)
    check(any(item[2] == OVERRIDE for item in stores),
          'direct store to 0x8878 was not detected')

    result, start, end = run_sequence(
        data, [adrp, encode_add_imm(8, 8, 0x874), store, ret])
    stores = sequence_stores(result, start, end)
    check(any(write_intersects(item[2], item[1], OVERRIDE, OVERRIDE_END)
              for item in stores),
          '8-byte store overlapping 0x8878 was not detected')

    result, start, end = run_sequence(
        data, [adrp, encode_strb_unsigned(31, 8, 0x87b), ret])
    stores = sequence_stores(result, start, end)
    check(any(write_intersects(item[2], item[1], OVERRIDE, OVERRIDE_END)
              for item in stores),
          'byte store overlapping 0x8878 was not detected')

    result, start, end = run_sequence(
        data, [adrp, encode_str_unsigned(31, 8, 0x880), ret])
    stores = sequence_stores(result, start, end)
    check(not any(write_intersects(item[2], item[1], OVERRIDE, OVERRIDE_END)
                  for item in stores),
          'adjacent store at 0x8880 was misclassified as an override write')

    result, start, end = run_sequence(
        data, [adrp, encode_add_imm(8, 8, 0x880),
               encode_str_unscaled(31, 8, -8, 1), store, ret])
    stores = sequence_stores(result, start, end)
    check((0x1108, 8, 0x8880) in stores,
          'post-index store address was not the original base')
    check((0x110c, 8, OVERRIDE) in stores,
          'post-index base update did not reach 0x8878')

    result, start, end = run_sequence(
        data, [adrp, encode_add_imm(8, 8, 0x880),
               encode_str_unscaled(31, 8, -8, 3), store, ret])
    stores = sequence_stores(result, start, end)
    check((0x1108, 8, OVERRIDE) in stores and
          (0x110c, 8, OVERRIDE) in stores,
          'pre-index store or base update did not reach 0x8878')

    result, start, end = run_sequence(
        data, [adrp, encode_add_imm(8, 8, 0x870),
               encode_stp(0, 1, 8, 0, 0), ret])
    stores = sequence_stores(result, start, end)
    check((0x1108, 8, 0x8870) in stores and
          (0x1108, 8, OVERRIDE) in stores,
          'paired store did not record both 8-byte accesses')

    result, start, end = run_sequence(
        data, [adrp, encode_add_imm(8, 8, 0x870),
               encode_stp(0, 1, 8, 16, 1),
               encode_str_unsigned(31, 8, 0), ret])
    stores = sequence_stores(result, start, end)
    check((0x1108, 8, 0x8870) in stores and
          (0x1108, 8, OVERRIDE) in stores,
          'post-index paired store did not record both accesses')
    check((0x110c, 8, 0x8880) in stores,
          'post-index paired store writeback was not applied')

    result, start, end = run_sequence(
        data, [adrp, encode_add_imm(8, 8, 0x880),
               encode_stp(0, 1, 8, -16, 3),
               encode_str_unsigned(31, 8, 0), ret])
    stores = sequence_stores(result, start, end)
    check((0x1108, 8, 0x8870) in stores and
          (0x1108, 8, OVERRIDE) in stores,
          'pre-index paired store did not record both accesses')
    check((0x110c, 8, 0x8870) in stores,
          'pre-index paired store writeback was not applied')

    data_bytes = firmware_data(data)

    check(write_intersects(MASK64 - 3, 8, 0, 4),
          'wrapped store range was not intersected')
    check(not write_intersects(MASK64 - 3, 8, OVERRIDE, OVERRIDE_END),
          'wrapped store was misclassified as an override write')

    shifted_add = 0x91400000 | (0x8 << 10) | (8 << 5) | 8
    result = run_direct(data_bytes, [
        encode_add_imm(8, 8, 1),
        shifted_add,
        encode_str_unsigned(31, 8, 0x878),
        ret], values={8: MASK64})
    check((0x1008, 8, OVERRIDE) in result.stores,
          'demonstrated ADD overflow missed the 0x8878 store')

    result = run_direct(data_bytes, [
        encode_add_imm(8, 8, 0x879),
        shifted_add,
        encode_str_unsigned(31, 8, 0),
        ret], values={8: MASK64})
    check(any(write_intersects(item[2], item[1], OVERRIDE, OVERRIDE_END)
              for item in result.stores),
          '64-bit wrapping ADD did not reach 0x8878')

    result = run_direct(data_bytes, [
        encode_str_unscaled(31, 8, 8, 1),
        encode_str_unsigned(31, 8, 0),
        ret], values={8: MASK64})
    check((0x1000, 8, MASK64) in result.stores and
          (0x1004, 8, 7) in result.stores,
          'post-index base update did not wrap at 64 bits')

    result = run_direct(data_bytes, [
        encode_str_unscaled(31, 8, 8, 3),
        encode_str_unsigned(31, 8, 0),
        ret], values={8: MASK64})
    check((0x1000, 8, 7) in result.stores and
          (0x1004, 8, 7) in result.stores,
          'pre-index base update did not wrap at 64 bits')

    result, start, end = run_sequence(
        data, [adrp, encode_ldr_unsigned(9, 8, 0x198),
               encode_ldr_unsigned(10, 9, 0x18),
               encode_str_unsigned(31, 10, 0), ret])
    stores = sequence_stores(result, start, end)
    check((0x110c, 8, DESCRIPTOR_ARRAY) in stores,
          'new-register pointer chain did not resolve the descriptor array')

    result, start, end = run_sequence(
        data, [adrp, encode_ldr_unsigned(8, 8, 0), add, store, ret])
    stores = sequence_stores(result, start, end)
    check(not any(item[2] == OVERRIDE for item in stores),
          'root register overwrite was not applied')

    result, start, end = run_sequence(
        data, [adrp, encode_mov(0, 8), encode_bl(0x1108, 0x1000),
               add, store, ret])
    stores = sequence_stores(result, start, end)
    check(not any(item[2] == OVERRIDE for item in stores),
          'caller-saved register survived a call clobber')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 8),
        encode_ldr_unsigned(31, 8, 0x198),
        encode_mov(9, 31),
        encode_str_unsigned(0, 9, 0),
        ret])
    check(not sequence_stores(result, 0x1000, 0x1010),
          'load into XZR was tracked as a pointer value')

    result, start, end = run_sequence(
        data, [encode_adrp(0x1100, 0x8000, 31),
               encode_str_unsigned(0, 31, 0x878), ret])
    stores = sequence_stores(result, start, end)
    check(not any(write_intersects(item[2], item[1], OVERRIDE, OVERRIDE_END)
                  for item in stores),
          'ADRP XZR seeded the stack pointer as an address register')

    result, start, end = run_sequence(
        data, [encode_adr(0x1100, 0x1000, 31),
               encode_str_unsigned(0, 31, 0x878), ret])
    stores = sequence_stores(result, start, end)
    check(not any(write_intersects(item[2], item[1], OVERRIDE, OVERRIDE_END)
                  for item in stores),
          'ADR XZR seeded the stack pointer as an address register')

    result = run_direct(data_bytes, [
        encode_sub_imm(8, 8, 1),
        encode_str_unsigned(31, 8, 0),
        ret], values={8: 0})
    check((0x1004, 8, MASK64) in result.stores,
          'SUB underflow did not wrap at 64 bits')
    check(write_intersects(MASK64, 8, 0, 8),
          'wrapped SUB store range was not intersected')

    result = run_direct(data_bytes, [
        encode_strb_unsigned(31, 8, 0x879),
        ret], values={8: MASK64})
    check((0x1000, 1, 0x878) in result.stores,
          'effective-address wrap was not applied')

    result = run_direct(data_bytes, [
        encode_stp(0, 1, 8, 0, 0), ret], values={8: MASK64 - 4})
    check((0x1000, 8, MASK64 - 4) in result.stores and
          (0x1000, 8, 3) in result.stores,
          'paired store crossing the address-space boundary was not masked')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 8),
        encode_str_unscaled(8, 31, -8, 1),
        ret])
    check(not sequence_stores(result, 0x1000, 0x100c),
          'stack store was reported as a resolved memory store')
    check(not [item for item in result.unresolved if item[0] == 0x1004],
          'stack store was reported as unresolved')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 8),
        encode_str_unsigned(0, 8, 0x198),
        encode_ldr_unsigned(9, 8, 0x198),
        encode_str_unsigned(0, 9, 0),
        ret])
    check((0x1004, 8, 0x8198) in result.stores,
          'pointer-slot store was not resolved')
    check(not [item for item in result.stores if item[0] == 0x100c],
          'load used pointer bytes overwritten earlier in the path')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 8),
        encode_ldr_unsigned(9, 0, 0),
        ret])
    check((0x1004, 'load', 8) in result.unresolved,
          'unresolved load was not reported')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 8),
        0xffffffff,
        encode_add_imm(8, 8, 0x878),
        encode_str_unsigned(0, 8, 0),
        ret])
    check((0x1004, 'unsupported') in result.boundaries,
          'unsupported instruction did not stop the path')
    check(not sequence_stores(result, 0x1000, 0x1010),
          'path continued past an unsupported instruction')

    result = run_direct(data_bytes,
                        [encode_adrp(0x1000, 0x8000, 8)] +
                        [0xd503201f] * (MAX_STEPS + 1))
    check(any(reason == 'steps' for _, reason in result.boundaries),
          'step exhaustion was not reported')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 8),
        encode_b_cond(0x1004, 0x1008, 0),
        ret], budget=0)
    check((0x1004, 'budget') in result.boundaries,
          'budget exhaustion was not reported')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 19),
        encode_mov(0, 19),
        encode_bl(0x1008, 0x1020),
        encode_ldr_unsigned(9, 19, 0x198),
        encode_str_unsigned(0, 9, 0),
        ret,
        0xd503201f,
        0xd503201f,
        encode_str_unsigned(1, 0, 0x198),
        ret])
    check((0x1020, 8, 0x8198) in result.stores,
          'callee pointer-slot store was not resolved')
    check(not [item for item in result.stores if item[0] == 0x1010],
          'caller trusted a pointer slot overwritten by the callee')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 19),
        encode_mov(0, 19),
        encode_bl(0x1008, 0x1020),
        encode_ldr_unsigned(9, 19, 0x198),
        encode_str_unsigned(0, 9, 0),
        ret,
        0xd503201f,
        0xd503201f,
        0xd503201f,
        0xffffffff,
        ret])
    check((0x1024, 'unsupported') in result.boundaries,
          'truncated callee did not report the unsupported instruction')
    check(not [item for item in result.stores if item[0] == 0x1010],
          'caller trusted a pointer slot after a truncated call')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 19),
        encode_blr(19),
        encode_ldr_unsigned(9, 19, 0x198),
        encode_str_unsigned(0, 9, 0),
        ret])
    check((0x1004, 'indirect branch') in result.boundaries,
          'indirect call was not reported')
    check(not [item for item in result.stores if item[0] == 0x100c],
          'caller trusted a pointer slot after an indirect call')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 19),
        encode_str_unsigned(0, 1, 0),
        encode_ldr_unsigned(9, 19, 0x198),
        encode_str_unsigned(0, 9, 0),
        ret])
    check((0x1004, 'store', 8) in result.unresolved,
          'unresolved store was not reported')
    check(not [item for item in result.stores if item[0] == 0x100c],
          'caller trusted a pointer slot after an unresolved store')

    result = run_direct(data_bytes, [
        encode_b_cond(0x1000, 0x1010, 0),
        encode_ldr_unsigned(9, 19, 0x198),
        encode_str_unsigned(0, 9, 0),
        ret,
        encode_str_unsigned(0, 19, 0x198),
        ret], values={19: 0x8000})
    check((0x1010, 8, 0x8198) in result.stores,
          'taken-branch pointer-slot store was not resolved')
    check((0x1008, 8, 0x8178) in result.stores,
          'fallthrough lost its valid pointer after a sibling-branch write')
    check(not [item for item in result.unresolved if item[0] == 0x1008],
          'fallthrough store was reported unresolved after a sibling write')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 19),
        encode_mov(0, 19),
        encode_bl(0x1008, 0x1020),
        encode_ldr_unsigned(9, 19, 0x198),
        encode_str_unsigned(0, 9, 0),
        ret,
        0xd503201f,
        0xd503201f,
        encode_b(0x1020, 0x1020),
        ret])
    check((0x1020, 'visited') in result.boundaries,
          'revisited state was not reported')
    check((0x1010, 'store', 8) in result.unresolved,
          'revisited state did not invalidate the caller pointer slot')
    check(not [item for item in result.stores if item[0] == 0x1010],
          'caller trusted a pointer slot after a revisited callee')

    result = run_direct(data_bytes, [
        encode_adrp(0x1000, 0x8000, 19),
        encode_mov(0, 19),
        encode_bl(0x1008, 0x1020),
        encode_ldr_unsigned(9, 19, 0x198),
        encode_str_unsigned(0, 9, 0),
        ret,
        0xd503201f,
        0xd503201f,
        encode_b_cond(0x1020, 0x1030, 0),
        ret,
        0xd503201f,
        0xd503201f,
        encode_str_unsigned(0, 0, 0x198),
        ret])
    check((0x1030, 8, 0x8198) in result.stores,
          'callee taken-branch pointer-slot store was not resolved')
    check((0x1010, 'store', 8) in result.unresolved,
          'callee conditional write did not invalidate the caller pointer slot')
    check(not [item for item in result.stores if item[0] == 0x1010],
          'caller trusted a pointer slot after a conditional callee write')

    mutated = bytearray(data)
    struct.pack_into('<I', mutated, pe_offset(mutated, 0x38b4), 0xf9043d08)
    result = scan_references(bytes(mutated))
    check([item for item in result.stores
           if write_intersects(item[2], item[1], OVERRIDE, OVERRIDE_END)],
          'scanner did not detect a synthetic store at 0x38b4')


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
    check_decoders()
    check_scanner_controls(data)
    result = check_scan(data)
    check_constant_absence(data)
    check_model()
    unresolved_loads = sum(1 for item in result.unresolved if item[1] == 'load')
    unresolved_stores = sum(1 for item in result.unresolved if item[1] == 'store')
    print(f'spmidxe descriptor base: sha256={actual} source={source} '
          f'accesses={len(result.accesses)} stores={len(result.stores)} '
          f'calls={len(result.calls)} boundaries={len(result.boundaries)} '
          f'unresolved_loads={unresolved_loads} '
          f'unresolved_stores={unresolved_stores} '
          f'clobbers={len(result.clobbers)} PASS')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except AssertionError as error:
        sys.exit(f'test-gta4l-spmidxe-descriptor-base: {error}')
